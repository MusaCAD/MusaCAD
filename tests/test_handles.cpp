#include <catch2/catch_test_macros.hpp>

#include "musacad/core/entity_handle.hpp"
#include "musacad/core/generational_arena.hpp"

using namespace musacad::core;

TEST_CASE("EntityHandle null sentinel") {
    constexpr EntityHandle null = EntityHandle::null();
    STATIC_REQUIRE(null.is_null());
    EntityHandle h{0, 1, EntityKind::Line};
    REQUIRE_FALSE(h.is_null());
    REQUIRE(h == EntityHandle{0, 1, EntityKind::Line});
    REQUIRE_FALSE(h == EntityHandle{0, 1, EntityKind::Point});
}

TEST_CASE("Arena generation invalidation and reuse") {
    GenerationalArena<int> arena;

    const auto s0 = arena.insert(10);
    const auto s1 = arena.insert(20);
    REQUIRE(arena.live_count() == 2);
    REQUIRE(arena.is_valid(s0.index, s0.generation));
    REQUIRE(*arena.get(s0.index, s0.generation) == 10);

    // Erase s0: its handle becomes stale; s1 untouched.
    REQUIRE(arena.erase(s0.index, s0.generation));
    REQUIRE_FALSE(arena.is_valid(s0.index, s0.generation));
    REQUIRE(arena.get(s0.index, s0.generation) == nullptr);
    REQUIRE(arena.live_count() == 1);
    REQUIRE(arena.is_valid(s1.index, s1.generation));
    REQUIRE(*arena.get(s1.index, s1.generation) == 20);

    // Double-erase is a no-op.
    REQUIRE_FALSE(arena.erase(s0.index, s0.generation));

    // Reuse: the freed slot index comes back with a bumped generation, and the
    // old handle stays invalid (detectably distinct).
    const auto s2 = arena.insert(30);
    REQUIRE(s2.index == s0.index);
    REQUIRE(s2.generation != s0.generation);
    REQUIRE(arena.is_valid(s2.index, s2.generation));
    REQUIRE_FALSE(arena.is_valid(s0.index, s0.generation));
    REQUIRE(*arena.get(s2.index, s2.generation) == 30);

    // Erasing with the wrong generation does nothing.
    REQUIRE_FALSE(arena.erase(s2.index, s0.generation));
    REQUIRE(arena.is_valid(s2.index, s2.generation));
}

TEST_CASE("Arena spans skip dead slots via alive()") {
    GenerationalArena<int> arena;
    const auto a = arena.insert(1);
    arena.insert(2);
    const auto c = arena.insert(3);
    REQUIRE(arena.erase(a.index, a.generation));
    REQUIRE(arena.erase(c.index, c.generation));

    REQUIRE(arena.slot_count() == 3); // slots retained
    std::size_t live = 0;
    int sum = 0;
    for (std::uint32_t i = 0; i < arena.slot_count(); ++i) {
        if (arena.alive(i)) {
            ++live;
            sum += arena.data()[i];
        }
    }
    REQUIRE(live == 1);
    REQUIRE(sum == 2);
    REQUIRE(arena.live_count() == 1);
}
