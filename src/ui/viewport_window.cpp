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
#include <QKeyEvent>
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
        selection_count_.store(static_cast<int>(snap.selection.size()), std::memory_order_relaxed);
        line_vertex_count_.store(static_cast<int>(snap.line_vertices.size()),
                                 std::memory_order_relaxed);
        dirty_.store(snap.dirty, std::memory_order_relaxed);
        document_version_.store(snap.document_version, std::memory_order_relaxed);

        // Surface the engine's command-result message (honest feedback) once.
        if (snap.status_version != status_version_.load(std::memory_order_relaxed)) {
            {
                std::scoped_lock lock(status_mutex_);
                status_ = snap.status;
            }
            status_version_.store(snap.status_version, std::memory_order_relaxed);
        }

        {
            render::RenderOverlay ov;
            {
                std::scoped_lock lock(overlay_mutex_);
                ov = overlay_;
            }
            renderer.set_overlay(std::move(ov));
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
        const core::Vec2 screen_px{event->position().x() * dpr, event->position().y() * dpr};
        core::Vec2 world;
        double scale = 1.0;
        {
            std::scoped_lock lock(camera_mutex_);
            world = camera_.screen_to_world(screen_px);
            scale = camera_.scale();
        }
        if (processor_->has_active_command()) {
            std::optional<core::Vec2> snap;
            if (snap_has_.load(std::memory_order_relaxed)) {
                snap = core::Vec2{snap_x_.load(std::memory_order_relaxed),
                                  snap_y_.load(std::memory_order_relaxed)};
            }
            processor_->set_pick_radius(10.0 / scale);
            processor_->pick_point(world, snap);
            rebuild_overlay();
        } else {
            // Idle: begin a selection drag (single click or window/crossing box).
            selecting_ = true;
            sel_additive_ = (event->modifiers() & Qt::ShiftModifier) != 0;
            sel_start_screen_ = screen_px;
            sel_start_world_ = world;
            sel_cur_world_ = world;
        }
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
    const std::uint32_t mask =
        modes_ ? modes_->snap_mask.load(std::memory_order_relaxed) : core::kAllSnaps;
    // The pick aperture is always sent (it drives the rollover hover-pick too);
    // the `osnap` flag gates only the snap-point computation.
    core::SetCursorCommand cmd{world, kApertonPx / scale, osnap, mask, {}, false};
    if (processor_ != nullptr) {
        if (const auto from = processor_->active_from()) {
            cmd.from = *from;
            cmd.has_from = true;
        }
    }
    engine_.submit(cmd);

    if (panning_) {
        const double dx = event->position().x() - last_x_;
        const double dy = event->position().y() - last_y_;
        last_x_ = event->position().x();
        last_y_ = event->position().y();
        std::scoped_lock lock(camera_mutex_);
        camera_.pan_pixels(core::Vec2{dx * dpr, dy * dpr});
    }
    if (selecting_) {
        sel_cur_world_ = world;
    }
    rebuild_overlay();
}

void ViewportWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        panning_ = false;
        return;
    }
    if (event->button() == Qt::LeftButton && selecting_) {
        selecting_ = false;
        const double dpr = devicePixelRatio();
        const core::Vec2 rel_screen{event->position().x() * dpr, event->position().y() * dpr};
        double scale = 1.0;
        core::Vec2 world;
        {
            std::scoped_lock lock(camera_mutex_);
            world = camera_.screen_to_world(rel_screen);
            scale = camera_.scale();
        }
        const double drag_px = core::length(rel_screen - sel_start_screen_);
        if (drag_px < 4.0) {
            // Single-click pick.
            engine_.submit(core::SelectPickCommand{world, 10.0 / scale, sel_additive_});
        } else {
            // Window (left->right) vs crossing (right->left), per AutoCAD.
            const bool crossing = rel_screen.x < sel_start_screen_.x;
            const core::Vec2 mn{std::min(sel_start_world_.x, world.x),
                                std::min(sel_start_world_.y, world.y)};
            const core::Vec2 mx{std::max(sel_start_world_.x, world.x),
                                std::max(sel_start_world_.y, world.y)};
            engine_.submit(core::SelectWindowCommand{mn, mx, crossing, sel_additive_});
        }
        rebuild_overlay();
    }
}

namespace {
void tess_circle(core::Vec2 c, double r, std::vector<core::Vec2>& out) {
    constexpr int kN = 64;
    core::Vec2 prev{c.x + r, c.y};
    for (int i = 1; i <= kN; ++i) {
        const double a = (static_cast<double>(i) / kN) * core::kTwoPi;
        const core::Vec2 cur{c.x + r * std::cos(a), c.y + r * std::sin(a)};
        out.push_back(prev);
        out.push_back(cur);
        prev = cur;
    }
}
} // namespace

