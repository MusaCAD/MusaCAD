#include "musacad/ui/viewport_window.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"

#include <QExposeEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QResizeEvent>
#include <QSurfaceFormat>
#include <QWheelEvent>

#include "musacad/render/frame_stats.hpp"
#include "musacad/render/gl_backend.hpp"
#include "musacad/render/viewport_renderer.hpp"

namespace musacad::ui {

namespace {
std::string overlay_text(double fps, double ms) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "FPS %d  %d MS", static_cast<int>(fps + 0.5),
                  static_cast<int>(ms + 0.5));
    return std::string(buf);
}
} // namespace

ViewportWindow::ViewportWindow(core::GeometryEngine& engine, QWindow* parent)
    : QWindow(parent), engine_(engine) {
    setSurfaceType(QWindow::OpenGLSurface);
    QSurfaceFormat fmt;
    fmt.setVersion(4, 5);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapInterval(1); // vsync
    setFormat(fmt);
}

ViewportWindow::~ViewportWindow() { stop_render_thread(); }

void ViewportWindow::set_initial_view(render::Vec2 min_world, render::Vec2 max_world) noexcept {
    std::scoped_lock lock(camera_mutex_);
    init_min_ = min_world;
    init_max_ = max_world;
    has_initial_view_ = true;
}

void ViewportWindow::zoom_extents() {
    // Deferred to the render thread, which has the snapshot bounds.
    zoom_extents_requested_.store(true, std::memory_order_relaxed);
}

void ViewportWindow::zoom_scale(double factor) {
    std::scoped_lock lock(camera_mutex_);
    const double cx = static_cast<double>(camera_.viewport_width()) * 0.5;
    const double cy = static_cast<double>(camera_.viewport_height()) * 0.5;
    camera_.zoom_about({cx, cy}, factor);
}

void ViewportWindow::update_viewport_size() noexcept {
    const double dpr = devicePixelRatio();
    fb_width_.store(std::max(1, static_cast<int>(static_cast<double>(width()) * dpr)),
                    std::memory_order_relaxed);
    fb_height_.store(std::max(1, static_cast<int>(static_cast<double>(height()) * dpr)),
                     std::memory_order_relaxed);
}

void ViewportWindow::exposeEvent(QExposeEvent*) {
    if (isExposed()) {
        update_viewport_size();
        start_render_thread();
    }
}

void ViewportWindow::resizeEvent(QResizeEvent*) { update_viewport_size(); }

void ViewportWindow::start_render_thread() {
    if (render_thread_.joinable()) {
        return;
    }
    render_thread_ = std::jthread([this](std::stop_token token) { render_loop(std::move(token)); });
}

void ViewportWindow::stop_render_thread() noexcept {
    if (render_thread_.joinable()) {
        render_thread_.request_stop();
        render_thread_.join();
    }
}

void ViewportWindow::render_loop(std::stop_token token) {
    // The render thread exclusively owns the GL context.
    QOpenGLContext context;
    context.setFormat(format());
    if (!context.create() || !context.makeCurrent(this)) {
        std::fprintf(stderr, "[musacad_ui] could not create/make-current GL context on render "
                             "thread; viewport disabled\n");
        return;
    }

    auto device = render::create_gl_device();
    auto target = render::create_gl_default_target(fb_width_.load(), fb_height_.load());
    if (!device || !target) {
        std::fprintf(stderr, "[musacad_ui] GL device creation failed; viewport disabled\n");
        context.doneCurrent();
        return;
    }
    render::ViewportRenderer renderer(*device);
    render::FrameStats stats;

    const bool smoke = std::getenv("MUSACAD_SMOKE") != nullptr;
    int smoke_frames = 0;
    auto last = std::chrono::steady_clock::now();

    while (!token.stop_requested()) {
        const int w = fb_width_.load(std::memory_order_relaxed);
        const int h = fb_height_.load(std::memory_order_relaxed);
        target->resize(w, h);

        render::Camera2D cam;
        {
            std::scoped_lock lock(camera_mutex_);
            camera_.set_viewport(w, h);
            if (!camera_initialized_ && has_initial_view_) {
                camera_.frame_bounds(init_min_, init_max_, 0.1);
                camera_initialized_ = true;
            }
            cam = camera_;
        }

        engine_.consume_snapshot();
        const core::RenderSnapshot& snap = engine_.snapshot();
        if (zoom_extents_requested_.exchange(false, std::memory_order_relaxed) && snap.has_bounds) {
            std::scoped_lock lock(camera_mutex_);
            camera_.frame_bounds(snap.bounds_min, snap.bounds_max, 0.1);
            cam = camera_;
        }
        // Share the latest snap point back to the GUI thread for click-time picks.
        snap_has_.store(snap.has_snap, std::memory_order_relaxed);
        if (snap.has_snap) {
            snap_x_.store(snap.snap_point.x, std::memory_order_relaxed);
            snap_y_.store(snap.snap_point.y, std::memory_order_relaxed);
        }

        renderer.set_grid_visible(modes_ == nullptr || modes_->grid.load(std::memory_order_relaxed));
        renderer.set_cursor(cursor_inside_.load(std::memory_order_relaxed),
                            static_cast<float>(cursor_px_x_.load(std::memory_order_relaxed)),
                            static_cast<float>(cursor_px_y_.load(std::memory_order_relaxed)));
        renderer.set_overlay_text(overlay_text(stats.fps(), stats.average_frame_ms()));
        renderer.render(*target, snap, cam);
        context.swapBuffers(this);

        const auto now = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        stats.add_frame(dt);
        fps_.store(stats.fps(), std::memory_order_relaxed);
        frames_rendered_.fetch_add(1, std::memory_order_relaxed);

        if (smoke && ++smoke_frames >= 30) {
            break; // headless/CI: render a few frames then stop
        }
    }

    context.doneCurrent();
}

void ViewportWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        panning_ = true;
        last_x_ = event->position().x();
        last_y_ = event->position().y();
        return;
    }
    if (event->button() == Qt::LeftButton && processor_ != nullptr) {
        const double dpr = devicePixelRatio();
        core::Vec2 world;
        double scale = 1.0;
        {
            std::scoped_lock lock(camera_mutex_);
            world = camera_.screen_to_world(
                core::Vec2{event->position().x() * dpr, event->position().y() * dpr});
            scale = camera_.scale();
        }
        std::optional<core::Vec2> snap;
        if (snap_has_.load(std::memory_order_relaxed)) {
            snap = core::Vec2{snap_x_.load(std::memory_order_relaxed),
                              snap_y_.load(std::memory_order_relaxed)};
        }
        processor_->set_pick_radius(10.0 / scale); // 10px aperture for ERASE-pick
        processor_->pick_point(world, snap);
    }
}

void ViewportWindow::mouseMoveEvent(QMouseEvent* event) {
    const double dpr = devicePixelRatio();
    const core::Vec2 screen_px{event->position().x() * dpr, event->position().y() * dpr};

    cursor_inside_.store(true, std::memory_order_relaxed);
    cursor_px_x_.store(screen_px.x, std::memory_order_relaxed);
    cursor_px_y_.store(screen_px.y, std::memory_order_relaxed);

    double scale = 1.0;
    core::Vec2 world;
    {
        std::scoped_lock lock(camera_mutex_);
        world = camera_.screen_to_world(screen_px);
        scale = camera_.scale();
    }
    Q_EMIT cursorWorldMoved(world.x, world.y);

    // Push the cursor to the geometry thread so it can compute the snap candidate
    // and publish it via the snapshot. Coalesced and non-blocking -- no per-move
    // synchronous query.
    const bool osnap = modes_ == nullptr || modes_->osnap.load(std::memory_order_relaxed);
    constexpr double kApertonPx = 10.0;
    engine_.submit(core::SetCursorCommand{world, osnap ? kApertonPx / scale : 0.0, osnap});

    if (panning_) {
        const double dx = event->position().x() - last_x_;
        const double dy = event->position().y() - last_y_;
        last_x_ = event->position().x();
        last_y_ = event->position().y();
        std::scoped_lock lock(camera_mutex_);
        camera_.pan_pixels(core::Vec2{dx * dpr, dy * dpr});
    }
}

void ViewportWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        panning_ = false;
    }
}

void ViewportWindow::wheelEvent(QWheelEvent* event) {
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0.0) {
        return;
    }
    const double factor = std::pow(1.2, steps);
    const double dpr = devicePixelRatio();
    const musacad::core::Vec2 anchor{event->position().x() * dpr, event->position().y() * dpr};
    std::scoped_lock lock(camera_mutex_);
    camera_.zoom_about(anchor, factor);
}

} // namespace musacad::ui
