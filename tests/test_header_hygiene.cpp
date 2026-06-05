// Header-hygiene compile guarantee: this translation unit includes every public
// core header and links only against musacad_core (no Qt, Vulkan, or any
// third-party geometry/math library). If a public core header pulled in an
// external type, this file would fail to compile -- complementing the
// header_scan.cmake textual check.

#include <type_traits>

#include "musacad/core/command.hpp"
#include "musacad/core/entity_handle.hpp"
#include "musacad/core/generational_arena.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/geometry_kernel.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/math/math.hpp"
#include "musacad/core/native_kernel_2d.hpp"
#include "musacad/core/render_snapshot.hpp"
#include "musacad/core/threading/mpsc_queue.hpp"
#include "musacad/core/threading/triple_buffer.hpp"
#include "musacad/core/version.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace musacad::core;

static_assert(std::is_trivially_copyable_v<EntityHandle>);
static_assert(std::is_trivially_copyable_v<Vec2>);
static_assert(std::is_trivially_copyable_v<Vec3>);

TEST_CASE("Core public headers are self-contained and native-only") {
    NativeKernel2D kernel;
    GeometryStore store;
    const EntityHandle h = store.add_line({0.0, 0.0}, {1.0, 1.0});
    std::vector<Vec2> out;
    kernel.tessellate(store, h, kDefaultTessTolerance, out);
    REQUIRE(out.size() == 2);
}
