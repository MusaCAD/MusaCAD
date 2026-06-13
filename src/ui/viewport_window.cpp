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
#include "musacad/core/dimension.hpp"
#include "musacad/core/text/stroke_font.hpp"

#include <QExposeEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QResizeEvent>
#include <QScreen>
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

void ViewportWindow::open_properties() {
    // The processor runs on the GUI thread, so forwarding directly is safe.
    if (properties_toggle_callback_) {
        properties_toggle_callback_();
    }
}

void ViewportWindow::import_dwg() {
    if (dwg_import_callback_) {
        dwg_import_callback_();
    }
}

void ViewportWindow::export_dwg() {
    if (dwg_export_callback_) {
        dwg_export_callback_();
    }
}

void ViewportWindow::plot_dialog() {
    if (plot_dialog_callback_) {
        plot_dialog_callback_();
    }
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
    double last_world_per_px = 0.0; // last view scale reported to the geometry thread

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
        // Zoom-adaptive tessellation (Part A): tell the geometry thread the view
        // scale ONLY when it actually changes (zoom/resize, never pan), so curves
        // re-tessellate to the current zoom. The engine buckets it, so tiny changes
        // are no-ops there too.
        const double wpp = cam.scale() > 0.0 ? 1.0 / cam.scale() : 1.0;
        if (std::abs(wpp - last_world_per_px) > last_world_per_px * 1e-3) {
            last_world_per_px = wpp;
            engine_.submit(core::SetViewScaleCommand{wpp});
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
        hovered_kind_.store(snap.has_hover ? static_cast<int>(snap.hover.kind) + 1 : 0,
                            std::memory_order_relaxed);
        dirty_.store(snap.dirty, std::memory_order_relaxed);
        document_version_.store(snap.document_version, std::memory_order_relaxed);
        current_layer_.store(snap.current_layer, std::memory_order_relaxed);
        {
            // Cache resolved object-dimension def points + Standard style for the
            // GUI-thread placement preview (read in rebuild_overlay).
            std::scoped_lock lock(pending_dim_mutex_);
            pending_dim_valid_ = snap.has_pending_dim;
            pdim_a_ = snap.pending_dim_a;
            pdim_b_ = snap.pending_dim_b;
            pdim_line_pt_ = snap.pending_dim_line_pt;
            pdim_type_ = snap.pending_dim_type;
            pdim_style_ = snap.dimstyles.empty() ? core::DimStyle{} : snap.dimstyles[0];
        }
        {
            // Cache grips for GUI-thread hit-testing (grab on press).
            std::scoped_lock lock(grips_mutex_);
            grips_cache_ = snap.grips;
            text_targets_ = snap.text_edit_targets; // double-click-to-edit hit-test
            selection_summary_ = snap.selection_summary; // PR palette
            bounds_min_ = snap.bounds_min;
            bounds_max_ = snap.bounds_max;
            has_bounds_ = snap.has_bounds;
        }
        {
            std::scoped_lock lock(layers_mutex_);
            layers_ = snap.layers;
        }

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
        // AutoCAD-accurate, HiDPI-correct lineweight. physicalDotsPerInch is a
        // *logical* pixel density (Qt derives it from device-independent geometry);
        // the framebuffer is in PHYSICAL pixels, so the renderer multiplies this by
        // the device-pixel-ratio. Both are pushed every frame, so dragging the
        // window between a normal monitor and a HiDPI laptop self-corrects.
        if (const QScreen* scr = screen(); scr != nullptr) {
            renderer.set_device_pixels_per_mm(
                static_cast<float>(scr->physicalDotsPerInch() / 25.4));
        }
        renderer.set_device_pixel_ratio(static_cast<float>(devicePixelRatio()));
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
        if (plot_picking_) {
            // Picking a plot window: begin a plain rubber-band drag, ignoring grips/commands.
            selecting_ = true;
            sel_additive_ = false;
            sel_start_screen_ = screen_px;
            sel_start_world_ = world;
            sel_cur_world_ = world;
        } else if (processor_->has_active_command()) {
            std::optional<core::Vec2> snap;
            if (snap_has_.load(std::memory_order_relaxed)) {
                snap = core::Vec2{snap_x_.load(std::memory_order_relaxed),
                                  snap_y_.load(std::memory_order_relaxed)};
            }
            processor_->set_pick_radius(10.0 * dpr / scale);
            processor_->pick_point(world, snap);
            rebuild_overlay();
        } else if (const int gi = grip_at(world, 10.0 * dpr / scale); gi >= 0) {
            // Idle press on a grip of a selected entity: begin a direct-manipulation
            // drag. ORTHO/POLAR resolve relative to the grip's origin.
            core::GripInfo ginfo;
            {
                std::scoped_lock lock(grips_mutex_);
                ginfo = grips_cache_[static_cast<std::size_t>(gi)];
            }
            dragging_grip_ = true;
            grip_origin_ = ginfo.pos;
            processor_->set_pick_radius(10.0 * dpr / scale);
            processor_->set_last_point(ginfo.pos);
            engine_.submit(core::GripDragCommand{core::GripDragCommand::Phase::Begin, ginfo.handle,
                                                 ginfo.index, {}, 0});
        } else {
            // Idle: begin a selection drag (single click or window/crossing box).
            selecting_ = true;
            sel_additive_ = (event->modifiers() & Qt::ShiftModifier) != 0;
            sel_start_screen_ = screen_px;
            sel_start_world_ = world;
            sel_cur_world_ = world;
        }
    }
    if (event->button() == Qt::LeftButton) {
        Q_EMIT pickerInteracted(); // host re-acquires DYN focus after a viewport pick
    }
}

