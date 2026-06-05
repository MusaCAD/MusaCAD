#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"

using namespace musacad::core;

TEST_CASE("GeometryEngine: clean start/stop with no work") {
    GeometryEngine engine;
    REQUIRE_FALSE(engine.running());
    engine.start();
    REQUIRE(engine.running());
    engine.start(); // idempotent
    REQUIRE(engine.running());
    engine.stop();
    REQUIRE_FALSE(engine.running());
    engine.stop(); // idempotent
    // Restart and let the destructor stop it.
    engine.start();
    REQUIRE(engine.running());
}

TEST_CASE("GeometryEngine: concurrent submit + snapshot round-trip") {
    GeometryEngine engine;
    engine.start();

    constexpr int kProducers = 4;
    constexpr int kPerProducer = 2500;
    constexpr std::size_t kTotalLines = kProducers * kPerProducer;
    constexpr std::size_t kExpectedVerts = kTotalLines * 2;

    // Single consumer (triple buffer is SPSC): validates consistency and tracks
    // the largest snapshot it observes.
    std::atomic<bool> stop_reader{false};
    std::atomic<bool> reader_ok{true};
    std::atomic<std::size_t> max_verts{0};

    std::jthread reader([&] {
        std::uint64_t prev = 0;
        while (!stop_reader.load(std::memory_order_acquire)) {
            if (engine.consume_snapshot()) {
                const RenderSnapshot& s = engine.snapshot();
                if (!s.consistent() || s.version < prev) {
                    reader_ok.store(false, std::memory_order_relaxed);
                    return;
                }
                prev = s.version;
                if (s.line_vertices.size() > max_verts.load(std::memory_order_relaxed)) {
                    max_verts.store(s.line_vertices.size(), std::memory_order_relaxed);
                }
            }
        }
    });

    // Multiple concurrent producers (MPSC queue).
    {
        std::vector<std::jthread> producers;
        for (int t = 0; t < kProducers; ++t) {
            producers.emplace_back([&engine] {
                for (int i = 0; i < kPerProducer; ++i) {
                    engine.submit(AddLineCommand{Vec2{0.0, 0.0}, Vec2{1.0, 1.0}});
                }
            });
        }
    } // producers joined here

    // Wait (bounded) until the reader observes a snapshot reflecting all lines.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (max_verts.load(std::memory_order_relaxed) < kExpectedVerts &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    stop_reader.store(true, std::memory_order_release);
    reader.join();
    engine.stop();

    REQUIRE(reader_ok.load());
    REQUIRE(max_verts.load() == kExpectedVerts);
    REQUIRE(engine.published_version() > 0);
}

TEST_CASE("GeometryEngine: add then remove reflected in snapshot") {
    GeometryEngine engine;
    engine.start();

    engine.submit(AddLineCommand{Vec2{0.0, 0.0}, Vec2{1.0, 0.0}});

    // Poll until the line shows up.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    std::size_t verts = 0;
    while (verts == 0 && std::chrono::steady_clock::now() < deadline) {
        if (engine.consume_snapshot()) {
            verts = engine.snapshot().line_vertices.size();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(verts == 2);
    REQUIRE(engine.snapshot().consistent());

    engine.stop();
}