void ViewportWindow::rebuild_overlay() {
    render::RenderOverlay ov;

    if (selecting_) {
        const double drag_px = std::abs(cursor_px_x_.load(std::memory_order_relaxed) -
                                        sel_start_screen_.x);
        ov.rect_mode = (cursor_px_x_.load(std::memory_order_relaxed) < sel_start_screen_.x) ? 2 : 1;
        ov.rect_a = sel_start_world_;
        ov.rect_b = sel_cur_world_;
        if (drag_px < 1.0) {
            ov.rect_mode = 0; // not yet a meaningful box
        }
    }

    if (processor_ != nullptr && processor_->preview().kind != command::PreviewKind::None) {
        core::Vec2 raw;
        {
            std::scoped_lock lock(camera_mutex_);
            raw = camera_.screen_to_world(core::Vec2{cursor_px_x_.load(std::memory_order_relaxed),
                                                     cursor_px_y_.load(std::memory_order_relaxed)});
        }
        std::optional<core::Vec2> snap;
        if (snap_has_.load(std::memory_order_relaxed)) {
            snap = core::Vec2{snap_x_.load(std::memory_order_relaxed),
                              snap_y_.load(std::memory_order_relaxed)};
        }
        const core::Vec2 cur = processor_->resolve_pick(raw, snap);
        const command::PreviewSpec& pv = processor_->preview();
        const auto& pts = pv.points;
        auto& seg = ov.preview_segments;
        switch (pv.kind) {
        case command::PreviewKind::Segment:
            if (!pts.empty()) {
                seg.push_back(pts[0]);
                seg.push_back(cur);
            }
            break;
        case command::PreviewKind::Polyline:
            for (std::size_t i = 1; i < pts.size(); ++i) {
                seg.push_back(pts[i - 1]);
                seg.push_back(pts[i]);
            }
            if (!pts.empty()) {
                seg.push_back(pts.back());
                seg.push_back(cur);
            }
            break;
        case command::PreviewKind::Rectangle:
            if (!pts.empty()) {
                const core::Vec2 a = pts[0];
                const core::Vec2 b = cur;
                seg.push_back({a.x, a.y}); seg.push_back({b.x, a.y});
                seg.push_back({b.x, a.y}); seg.push_back({b.x, b.y});
                seg.push_back({b.x, b.y}); seg.push_back({a.x, b.y});
                seg.push_back({a.x, b.y}); seg.push_back({a.x, a.y});
            }
            break;
        case command::PreviewKind::Circle:
            if (!pts.empty()) {
                tess_circle(pts[0], core::distance(pts[0], cur), seg);
            }
            break;
        case command::PreviewKind::Arc:
            if (pts.size() == 1) {
                seg.push_back(pts[0]);
                seg.push_back(cur);
            } else if (pts.size() == 2) {
                seg.push_back(pts[0]);
                seg.push_back(pts[1]);
                seg.push_back(pts[1]);
                seg.push_back(cur);
            }
            break;
        case command::PreviewKind::Move:
        case command::PreviewKind::Mirror:
            if (!pts.empty()) {
                ov.ghost_mode = (pv.kind == command::PreviewKind::Move) ? 1 : 2;
                ov.ghost_a = pts[0];
                ov.ghost_b = cur;
                seg.push_back(pts[0]);
                seg.push_back(cur);
            }
            break;
        case command::PreviewKind::Rotate:
            if (!pts.empty()) {
                ov.ghost_mode = 3;
                ov.ghost_a = pts[0];
                ov.ghost_param = std::atan2(cur.y - pts[0].y, cur.x - pts[0].x);
                seg.push_back(pts[0]);
                seg.push_back(cur);
            }
            break;
        case command::PreviewKind::Scale:
            if (!pts.empty()) {
                ov.ghost_mode = 4;
                ov.ghost_a = pts[0];
                ov.ghost_param = core::distance(pts[0], cur); // reference length 1
                seg.push_back(pts[0]);
                seg.push_back(cur);
            }
            break;
        case command::PreviewKind::None:
            break;
        }
    }

    std::scoped_lock lock(overlay_mutex_);
    overlay_ = std::move(ov);
}

void ViewportWindow::keyPressEvent(QKeyEvent* event) {
    if (processor_ == nullptr) {
        QWindow::keyPressEvent(event);
        return;
    }
    // Delete/Backspace are handled by the application-wide event filter in
    // MainWindow (so they work regardless of which window holds focus, while
    // still leaving text-entry keys to the command-line field).
    if (event->key() == Qt::Key_Escape) {
        // Cancel the active command, or clear the selection when idle.
        processor_->cancel();
        return;
    }
    QWindow::keyPressEvent(event);
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