void ViewportWindow::mouseDoubleClickEvent(QMouseEvent* event) {
    // Double-click a text-bearing entity -> request the in-window editor. Idle only
    // (an active command keeps its own click semantics). Hit-test the cached text
    // targets (already gated to editable/unlocked layers geometry-side).
    if (event->button() != Qt::LeftButton || processor_ == nullptr ||
        processor_->has_active_command() || !text_edit_callback_) {
        return;
    }
    const double dpr = devicePixelRatio();
    const core::Vec2 screen_px{event->position().x() * dpr, event->position().y() * dpr};
    core::Vec2 world;
    double scale = 1.0;
    {
        std::scoped_lock lock(camera_mutex_);
        world = camera_.screen_to_world(screen_px);
        scale = camera_.scale();
    }
    const double pad = 10.0 * dpr / scale; // pick aperture in world units (DPR-aware)
    bool found = false;
    std::string content;
    bool multiline = false;
    {
        std::scoped_lock lock(grips_mutex_);
        double best = 0.0;
        for (const core::TextEditTarget& t : text_targets_) {
            if (world.x < t.min.x - pad || world.x > t.max.x + pad || world.y < t.min.y - pad ||
                world.y > t.max.y + pad) {
                continue;
            }
            const core::Vec2 c{(t.min.x + t.max.x) * 0.5, (t.min.y + t.max.y) * 0.5};
            const double d2 = core::length_squared(world - c);
            if (!found || d2 < best) {
                found = true;
                best = d2;
                content = t.content;
                multiline = t.multiline;
            }
        }
    }
    // Open the editor OUTSIDE the lock (it runs a modal dialog).
    if (found) {
        text_edit_callback_(TextEditRequest{world, pad, content, multiline});
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
    Q_EMIT cursorScreenMoved(event->position().x(), event->position().y());

    // Push the cursor to the geometry thread so it can compute the snap candidate
    // and publish it via the snapshot. Coalesced and non-blocking -- no per-move
    // synchronous query.
    const bool osnap = modes_ == nullptr || modes_->osnap.load(std::memory_order_relaxed);
    constexpr double kApertonPx = 10.0;
    const std::uint32_t mask =
        modes_ ? modes_->snap_mask.load(std::memory_order_relaxed) : core::kAllSnaps;
    // The pick aperture is always sent (it drives the rollover hover-pick too);
    // the `osnap` flag gates only the snap-point computation.
    core::SetCursorCommand cmd{world, kApertonPx * dpr / scale, osnap, mask, {}, false};
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
    if (dragging_grip_ && processor_ != nullptr) {
        // Resolve the dragged point (OSNAP wins, else ORTHO/POLAR/grid vs the grip
        // origin) and push it to the geometry thread for the transient preview.
        std::optional<core::Vec2> snap;
        if (snap_has_.load(std::memory_order_relaxed)) {
            snap = core::Vec2{snap_x_.load(std::memory_order_relaxed),
                              snap_y_.load(std::memory_order_relaxed)};
        }
        const core::Vec2 target = processor_->resolve_pick(world, snap);
        engine_.submit(
            core::GripDragCommand{core::GripDragCommand::Phase::Move, {}, 0, target, 0});
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
    if (event->button() == Qt::LeftButton && dragging_grip_ && processor_ != nullptr) {
        dragging_grip_ = false;
        const double dpr = devicePixelRatio();
        const core::Vec2 rel_screen{event->position().x() * dpr, event->position().y() * dpr};
        core::Vec2 world;
        {
            std::scoped_lock lock(camera_mutex_);
            world = camera_.screen_to_world(rel_screen);
        }
        std::optional<core::Vec2> snap;
        if (snap_has_.load(std::memory_order_relaxed)) {
            snap = core::Vec2{snap_x_.load(std::memory_order_relaxed),
                              snap_y_.load(std::memory_order_relaxed)};
        }
        const core::Vec2 target = processor_->resolve_pick(world, snap);
        const std::uint64_t group = processor_->begin_group();
        engine_.submit(
            core::GripDragCommand{core::GripDragCommand::Phase::Commit, {}, 0, target, group});
        processor_->clear_last_point();
        rebuild_overlay();
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
        if (plot_picking_) {
            // Deliver the picked plot window (two world corners) and disarm. A real drag
            // delivers ok=true; a click-without-drag delivers ok=false (cancel). Either
            // way the callback fires exactly once so the host re-opens the dialog.
            plot_picking_ = false;
            const double drag = core::length(rel_screen - sel_start_screen_);
            if (auto cb = std::move(plot_pick_callback_)) {
                plot_pick_callback_ = nullptr;
                if (drag >= 4.0 * dpr) {
                    const core::Vec2 mn{std::min(sel_start_world_.x, world.x),
                                        std::min(sel_start_world_.y, world.y)};
                    const core::Vec2 mx{std::max(sel_start_world_.x, world.x),
                                        std::max(sel_start_world_.y, world.y)};
                    cb(true, mn, mx);
                } else {
                    cb(false, {}, {});
                }
            }
            rebuild_overlay();
            return;
        }
        const double drag_px = core::length(rel_screen - sel_start_screen_);
        if (drag_px < 4.0 * dpr) {
            // Single-click pick.
            engine_.submit(core::SelectPickCommand{world, 10.0 * dpr / scale, sel_additive_});
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
        Q_EMIT pickerInteracted();
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

// Flatten a dimension's computed geometry into preview line segments (ext + dim +
// arrow outlines + the live label as stroke text), for the placement rubber-band.
void dim_preview_segments(const core::DimData& d, const core::DimStyle& style,
                          std::vector<core::Vec2>& seg) {
    const core::DimGeometry g = core::compute_dim_geometry(d, style, core::Rgb{});
    const auto add = [&](const std::vector<core::Vec2>& v) {
        seg.insert(seg.end(), v.begin(), v.end());
    };
    add(g.ext_lines);
    add(g.dim_lines);
    add(g.arrow_lines);
    // Filled arrowheads -> draw their triangle outlines as preview lines.
    for (std::size_t i = 0; i + 2 < g.arrow_fills.size(); i += 3) {
        seg.push_back(g.arrow_fills[i]);
        seg.push_back(g.arrow_fills[i + 1]);
        seg.push_back(g.arrow_fills[i + 1]);
        seg.push_back(g.arrow_fills[i + 2]);
        seg.push_back(g.arrow_fills[i + 2]);
        seg.push_back(g.arrow_fills[i]);
    }
    core::text::append_text_segments(g.label, g.text_pos, g.text_height, g.text_rotation,
                                     g.text_justify, seg);
}
} // namespace

int ViewportWindow::grip_at(core::Vec2 world, double radius_world) const {
    std::scoped_lock lock(grips_mutex_);
    int best = -1;
    double best_d2 = radius_world * radius_world;
    for (std::size_t i = 0; i < grips_cache_.size(); ++i) {
        const double d2 = core::length_squared(grips_cache_[i].pos - world);
        if (d2 <= best_d2) {
            best_d2 = d2;
            best = static_cast<int>(i);
        }
    }
    return best;
}

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
        Q_EMIT constrainedCursorMoved(cur.x, cur.y); // DYN live values read this
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
        case command::PreviewKind::Dimension: {
            // Rubber-band the full dimension at the cursor placement (Part C). Def
            // points come from pts (two-point dims) or the resolved pending_dim
            // (object dims). Pure render-side: no store mutation, no op-log entry.
            core::DimData d;
            d.type = static_cast<core::DimType>(pv.dim_type);
            d.style = pv.dim_style;
            core::DimStyle style;
            bool ok = false;
            if (pts.size() >= 2) {
                d.a = pts[0];
                d.b = pts[1];
                d.line_pt = cur; // placement follows the cursor
                ok = true;
            } else {
                std::scoped_lock lock(pending_dim_mutex_);
                if (pending_dim_valid_ && static_cast<int>(pdim_type_) == pv.dim_type) {
                    style = pdim_style_;
                    const auto t = static_cast<core::DimType>(pdim_type_);
                    if (t == core::DimType::Radius || t == core::DimType::Diameter) {
                        // Radius line follows the cursor: edge = centre + R*dir(cursor).
                        const double r = core::distance(pdim_a_, pdim_b_);
                        core::Vec2 dir = cur - pdim_a_;
                        dir = core::length_squared(dir) > 1e-12 ? core::normalized(dir)
                                                               : core::Vec2{1.0, 0.0};
                        d.a = pdim_a_;
                        d.b = pdim_a_ + dir * r;
                        d.line_pt = cur;
                    } else if (t == core::DimType::Angular) {
                        d.a = pdim_a_; // geometry fixed by the two lines
                        d.b = pdim_b_;
                        d.line_pt = pdim_line_pt_;
                    } else { // Linear / Aligned: endpoints fixed, placement follows cursor
                        d.a = pdim_a_;
                        d.b = pdim_b_;
                        d.line_pt = cur;
                    }
                    ok = true;
                }
            }
            if (ok) {
                dim_preview_segments(d, style, seg);
            }
            break;
        }
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
        if (plot_picking_) {
            cancel_plot_window_pick(); // re-opens the plot dialog (ok=false)
            return;
        }
        handle_escape();
        return;
    }
    QWindow::keyPressEvent(event);
}

void ViewportWindow::handle_escape() {
    if (processor_ == nullptr) {
        return;
    }
    if (dragging_grip_) {
        // Cancel the grip drag: the entity is left unchanged (no commit).
        dragging_grip_ = false;
        engine_.submit(core::GripDragCommand{core::GripDragCommand::Phase::Cancel, {}, 0, {}, 0});
        processor_->clear_last_point();
        rebuild_overlay();
        return;
    }
    // Cancel the active command, or clear the selection when idle.
    processor_->cancel();
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
