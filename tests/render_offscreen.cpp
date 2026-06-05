// Offscreen render verification + benchmark on a real GL context.
//
// Verifies, on actual hardware:
//   * the instanced path draws a ~1M-primitive scene in a few draw calls;
//   * constraint A -- deleting entities returns the uploaded instance count to
//     baseline (no dead vertex-pool residue is uploaded);
//   * constraint B -- camera-only frames re-upload ZERO scene bytes, and reports
//     measured render throughput (offscreen, no vsync) for the 1M scene;
//   * geometry is actually rasterized (non-background pixels read back).
//
// Run under the release preset for representative timing. Exit code 0 on pass.

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>

#include "musacad/core/geometry_store.hpp"
#include "musacad/core/native_kernel_2d.hpp"
#include "musacad/core/render_snapshot.hpp"
#include "musacad/core/scene_snapshot.hpp"
#include "musacad/render/camera.hpp"
#include "musacad/render/gl_backend.hpp"
#include "musacad/render/viewport_renderer.hpp"

using namespace musacad;
using musacad::core::Vec2;

namespace {
int g_failures = 0;
void check(bool cond, const char* what) {
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) {
        ++g_failures;
    }
}
} // namespace

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);

    QSurfaceFormat fmt;
    fmt.setVersion(4, 5);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(fmt);

    QOffscreenSurface surface;
    surface.setFormat(fmt);
    surface.create();
    if (!surface.isValid()) {
        std::printf("offscreen surface invalid\n");
        return 77; // environment cannot provide GL
    }
    QOpenGLContext ctx;
    ctx.setFormat(fmt);
    if (!ctx.create() || !ctx.makeCurrent(&surface)) {
        std::printf("no GL context\n");
        return 77;
    }
    QOpenGLFunctions* base = ctx.functions();

    const int W = 1280;
    const int H = 720;
    auto device = render::create_gl_device();
    auto target = render::create_gl_offscreen_target(W, H);
    if (!device || !target) {
        std::printf("GL device/target creation failed\n");
        return 77;
    }
    std::printf("Backend  : %s\n", device->backend_name());
    std::printf("Renderer : %s\n",
                reinterpret_cast<const char*>(base->glGetString(GL_RENDERER)));

    render::ViewportRenderer renderer(*device);
    render::Camera2D cam;
    cam.set_viewport(W, H);

    // ----- Build a ~1,000,000-line scene -----
    const std::size_t kCount =
        (argc > 1) ? static_cast<std::size_t>(std::atoll(argv[1])) : 1'000'000;
    const int side = 1000;
    core::GeometryStore store;
    core::NativeKernel2D kernel;
    store.reserve_lines(kCount);
    for (std::size_t i = 0; i < kCount; ++i) {
        const double x = static_cast<double>(i % side);
        const double y = static_cast<double>(i / side);
        store.add_line(Vec2{x, y}, Vec2{x + 0.85, y});
    }
    core::RenderSnapshot snap;
    core::build_render_snapshot(store, kernel, snap);
    snap.version = 1;
    snap.geometry_version = 1;
    cam.frame_bounds(Vec2{0.0, 0.0}, Vec2{static_cast<double>(side), static_cast<double>(side)}, 0.05);
    std::printf("Scene    : %zu lines (%zu line-instances)\n", kCount, snap.line_vertices.size() / 2);

    // ----- First frame: uploads scene, draws, reads back -----
    renderer.set_overlay_text("FPS 000 0 MS");
    renderer.render(*target, snap, cam);
    base->glFinish();
    const render::RenderStats s0 = renderer.stats();
    std::printf("Draw calls         : %u\n", s0.draw_calls);
    std::printf("Line instances     : %zu\n", s0.line_instances);
    std::printf("First-frame upload : %zu bytes (scene)\n", s0.scene_uploaded_bytes);

    std::printf("\n== Draw-call count ==\n");
    check(s0.draw_calls <= 6, "scene drawn in <= 6 draw calls");
    check(s0.line_instances == kCount, "all lines drawn as instances");
    check(s0.scene_uploaded_bytes > 0, "scene uploaded on first frame");

    // Draw-call count WITH crosshair + a snap marker (still bounded, not per-primitive).
    renderer.set_cursor(true, W * 0.5f, H * 0.5f);
    snap.has_snap = true;
    snap.snap_point = Vec2{static_cast<double>(side) * 0.5, static_cast<double>(side) * 0.5};
    snap.snap_type = core::SnapType::Endpoint;
    renderer.render(*target, snap, cam);
    base->glFinish();
    const std::uint32_t calls_with_aids = renderer.stats().draw_calls;
    std::printf("Draw calls (grid+scene+overlay+crosshair+marker): %u\n", calls_with_aids);
    check(calls_with_aids <= 8, "crosshair + snap marker keep draw calls bounded (<= 8)");
    snap.has_snap = false;
    renderer.set_cursor(false, 0.0f, 0.0f);

    // ----- Pixel proof: geometry actually rasterized -----
    const std::vector<std::uint8_t> pixels = render::read_offscreen_rgba(*target);
    std::size_t lit = 0;
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        if (pixels[i] > 60 || pixels[i + 1] > 60) {
            ++lit;
        }
    }
    std::printf("\n== Pixel proof ==\n");
    std::printf("Lit pixels         : %zu of %d\n", lit, W * H);
    check(lit > 1000, "geometry rasterized to the framebuffer");

    // ----- Constraint B: camera-only frames re-upload nothing; measure timing -----
    std::printf("\n== Constraint B: pan/zoom independent of edit load ==\n");
    const int kFrames = 300;
    std::size_t reuploaded = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int f = 0; f < kFrames; ++f) {
        // Pure camera motion -- no snapshot/version change, no geometry thread.
        cam.zoom_about(Vec2{W * 0.5, H * 0.5}, (f % 2 == 0) ? 1.01 : 1.0 / 1.01);
        cam.pan_pixels(Vec2{1.0, 0.5});
        renderer.render(*target, snap, cam);
        reuploaded += renderer.stats().scene_uploaded_bytes;
    }
    base->glFinish();
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / kFrames;
    std::printf("Scene re-upload over %d camera frames : %zu bytes\n", kFrames, reuploaded);
    std::printf("Mean frame time    : %.3f ms  (%.1f FPS, offscreen no-vsync)\n", ms, 1000.0 / ms);
    check(reuploaded == 0, "camera-only frames re-upload ZERO scene bytes");

    // ----- Constraint A: deletions return uploaded count to baseline -----
    std::printf("\n== Constraint A: deleted geometry leaves no residue ==\n");
    core::GeometryStore s2;
    core::RenderSnapshot snapA;
    s2.add_line(Vec2{0, 0}, Vec2{1, 0}); // baseline
    core::build_render_snapshot(s2, kernel, snapA);
    snapA.version = 10;
    snapA.geometry_version = 10;
    renderer.render(*target, snapA, cam);
    const std::size_t baseline = renderer.stats().line_instances;

    const std::array<Vec2, 5> poly{{{0, 0}, {1, 1}, {2, 0}, {3, 1}, {4, 0}}};
    std::vector<core::EntityHandle> handles;
    for (int i = 0; i < 50'000; ++i) {
        handles.push_back(s2.add_polyline(poly, false));
    }
    core::build_render_snapshot(s2, kernel, snapA);
    snapA.version = 11;
    snapA.geometry_version = 11;
    renderer.render(*target, snapA, cam);
    const std::size_t high = renderer.stats().line_instances;

    for (const auto h : handles) {
        s2.remove(h);
    }
    core::build_render_snapshot(s2, kernel, snapA);
    snapA.version = 12;
    snapA.geometry_version = 12;
    renderer.render(*target, snapA, cam);
    const std::size_t after = renderer.stats().line_instances;
    std::printf("Line instances: baseline=%zu  with-polylines=%zu  after-delete=%zu\n", baseline,
                high, after);
    check(high > baseline, "polylines increased the uploaded instance count");
    check(after == baseline, "deletion returned uploaded count to baseline (no residue)");

    ctx.doneCurrent();
    std::printf("\n%s\n", g_failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED");
    return g_failures == 0 ? 0 : 1;
}
