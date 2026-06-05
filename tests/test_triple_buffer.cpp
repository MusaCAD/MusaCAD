#include <atomic>
#include <cstdint>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "musacad/core/render_snapshot.hpp"
#include "musacad/core/threading/triple_buffer.hpp"

using namespace musacad::core;

// Constraint A test: a writer publishes monotonically-versioned snapshots in a
// tight loop while a reader validates that every snapshot it observes is
// internally consistent (checksum matches the payload it guards) and that
// versions never go backwards. Many iterations, run under ASan/UBSan (dev) and
// ThreadSanitizer (tsan). A single API call would not exercise the race; this
// hammers the handoff so a torn or stale read would be caught.
TEST_CASE("TripleBuffer SPSC: reader always sees a consistent snapshot") {
    TripleBuffer<RenderSnapshot> tb;
    constexpr std::uint64_t kIterations = 200'000;

    std::atomic<bool> done{false};
    std::atomic<bool> reader_ok{true};
    std::atomic<std::uint64_t> reads_observed{0};
    std::atomic<std::uint64_t> max_version_seen{0};

    std::jthread writer([&] {
        for (std::uint64_t v = 1; v <= kIterations; ++v) {
            RenderSnapshot& b = tb.write_buffer();
            // Rebuild the payload from scratch each publish (as the real engine
            // does), so the buffer is momentarily inconsistent with its old
            // checksum mid-write. A correct triple buffer guarantees the reader
            // never observes the buffer being written.
            b.points.clear();
            b.line_vertices.clear();
            const std::size_t k = static_cast<std::size_t>(v % 16u) + 1u;
            for (std::size_t i = 0; i < k; ++i) {
                b.line_vertices.push_back(Vec2{static_cast<double>(v), static_cast<double>(i)});
            }
            b.version = v;
            b.checksum = b.compute_checksum();
            tb.publish();
        }
        done.store(true, std::memory_order_release);
    });

    std::uint64_t prev = 0;
    for (;;) {
        const bool fresh = tb.acquire();
        if (fresh) {
            const RenderSnapshot& s = tb.read_buffer();
            if (!s.consistent() || s.version < prev) {
                reader_ok.store(false, std::memory_order_relaxed);
                break;
            }
            prev = s.version;
            reads_observed.fetch_add(1, std::memory_order_relaxed);
            max_version_seen.store(prev, std::memory_order_relaxed);
        }
        if (done.load(std::memory_order_acquire) && !fresh) {
            // Drain the final published buffer, if any.
            if (tb.acquire()) {
                const RenderSnapshot& s = tb.read_buffer();
                if (!s.consistent() || s.version < prev) {
                    reader_ok.store(false, std::memory_order_relaxed);
                } else {
                    prev = s.version;
                }
            }
            break;
        }
    }
    writer.join();

    REQUIRE(reader_ok.load());
    REQUIRE(reads_observed.load() > 0); // the reader actually saw published data
    REQUIRE(prev > 0);
    REQUIRE(prev <= kIterations);
}

TEST_CASE("TripleBuffer: acquire returns false until first publish") {
    TripleBuffer<RenderSnapshot> tb;
    REQUIRE_FALSE(tb.acquire());
    tb.write_buffer().version = 42;
    tb.write_buffer().checksum = tb.write_buffer().compute_checksum();
    tb.publish();
    REQUIRE(tb.acquire());
    REQUIRE(tb.read_buffer().version == 42);
    REQUIRE_FALSE(tb.acquire()); // no new data
}
