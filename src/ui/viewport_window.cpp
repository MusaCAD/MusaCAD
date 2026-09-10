// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/ui/viewport_window.hpp"

#include <vector>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

#include "musacad/command/command_processor.hpp"
#include "musacad/command/dyn_fields.hpp"
#include "musacad/core/font_engine.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/dimension.hpp"
#include "musacad/core/ellipse.hpp"
#include "musacad/core/polygon.hpp"
#include "musacad/core/spline_eval.hpp"
#include "musacad/core/text/stroke_font.hpp"

#include <QCursor>
#include <QExposeEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPolygon>
#include <QOpenGLContext>
#include <QResizeEvent>
#include <QScreen>
#include <QImage>
#include <QOpenGLFunctions>
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

// Parse a Dynamic-Input field buffer to a number (nullopt if empty/invalid).
std::optional<double> dyn_parse(const std::string& s) {
    if (s.empty() || s == "-" || s == ".") {
        return std::nullopt;
    }
    try {
        std::size_t used = 0;
        const double v = std::stod(s, &used);
        return used == s.size() ? std::optional<double>(v) : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

// Format a live value for an on-canvas field: up to 4 decimals, no trailing zeros
// or exponent (the stroke font only has digits / '.' / '-').
std::string dyn_format(double v) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.4f", v);
    std::string s(buf);
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0') {
            s.pop_back();
        }
        if (s.back() == '.') {
            s.pop_back();
        }
    }
    return s;
}

// Screen-space box helpers for the on-canvas command surfaces (filled quad / outline).
void ui_quad(std::vector<core::Vec2>& v, double x, double y, double w, double h) {
    v.push_back({x, y});     v.push_back({x + w, y});     v.push_back({x + w, y + h});
    v.push_back({x, y});     v.push_back({x + w, y + h}); v.push_back({x, y + h});
}
void ui_outline(std::vector<core::Vec2>& v, double x, double y, double w, double h) {
    v.push_back({x, y});        v.push_back({x + w, y});
    v.push_back({x + w, y});    v.push_back({x + w, y + h});
    v.push_back({x + w, y + h});v.push_back({x, y + h});
    v.push_back({x, y + h});    v.push_back({x, y});
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

void ViewportWindow::request_frame_capture(std::string png_path) {
    {
        std::scoped_lock lock(capture_mutex_);
        capture_path_ = std::move(png_path);
    }
    capture_requested_.store(true, std::memory_order_release);
    dirty_.store(true, std::memory_order_release); // make sure a frame is rendered
}

bool ViewportWindow::current_view(core::Vec2& center, double& scale) {
    std::scoped_lock lock(camera_mutex_);
    center = camera_.center();
    scale = camera_.scale();
    return scale > 0.0;
}

void ViewportWindow::set_view(core::Vec2 center, double scale) {
    {
        std::scoped_lock lock(camera_mutex_);
        pending_view_center_ = center;
        pending_view_scale_ = scale;
    }
    view_requested_.store(true, std::memory_order_relaxed); // applied on the render thread
}

bool ViewportWindow::viewport_size(int& w, int& h) const {
    w = width();
    h = height();
    return w > 0 && h > 0;
}

std::vector<core::NamedView> ViewportWindow::named_views() {
    std::scoped_lock lock(layers_mutex_);
    return named_views_;
}

core::DrawingUnits ViewportWindow::units() {
    std::scoped_lock lock(layers_mutex_);
    return units_;
}

std::vector<core::TextStyle> ViewportWindow::text_styles() {
    std::scoped_lock lock(layers_mutex_);
    return text_styles_;
}

std::uint16_t ViewportWindow::current_text_style() {
    std::scoped_lock lock(layers_mutex_);
    return current_text_style_;
}

std::vector<std::string> ViewportWindow::block_names() {
    std::scoped_lock lock(layers_mutex_);
    return block_names_;
}

std::vector<std::vector<core::BlockAttDefInfo>> ViewportWindow::block_attdefs() {
    std::scoped_lock lock(layers_mutex_);
    return block_attdefs_;
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

core::MatchPropFilter ViewportWindow::match_filter() const {
    return match_filter_callback_ ? match_filter_callback_() : core::MatchPropFilter{};
}

void ViewportWindow::match_settings_dialog() {
    if (match_settings_callback_) {
        match_settings_callback_(); // MainWindow owns the modal dialog (GUI thread)
    }
}

std::uint32_t ViewportWindow::snap_mask() const {
    return modes_ ? modes_->snap_mask.load(std::memory_order_relaxed) : core::kAllSnaps;
}

void ViewportWindow::set_snap_mask(std::uint32_t mask) {
    if (modes_) {
        modes_->snap_mask.store(mask, std::memory_order_relaxed);
    }
}

void ViewportWindow::osnap_settings_dialog() {
    if (osnap_settings_callback_) {
        osnap_settings_callback_(); // MainWindow owns the dialog (GUI thread)
    }
}

void ViewportWindow::set_dialog_ghost(int mode, core::Vec2 a, double param) {
    dialog_ghost_mode_ = mode;
    dialog_ghost_a_ = a;
    dialog_ghost_param_ = param;
    rebuild_overlay();
}

bool ViewportWindow::selection_bounds(core::Vec2& mn, core::Vec2& mx) {
    std::scoped_lock lock(grips_mutex_);
    mn = sel_bounds_min_;
    mx = sel_bounds_max_;
    return has_sel_bounds_;
}

void ViewportWindow::set_match_cursor(bool on) {
    if (on) {
        // A simple drawn paintbrush (Qt has no built-in one): a wooden handle, a metal
        // ferrule, and a blue paint tip; hotspot at the tip so it marks the pick point.
        // The AutoCAD "match mode" signal. Built once.
        static const QCursor brush = [] {
            QPixmap pm(32, 32);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(QColor(170, 120, 60), 3)); // handle
            p.drawLine(29, 29, 12, 12);
            p.setPen(QPen(QColor(185, 185, 195), 4)); // ferrule
            p.drawLine(12, 12, 8, 8);
            p.setPen(Qt::NoPen); // paint tip
            p.setBrush(QColor(60, 140, 230));
            QPolygon tip;
            tip << QPoint(9, 9) << QPoint(1, 7) << QPoint(7, 1);
            p.drawPolygon(tip);
            p.end();
            return QCursor(pm, 3, 3);
        }();
        setCursor(brush);
    } else {
        unsetCursor();
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

    // Say once what is actually drawing: the answer to "is the GPU being used?".
    if (QOpenGLFunctions* f = context.functions(); f != nullptr) {
        const GLubyte* r = f->glGetString(GL_RENDERER);
        const GLubyte* v = f->glGetString(GL_VERSION);
        std::fprintf(stderr, "[musacad_ui] GL renderer: %s | %s\n",
                     r != nullptr ? reinterpret_cast<const char*>(r) : "?",
                     v != nullptr ? reinterpret_cast<const char*>(v) : "?");
    }

    const bool smoke = std::getenv("MUSACAD_SMOKE") != nullptr;
    int smoke_frames = 0;
    auto last = std::chrono::steady_clock::now();
    double last_world_per_px = 0.0; // last view scale reported to the geometry thread

    // Frame pacing: late latching. With vsync, swapBuffers blocks until the display's
    // refresh. A frame rendered the moment the previous swap returns therefore carries
    // input that is a whole refresh old by the time it is scanned out -- the "few
    // milliseconds behind the pointer" feel. Rendering takes about a millisecond, so
    // instead this loop sleeps until just before the next refresh, THEN samples the
    // cursor, overlay and snapshot and renders. The wait is derived from the measured
    // refresh interval and a decaying maximum of the measured render time, and is
    // only applied while vsync is demonstrably pacing the loop (the swap or the sleep
    // takes most of each interval), so on an uncapped or offscreen context nothing
    // is delayed. MUSACAD_NO_PACING=1 disables it for A/B measurement.
    using clock = std::chrono::steady_clock;
    const auto to_ms = [](auto d) { return std::chrono::duration<double, std::milli>(d).count(); };
    const bool pacing_enabled = std::getenv("MUSACAD_NO_PACING") == nullptr;
    const bool timing = std::getenv("MUSACAD_TIMING") != nullptr;
    QOpenGLFunctions* const gf = context.functions();
    double period_ms = 0.0;     // EMA of refresh-anchor intervals (the refresh period)
    double render_max_ms = 0.0; // decaying max of sample-to-swap time
    double slept_ms = 0.0;
    double touch_ms = 0.0;
    bool paced = false;
    auto refresh_anchor = clock::now(); // the moment the display throttle last released
    // Latency probe (MUSACAD_TIMING): mouse event -> swap returned for the frame that
    // rendered that cursor position.
    std::int64_t last_stamp = 0;
    double lat_sum = 0.0;
    double lat_max = 0.0;
    int lat_n = 0;

    while (!token.stop_requested()) {
        if (pacing_enabled && paced) {
            // Safety margin before the refresh: the recent worst render time with
            // headroom, never under 2 ms (sleep_until jitter) nor over half a period. A
            // missed refresh costs a whole period of latency, so the margin errs wide.
            const double budget = std::clamp(render_max_ms * 1.5 + 1.0, 2.0, period_ms * 0.5);
            const auto wake = refresh_anchor +
                              std::chrono::duration_cast<clock::duration>(
                                  std::chrono::duration<double, std::milli>(period_ms - budget));
            const auto t0 = clock::now();
            if (wake > t0) {
                std::this_thread::sleep_until(wake);
            }
            slept_ms = to_ms(clock::now() - t0);
        } else {
            slept_ms = 0.0;
        }
        const auto t_sample = clock::now();
        const std::int64_t stamp = cursor_stamp_ns_.load(std::memory_order_relaxed);
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
        // Per-document camera: each tab remembers its own zoom/pan. When the active
        // document changes (the snapshot now reports a new id), save the outgoing tab's
        // view and restore the incoming tab's -- or frame a never-seen document's extents.
        if (snap.active_document_id != 0 && snap.active_document_id != cam_doc_id_) {
            std::scoped_lock lock(camera_mutex_);
            if (cam_doc_id_ != 0) {
                doc_cameras_[cam_doc_id_] = camera_; // remember the outgoing tab's view
            }
            const auto it = doc_cameras_.find(snap.active_document_id);
            if (it != doc_cameras_.end()) {
                camera_ = it->second; // restore the tab's exact view
                camera_.set_viewport(w, h);
            } else {
                camera_ = render::Camera2D{}; // first time: default, then frame content below
                camera_.set_viewport(w, h);
                camera_.frame_bounds(init_min_, init_max_, 0.1);
                zoom_extents_requested_.store(true, std::memory_order_relaxed);
            }
            cam = camera_;
            cam_doc_id_ = snap.active_document_id;
        }
        if (zoom_extents_requested_.exchange(false, std::memory_order_relaxed) && snap.has_bounds) {
            std::scoped_lock lock(camera_mutex_);
            camera_.frame_bounds(snap.bounds_min, snap.bounds_max, 0.1);
            cam = camera_;
        }
        if (view_requested_.exchange(false, std::memory_order_relaxed)) {
            std::scoped_lock lock(camera_mutex_);
            camera_.set_center(pending_view_center_);
            if (pending_view_scale_ > 0.0) {
                camera_.set_scale(pending_view_scale_);
            }
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
        grip_preview_count_.store(static_cast<int>(snap.grip_preview_segments.size()),
                                  std::memory_order_relaxed);
        line_vertex_count_.store(static_cast<int>(snap.line_vertices.size()),
                                 std::memory_order_relaxed);
        hovered_kind_.store(snap.has_hover ? static_cast<int>(snap.hover.kind) + 1 : 0,
                            std::memory_order_relaxed);
        dirty_.store(snap.dirty, std::memory_order_relaxed);
        document_version_.store(snap.document_version, std::memory_order_relaxed);
        current_layer_.store(snap.current_layer, std::memory_order_relaxed);
        {
            // Multi-document tab list, for the GUI thread to render the tab strip + title.
            std::scoped_lock lock(docs_mutex_);
            docs_cache_ = snap.documents;
            active_doc_id_cache_ = snap.active_document_id;
        }
        {
            // Cache resolved object-dimension def points + Standard style for the
            // GUI-thread placement preview (read in rebuild_overlay).
            std::scoped_lock lock(pending_dim_mutex_);
            pending_dim_valid_ = snap.has_pending_dim;
            pdim_a_ = snap.pending_dim_a;
            pdim_b_ = snap.pending_dim_b;
            pdim_line_pt_ = snap.pending_dim_line_pt;
            pdim_type_ = snap.pending_dim_type;
            pdim_aux_ = snap.pending_dim_aux;
            pdim_style_ = snap.dimstyles.empty() ? core::DimStyle{} : snap.dimstyles[0];
        }
        {
            // Cache grips for GUI-thread hit-testing (grab on press).
            std::scoped_lock lock(grips_mutex_);
            grips_cache_ = snap.grips;
            text_targets_ = snap.text_edit_targets; // double-click-to-edit hit-test
            selection_summary_ = snap.selection_summary; // PR palette
            has_sel_bounds_ = !snap.selected_line_vertices.empty();
            if (has_sel_bounds_) {
                sel_bounds_min_ = sel_bounds_max_ = snap.selected_line_vertices.front();
                for (const core::Vec2& p : snap.selected_line_vertices) {
                    sel_bounds_min_ = {std::min(sel_bounds_min_.x, p.x), std::min(sel_bounds_min_.y, p.y)};
                    sel_bounds_max_ = {std::max(sel_bounds_max_.x, p.x), std::max(sel_bounds_max_.y, p.y)};
                }
            }
            bounds_min_ = snap.bounds_min;
            bounds_max_ = snap.bounds_max;
            has_bounds_ = snap.has_bounds;
        }
        {
            std::scoped_lock lock(layers_mutex_);
            layers_ = snap.layers;
            named_views_ = snap.named_views;
            units_ = snap.units;
            text_styles_ = snap.text_styles;
            current_text_style_ = snap.current_text_style;
            block_names_ = snap.block_names;
            block_attdefs_ = snap.block_attdefs;
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
            std::shared_ptr<const render::RenderOverlay> ov;
            {
                std::scoped_lock lock(overlay_mutex_);
                ov = overlay_; // a reference, not a copy of the overlay's vertices
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
        if (capture_requested_.exchange(false, std::memory_order_acq_rel)) {
            std::string path;
            {
                std::scoped_lock lock(capture_mutex_);
                path = capture_path_;
            }
            const int cw = target->width();
            const int ch = target->height();
            bool ok = false;
            if (gf != nullptr && cw > 0 && ch > 0) {
                std::vector<unsigned char> px(static_cast<std::size_t>(cw) * static_cast<std::size_t>(ch) * 4);
                gf->glReadPixels(0, 0, cw, ch, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                QImage img(cw, ch, QImage::Format_RGBA8888);
                const std::size_t row = static_cast<std::size_t>(cw) * 4;
                for (int y = 0; y < ch; ++y) { // GL rows run bottom-up
                    std::memcpy(img.scanLine(y), px.data() + static_cast<std::size_t>(ch - 1 - y) * row, row);
                }
                ok = img.save(QString::fromStdString(path), "PNG");
            }
            std::printf("[viewport_capture] %s -> %s\n", path.c_str(), ok ? "saved" : "FAILED");
            std::fflush(stdout);
        }
        const auto t_before_swap = clock::now();
        context.swapBuffers(this);
        const auto t_after_swap = clock::now();
        // Where the display throttle actually sits differs by driver. NVIDIA blocks in
        // swapBuffers; Mesa returns at once and blocks on the NEXT frame's first
        // back-buffer write instead -- which, left alone, lands AFTER the cursor was
        // sampled and silently adds a whole refresh to every frame's input age. So the
        // back buffer is acquired here, deliberately, with a one-pixel clear: whichever
        // call blocks, the loop is now pinned to the refresh before it samples input.
        if (gf != nullptr) {
            gf->glEnable(GL_SCISSOR_TEST);
            gf->glScissor(0, 0, 1, 1);
            gf->glClear(GL_COLOR_BUFFER_BIT);
            gf->glDisable(GL_SCISSOR_TEST);
        }
        const auto now = clock::now();

        // Pacing statistics from this frame.
        const double render_ms = to_ms(t_before_swap - t_sample);
        const double swap_ms = to_ms(t_after_swap - t_before_swap);
        touch_ms = to_ms(now - t_after_swap);
        const double interval_ms = to_ms(now - refresh_anchor);
        refresh_anchor = now;
        render_max_ms = std::max(render_ms, render_max_ms * 0.9);
        // A paced frame whose acquire did not wait at all arrived AFTER the refresh: it
        // slipped a period. Widen the margin so the next frames make it comfortably.
        if (paced && slept_ms > 0.0 && touch_ms < 0.2) {
            render_max_ms += 1.0;
        }
        period_ms = period_ms <= 0.0 ? interval_ms : period_ms * 0.9 + interval_ms * 0.1;
        // vsync is pacing us when the frame's idle time (blocking swap, the throttled
        // acquire, and our own sleep) fills most of a plausible refresh interval.
        paced = period_ms >= 6.0 && period_ms <= 40.0 &&
                (swap_ms + touch_ms + slept_ms) >= interval_ms * 0.5;
        if (timing && stamp != 0 && stamp != last_stamp) {
            last_stamp = stamp;
            const double lat = to_ms(now.time_since_epoch()) - static_cast<double>(stamp) / 1e6;
            lat_sum += lat;
            lat_max = std::max(lat_max, lat);
            if (++lat_n == 20) {
                std::fprintf(stderr,
                             "[timing] input->present %.1f ms avg, %.1f max | refresh %.1f ms, "
                             "render %.2f, swap %.1f, acquire %.1f, slept %.1f, paced=%d\n",
                             lat_sum / lat_n, lat_max, period_ms, render_ms, swap_ms, touch_ms,
                             slept_ms, paced ? 1 : 0);
                lat_sum = lat_max = 0.0;
                lat_n = 0;
            }
        }

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
    if (event->button() == Qt::RightButton) {
        // AutoCAD: right-click is Enter while a command is running -- it ends "Select
        // objects:" and accepts a prompt's default. Idle right-click is left alone.
        if (processor_ != nullptr && processor_->has_active_command()) {
            sync_selection_to_processor();
            processor_->submit_line("");
            rebuild_overlay();
        }
        return;
    }
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
        } else if (processor_->in_selection_phase()) {
            // "Select objects:" -- the same drag the idle canvas uses (a pick, a window
            // left-to-right, a crossing window right-to-left), on the RAW cursor, but
            // always accumulating and announced as "N found". Never a tab-to-tab drop.
            selecting_ = true;
            sel_additive_ = true;
            sel_start_screen_ = screen_px;
            sel_start_world_ = world;
            sel_cur_world_ = world;
            had_selection_at_press_ = false;
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
            // Idle: begin a selection drag (single click or window/crossing box). If a
            // selection already exists, this drag may instead be a tab-to-tab transfer
            // (resolved on release if it ends over a document tab).
            selecting_ = true;
            sel_additive_ = (event->modifiers() & Qt::ShiftModifier) != 0;
            sel_start_screen_ = screen_px;
            sel_start_world_ = world;
            sel_cur_world_ = world;
            had_selection_at_press_ = selection_count_.load(std::memory_order_relaxed) > 0;
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
    cursor_stamp_ns_.store(std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count(),
                           std::memory_order_relaxed);

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
        // Tab-to-tab drag: a selection existed at press and the user dragged it onto a
        // different document's tab. The host maps the drop position to a tab and performs
        // the cross-document transfer (copy -> switch -> paste). Selection is still the
        // press-time set here (the window-select below has not run yet).
        if (had_selection_at_press_ && drag_px >= 4.0 * dpr && selection_drop_cb_ &&
            selection_drop_cb_(event->globalPosition().toPoint())) {
            had_selection_at_press_ = false;
            rebuild_overlay();
            return;
        }
        had_selection_at_press_ = false;
        // At a command's "Select objects:" prompt the engine echoes AutoCAD's "N found".
        const bool announce = processor_ != nullptr && processor_->in_selection_phase();
        if (drag_px < 4.0 * dpr) {
            // Single-click pick.
            engine_.submit(
                core::SelectPickCommand{world, 10.0 * dpr / scale, sel_additive_, announce});
        } else {
            // Window (left->right) vs crossing (right->left), per AutoCAD.
            const bool crossing = rel_screen.x < sel_start_screen_.x;
            const core::Vec2 mn{std::min(sel_start_world_.x, world.x),
                                std::min(sel_start_world_.y, world.y)};
            const core::Vec2 mx{std::max(sel_start_world_.x, world.x),
                                std::max(sel_start_world_.y, world.y)};
            engine_.submit(
                core::SelectWindowCommand{mn, mx, crossing, sel_additive_, announce});
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
    std::optional<core::Vec2> pending_stretch; // live STRETCH delta to stream, if any
    render::RenderOverlay ov;

    // Drop any typed Dynamic-Input value once the command/rubber-band ends, so a new
    // command never inherits stale field text.
    if (!dyn_capturing() && (!dyn_buf_[0].empty() || !dyn_buf_[1].empty() || dyn_active_slot_ != 0)) {
        dyn_reset();
    }

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
        dyn_cursor_ = cur; // for composing a typed value on Enter
        Q_EMIT constrainedCursorMoved(cur.x, cur.y); // DYN live values read this
        const command::PreviewSpec& pv = processor_->preview();
        const auto& pts = pv.points;
        // On-canvas Dynamic Input: a typed field pins that dimension -- the preview
        // draws to the locked point while the cursor drives the unlocked DOF.
        const bool dim = dyn_enabled_ && dyn_dimensional();
        std::optional<double> dyn_prim;
        std::optional<double> dyn_sec;
        if (dim) {
            dyn_prim = dyn_parse(dyn_buf_[0]);
            dyn_sec = dyn_parse(dyn_buf_[1]);
        }
        const core::Vec2 cur_eff = command::apply_dyn_lock(pv, cur, dyn_prim, dyn_sec);
        // Live STRETCH: stream the (ortho/snap/DYN-resolved) cursor delta to the geometry
        // thread, which previews the whole selection stretched by it. Only on change, so
        // a redraw that did not move the cursor does not resubmit the same preview.
        if (pv.live_stretch && !pts.empty()) {
            const core::Vec2 delta = cur_eff - pts[0];
            if (!stretch_preview_sent_ || core::length(delta - last_stretch_delta_) > 1e-12) {
                pending_stretch = delta; // submitted after the overlay hand-off below
                last_stretch_delta_ = delta;
                stretch_preview_sent_ = true;
            }
        } else {
            stretch_preview_sent_ = false;
        }
        auto& seg = ov.preview_segments;
        switch (pv.kind) {
        case command::PreviewKind::Segment:
            if (!pts.empty()) {
                seg.push_back(pts[0]);
                seg.push_back(cur_eff);
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
                core::Vec2 b = cur_eff;
                if (pv.fixed_w > 0.0 && pv.fixed_h > 0.0) {
                    // RECTANGLE Dimensions/Area: fixed size; the cursor's quadrant relative
                    // to `a` flips which way the rectangle extends (NE/NW/SE/SW).
                    const double sx = (cur.x >= a.x) ? 1.0 : -1.0;
                    const double sy = (cur.y >= a.y) ? 1.0 : -1.0;
                    b = {a.x + sx * pv.fixed_w, a.y + sy * pv.fixed_h};
                }
                core::Vec2 c[4] = {{a.x, a.y}, {b.x, a.y}, {b.x, b.y}, {a.x, b.y}};
                if (pv.rect_rotation != 0.0) { // RECTANGLE Rotation: spin about `a`
                    const double cs = std::cos(pv.rect_rotation);
                    const double sn = std::sin(pv.rect_rotation);
                    for (core::Vec2& q : c) {
                        const double dx = q.x - a.x;
                        const double dy = q.y - a.y;
                        q = {a.x + dx * cs - dy * sn, a.y + dx * sn + dy * cs};
                    }
                }
                seg.push_back(c[0]); seg.push_back(c[1]);
                seg.push_back(c[1]); seg.push_back(c[2]);
                seg.push_back(c[2]); seg.push_back(c[3]);
                seg.push_back(c[3]); seg.push_back(c[0]);
            }
            break;
        case command::PreviewKind::Circle:
            if (!pts.empty()) {
                tess_circle(pts[0], core::distance(pts[0], cur_eff), seg);
            }
            break;
        case command::PreviewKind::Spline:
            if (!pts.empty()) {
                std::vector<core::Vec2> through(pts.begin(), pts.end());
                through.push_back(cur_eff);
                std::vector<core::Vec2> ctrl;
                if (pv.spline_fit) {
                    ctrl = core::spline::fit_or_fallback(
                        through, pv.spline_degree,
                        static_cast<core::spline::FitParam>(pv.spline_knots));
                } else {
                    ctrl = std::move(through);
                }
                std::vector<core::Vec2> curve;
                core::spline::tessellate(ctrl, static_cast<std::uint32_t>(pv.spline_degree), curve);
                for (std::size_t i = 0; i + 1 < curve.size(); ++i) {
                    seg.push_back(curve[i]);
                    seg.push_back(curve[i + 1]);
                }
            }
            break;
        case command::PreviewKind::Ellipse:
            if (!pts.empty()) {
                core::EllipseData e;
                e.center = pts[0];
                e.major = pv.major;
                e.ratio = pv.ratio;
                bool draw = true;
                if (pv.ellipse_stage == 0) {
                    // The other half-axis follows the cursor; axes swap past the major.
                    const double a = core::length(pv.major);
                    const double d = core::distance(cur_eff, pts[0]);
                    if (a <= 1e-9 || d <= 1e-9) {
                        draw = false;
                    } else if (d <= a) {
                        e.ratio = d / a;
                    } else {
                        const core::Vec2 mh = pv.major * (1.0 / a);
                        e.major = core::Vec2{-mh.y, mh.x} * d;
                        e.ratio = a / d;
                    }
                } else if (pv.ellipse_stage == 2) {
                    e.start = pv.ellipse_start;
                    e.end = core::ellipse::param_of(e, cur);
                    if (std::abs(e.end - e.start) < 1e-9) {
                        e.end = e.start + core::kTwoPi;
                    }
                }
                if (draw) {
                    std::vector<core::Vec2> ring;
                    core::ellipse::tessellate(e, core::length(e.major) / 2000.0, ring);
                    for (std::size_t i = 0; i + 1 < ring.size(); ++i) {
                        seg.push_back(ring[i]);
                        seg.push_back(ring[i + 1]);
                    }
                    if (pv.ellipse_stage == 1) {
                        seg.push_back(pts[0]); // rubber line: the start angle being picked
                        seg.push_back(cur);
                    }
                }
            }
            break;
        case command::PreviewKind::Polygon:
            if (!pts.empty() && pv.sides >= 3) {
                // Built by the SAME rule the command commits with, so what is dragged
                // out is what lands: the cursor fixes one vertex when inscribed, or the
                // midpoint of one edge when circumscribed.
                const core::Vec2 c = pts[0];
                const core::Vec2 r = cur_eff - c;
                const std::vector<core::Vec2> v = core::polygon_vertices(
                    c, core::length(r), pv.sides, pv.inscribed, std::atan2(r.y, r.x));
                for (std::size_t i = 0; i < v.size(); ++i) {
                    seg.push_back(v[i]);
                    seg.push_back(v[(i + 1) % v.size()]);
                }
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
                    } else if (t == core::DimType::ArcLength) {
                        d.a = pdim_a_; // the arc is fixed; the dimension arc's radius
                        d.b = pdim_b_; // follows the cursor
                        d.aux = pdim_aux_;
                        d.line_pt = cur;
                    } else if (t == core::DimType::Jogged) {
                        const double r = core::distance(pdim_a_, pdim_b_);
                        core::Vec2 dir = cur - pdim_a_;
                        dir = core::length_squared(dir) > 1e-12 ? core::normalized(dir)
                                                               : core::Vec2{1.0, 0.0};
                        d.a = pdim_a_;
                        d.b = pdim_a_ + dir * r;
                        d.line_pt = pdim_a_ + dir * (r * 0.4); // a stand-in centre override
                        d.aux = 0.5;
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

        // On-canvas Dynamic Input value fields: anchored to the live geometry (drawn
        // by the renderer with the same camera as the rubber-band, so they never
        // drift). The shown value is the typed buffer if any, else the live value.
        if (dim) {
            const core::Vec2 a = pts.empty() ? core::Vec2{0, 0} : pts[0];
            const core::Vec2 b = cur_eff;
            const auto unit = [](core::Vec2 v) -> core::Vec2 {
                const double l = core::length(v);
                return l > 1e-9 ? core::Vec2{v.x / l, v.y / l} : core::Vec2{0, 0};
            };
            const core::Vec2 rcenter = (a + b) * 0.5;
            const std::vector<command::DynField> fields = command::dyn_fields(pv, cur_eff);
            for (const command::DynField& f : fields) {
                render::DynLabel label;
                label.anchor = f.anchor;
                // Push the box OUTWARD so fields never overlap and sit just off the edge.
                if (pv.kind == command::PreviewKind::Rectangle) {
                    label.out = unit(f.anchor - rcenter);
                } else if (pv.kind == command::PreviewKind::Segment) {
                    label.out = (f.slot == 0) ? unit({-(b.y - a.y), b.x - a.x}) // Length: aside
                                              : unit(b - a);                    // Angle: ahead
                } else { // Circle radius: beside the radius line
                    label.out = unit({-(b.y - a.y), b.x - a.x});
                }
                const std::string& buf =
                    (f.slot >= 0 && f.slot < 2) ? dyn_buf_[static_cast<std::size_t>(f.slot)]
                                                : std::string{};
                label.text = buf.empty() ? dyn_format(f.value) : buf;
                label.focused = (f.slot == dyn_active_slot_);
                ov.dyn_labels.push_back(std::move(label));
            }
        }
    }

    // On-canvas command input: idle entry + autocomplete, OR the mid-command sub-prompt
    // cell (FILLET radius, CHAMFER distances, option keywords) -- one primitive, two
    // anchor strategies (screen-fixed entry vs at-cursor sub-prompt).
    if (cmd_active_) {
        build_command_ui(ov.command_ui);
    } else if (sub_prompt_active()) {
        build_sub_prompt_ui(ov.command_ui); // buffer is cleared on Enter (per step)
    } else if (!sub_entry_.empty()) {
        sub_entry_.clear(); // command ended / entered a rubber-band: drop stale text
    }
    // A dialog-driven ghost (Rotate/Scale dialogs) shows the selection transformed by
    // the typed value, through the same ghost path the commands' rubber bands use.
    if (dialog_ghost_mode_ != 0 && ov.ghost_mode == 0) {
        ov.ghost_mode = dialog_ghost_mode_;
        ov.ghost_a = dialog_ghost_a_;
        ov.ghost_param = dialog_ghost_param_;
    }
    // Publish as an immutable shared object; the render thread references it.
    auto published = std::make_shared<const render::RenderOverlay>(std::move(ov));
    {
        std::scoped_lock lock(overlay_mutex_);
        overlay_ = std::move(published);
    }
    // Stream the live-stretch delta only AFTER the overlay is handed off: the engine
    // publishes within a millisecond and the render thread then consumes, taking the
    // same overlay/grip locks -- submitting earlier made this thread wait on them.
    if (pending_stretch) {
        engine_.submit(core::StretchPreviewCommand{*pending_stretch, true});
    }
}

// ---------------------------------------------------------------------------
// On-canvas Dynamic Input (the value fields drawn ON the geometry by the renderer)
// ---------------------------------------------------------------------------
bool ViewportWindow::dyn_dimensional() const {
    if (processor_ == nullptr) {
        return false;
    }
    const command::PreviewSpec& pv = processor_->preview();
    // A single scalar/keyword sub-step (e.g. RECTANGLE Dimensions length/width) uses the
    // at-cursor sub-prompt cell, not the on-geometry Length/Width drag fields.
    if (pv.scalar_prompt) {
        return false;
    }
    using command::PreviewKind;
    return pv.kind == PreviewKind::Segment || pv.kind == PreviewKind::Circle ||
           pv.kind == PreviewKind::Rectangle;
}

int ViewportWindow::dyn_field_count() const {
    if (processor_ == nullptr) {
        return 0;
    }
    using command::PreviewKind;
    switch (processor_->preview().kind) {
    case PreviewKind::Rectangle:
    case PreviewKind::Segment:
        return 2;
    case PreviewKind::Circle:
        return 1;
    default:
        return 0;
    }
}

bool ViewportWindow::dyn_capturing() const {
    return dyn_enabled_ && processor_ != nullptr && processor_->has_active_command() &&
           dyn_dimensional();
}

bool ViewportWindow::dyn_typing() const {
    return dyn_capturing() && (!dyn_buf_[0].empty() || !dyn_buf_[1].empty());
}

std::string ViewportWindow::dyn_value(int slot) const {
    if (slot < 0 || slot >= 2) {
        return {};
    }
    return dyn_buf_[static_cast<std::size_t>(slot)];
}

void ViewportWindow::set_dyn_enabled(bool on) {
    dyn_enabled_ = on;
    if (!on) {
        dyn_reset();
    }
    rebuild_overlay();
}

bool ViewportWindow::dyn_commit() {
    if (processor_ == nullptr) {
        return false;
    }
    const std::string line = command::compose_dyn_submit(processor_->preview(), dyn_cursor_,
                                                         dyn_parse(dyn_buf_[0]),
                                                         dyn_parse(dyn_buf_[1]));
    if (line.empty()) {
        return false;
    }
    dyn_reset();
    processor_->submit_line(line);
    return true;
}

void ViewportWindow::dyn_end_step() {
    if (processor_ == nullptr) {
        return;
    }
    if (dyn_capturing()) {
        // Dimensional rubber-band: a typed value commits (keeps drawing); nothing typed
        // -> end the step (an empty submit ends LINE at a next-point prompt).
        if (!dyn_commit()) {
            processor_->submit_line("");
        }
    } else if (sub_prompt_active()) {
        // Value/keyword sub-prompt: submit the typed text, or the command's default when
        // empty (accept <0>, end the command, ...). Mirrors sub_prompt_handle_key's Enter.
        const std::string v = sub_entry_;
        sub_entry_.clear();
        processor_->submit_line(v);
        rebuild_overlay();
    } else {
        // Active command with no canvas input field -> end the current step.
        processor_->submit_line("");
    }
}

void ViewportWindow::dyn_test_type(const std::string& chars) {
    if (!dyn_capturing()) {
        return;
    }
    dyn_buf_[static_cast<std::size_t>(dyn_active_slot_)] += chars;
    rebuild_overlay();
}

void ViewportWindow::dyn_test_tab() {
    const int n = dyn_field_count();
    if (n > 1) {
        dyn_active_slot_ = (dyn_active_slot_ + 1) % n;
        rebuild_overlay();
    }
}

bool ViewportWindow::dyn_test_commit() {
    return dyn_commit();
}

// ---------------------------------------------------------------------------
// On-canvas command entry (idle): typed command + autocomplete, drawn on the canvas
// ---------------------------------------------------------------------------
void ViewportWindow::cmd_clear() noexcept {
    cmd_entry_.clear();
    cmd_suggestions_.clear();
    cmd_active_ = false;
    cmd_sel_ = 0;
}

void ViewportWindow::cmd_recompute() {
    cmd_suggestions_.clear();
    cmd_sel_ = 0;
    if (processor_ != nullptr && !cmd_entry_.empty()) {
        cmd_suggestions_ = processor_->registry().suggest(cmd_entry_);
    }
}

std::string ViewportWindow::cmd_font() {
    if (!cmd_font_name_.empty() || font_engine_ == nullptr) {
        return cmd_font_name_;
    }
    // A face is usable only if it really yields outlines: the font database lists faces
    // whose glyphs have none (on Windows the alphabetical list starts with "8514oem", a
    // raster font), and such a pick drew the command entry as an empty box.
    const auto usable = [this](const std::string& n) {
        if (n.empty() || !font_engine_->is_outline_font(n)) {
            return false;
        }
        std::vector<core::Vec2> tris;
        font_engine_->glyph_fills(n, "A", {0.0, 0.0}, 1.0, 0.0, tris);
        return !tris.empty();
    };
    // The platform's own UI font first (Segoe UI on Windows, the desktop sans on Linux),
    // so the on-canvas entry looks like the rest of the interface; the first usable face
    // in the database only as the fallback.
    const std::string ui_font = QGuiApplication::font().family().toStdString();
    if (usable(ui_font)) {
        cmd_font_name_ = ui_font;
        return cmd_font_name_;
    }
    for (const std::string& n : font_engine_->available()) {
        if (usable(n)) {
            cmd_font_name_ = n;
            break;
        }
    }
    return cmd_font_name_;
}

void ViewportWindow::append_glyphs(const std::string& text, double ox, double baseline_y, double h,
                                   std::vector<core::Vec2>& out) {
    const std::string face = cmd_font();
    if (font_engine_ == nullptr || face.empty() || text.empty()) {
        return;
    }
    // One layout per (face, height, text); every later use is a translate of the
    // cached run. Bounded: the cache is dropped wholesale when it grows large, which
    // costs one relayout of whatever is on screen and nothing else.
    const std::string key = face + '\x1f' + std::to_string(h) + '\x1f' + text;
    auto it = glyph_run_cache_.find(key);
    if (it == glyph_run_cache_.end()) {
        if (glyph_run_cache_.size() > 256) {
            glyph_run_cache_.clear();
        }
        std::vector<core::Vec2> tris;
        font_engine_->glyph_fills(face, text, {0.0, 0.0}, h, 0.0, tris);
        it = glyph_run_cache_.emplace(key, std::move(tris)).first;
    }
    const std::vector<core::Vec2>& tris = it->second;
    out.reserve(out.size() + tris.size());
    for (const core::Vec2& v : tris) {
        out.push_back({ox + v.x, baseline_y - v.y}); // font is y-up; screen is y-down
    }
}

double ViewportWindow::cmd_advance(const std::string& text, double h) {
    const std::string face = cmd_font();
    if (font_engine_ == nullptr || face.empty()) {
        return static_cast<double>(text.size()) * h * 0.55;
    }
    const std::string key = face + '\x1f' + std::to_string(h) + '\x1f' + text;
    auto it = advance_cache_.find(key);
    if (it == advance_cache_.end()) {
        if (advance_cache_.size() > 512) {
            advance_cache_.clear();
        }
        it = advance_cache_.emplace(key, font_engine_->advance(face, text, h)).first;
    }
    return it->second;
}

bool ViewportWindow::cmd_entry_handle_key(int key, const QString& text) {
    if (processor_ == nullptr) {
        return false;
    }
    const int n = static_cast<int>(cmd_suggestions_.size());
    switch (key) {
    case Qt::Key_Escape:
        if (cmd_active_) {
            cmd_clear();
            rebuild_overlay();
            return true;
        }
        return false;
    case Qt::Key_Return:
    case Qt::Key_Enter: {
        if (!cmd_active_) {
            return false;
        }
        std::string run = cmd_entry_;
        if (!processor_->registry().contains(cmd_entry_) && n > 0) {
            run = cmd_suggestions_[static_cast<std::size_t>(std::clamp(cmd_sel_, 0, n - 1))].alias;
        }
        cmd_clear();
        rebuild_overlay();
        if (!run.empty()) {
            processor_->submit_line(run);
        }
        return true;
    }
    case Qt::Key_Backspace:
        if (!cmd_active_) {
            return false;
        }
        if (!cmd_entry_.empty()) {
            cmd_entry_.pop_back();
        }
        if (cmd_entry_.empty()) {
            cmd_clear();
        } else {
            cmd_recompute();
        }
        rebuild_overlay();
        return true;
    case Qt::Key_Down:
    case Qt::Key_Tab:
        if (cmd_active_ && n > 0) {
            cmd_sel_ = (cmd_sel_ + 1) % n;
            rebuild_overlay();
            return true;
        }
        return false;
    case Qt::Key_Up:
    case Qt::Key_Backtab:
        if (cmd_active_ && n > 0) {
            cmd_sel_ = (cmd_sel_ - 1 + n) % n;
            rebuild_overlay();
            return true;
        }
        return false;
    default:
        break;
    }
    if (!text.isEmpty()) {
        const QChar c = text.at(0);
        if (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_')) {
            if (!cmd_active_) {
                cmd_active_ = true;
                cmd_anchor_px_ = {cursor_px_x_.load(std::memory_order_relaxed),
                                  cursor_px_y_.load(std::memory_order_relaxed)};
            }
            cmd_entry_ += c.toLatin1();
            cmd_recompute();
            rebuild_overlay();
            return true;
        }
    }
    return false;
}

void ViewportWindow::build_command_ui(render::CanvasCommandUI& ui) {
    ui.active = true;
    const double dpr = devicePixelRatio();
    const double h = 16.0 * dpr;        // input glyph cap height
    const double rh = 14.0 * dpr;       // dropdown glyph cap height
    const double padx = 8.0 * dpr;
    const double pady = 5.0 * dpr;
    const double inrow = h + 2.0 * pady;
    const double droprow = rh + 2.0 * pady;
    const std::string face = cmd_font();
    const auto adv = [&](const std::string& s, double gh) {
        return (font_engine_ != nullptr && !face.empty())
                   ? cmd_advance(s, gh)
                   : static_cast<double>(s.size()) * gh * 0.55; // fallback width estimate
    };
    const auto quad = [](std::vector<core::Vec2>& v, double x, double y, double w, double hh) {
        ui_quad(v, x, y, w, hh);
    };
    const auto outline = [](std::vector<core::Vec2>& v, double x, double y, double w, double hh) {
        ui_outline(v, x, y, w, hh);
    };

    // Clamp the panel so it stays on-screen (anchored where it first appeared).
    const double vw = static_cast<double>(width()) * dpr;
    const double ax = std::min(cmd_anchor_px_.x + 18.0 * dpr, vw - 320.0 * dpr);
    const double ay = cmd_anchor_px_.y + 18.0 * dpr;

    // Input box.
    const double tw = adv(cmd_entry_, h);
    double bw = std::max(tw + 2.0 * padx, 150.0 * dpr);
    const int rows = std::min(static_cast<int>(cmd_suggestions_.size()), 9);
    for (int i = 0; i < rows; ++i) {
        const std::string s = cmd_suggestions_[static_cast<std::size_t>(i)].alias + "    " +
                              cmd_suggestions_[static_cast<std::size_t>(i)].name;
        bw = std::max(bw, adv(s, rh) + 2.0 * padx);
    }
    quad(ui.box_fills, ax, ay, bw, inrow);
    append_glyphs(cmd_entry_, ax + padx, ay + pady + h, h, ui.glyph_fills);
    const double cx = ax + padx + tw + 1.0 * dpr; // caret
    ui.lines.push_back({cx, ay + pady});
    ui.lines.push_back({cx, ay + pady + h});
    outline(ui.lines, ax, ay, bw, inrow);

    // Autocomplete dropdown.
    for (int i = 0; i < rows; ++i) {
        const double ry = ay + inrow + static_cast<double>(i) * droprow;
        quad(ui.box_fills, ax, ry, bw, droprow);
        if (i == std::clamp(cmd_sel_, 0, rows - 1)) {
            quad(ui.hi_fills, ax, ry, bw, droprow);
        }
        const std::string s = cmd_suggestions_[static_cast<std::size_t>(i)].alias + "    " +
                              cmd_suggestions_[static_cast<std::size_t>(i)].name;
        append_glyphs(s, ax + padx, ry + pady + rh, rh, ui.glyph_fills);
    }
    if (rows > 0) {
        outline(ui.lines, ax, ay + inrow, bw, static_cast<double>(rows) * droprow);
    }
}

// ---------------------------------------------------------------------------
// On-canvas mid-command sub-prompt (FILLET radius, CHAMFER distances, options)
// ---------------------------------------------------------------------------
bool ViewportWindow::sub_prompt_active() const {
    // A command is running but not in a dimensional rubber-band (those use the value
    // fields). This step's input surface is the at-cursor prompt cell.
    return dyn_enabled_ && processor_ != nullptr && processor_->has_active_command() &&
           !dyn_dimensional();
}

bool ViewportWindow::sub_prompt_handle_key(int key, const QString& text) {
    if (!sub_prompt_active()) {
        return false;
    }
    switch (key) {
    case Qt::Key_Escape:
        return false; // the caller cancels the command
    case Qt::Key_Return:
    case Qt::Key_Enter: {
        const std::string v = sub_entry_;
        sub_entry_.clear();
        sync_selection_to_processor();
        processor_->submit_line(v); // empty -> the command's default (accept <0>, end LINE, ...)
        rebuild_overlay();
        return true;
    }
    case Qt::Key_Backspace:
        if (sub_entry_.empty()) {
            return false;
        }
        sub_entry_.pop_back();
        rebuild_overlay();
        return true;
    default:
        break;
    }
    if (!text.isEmpty()) {
        const QChar c = text.at(0);
        if (c.isLetterOrNumber() || c == QLatin1Char('.') || c == QLatin1Char('-') ||
            c == QLatin1Char(',') || c == QLatin1Char('@') || c == QLatin1Char('<') ||
            c == QLatin1Char('/')) {
            sub_entry_ += c.toLatin1();
            rebuild_overlay();
            return true;
        }
    }
    return false;
}

void ViewportWindow::build_sub_prompt_ui(render::CanvasCommandUI& ui) {
    ui.active = true;
    const double dpr = devicePixelRatio();
    const double h = 15.0 * dpr;
    const double padx = 8.0 * dpr;
    const double pady = 5.0 * dpr;
    const double row = h + 2.0 * pady;
    const std::string face = cmd_font();
    const auto adv = [&](const std::string& s) {
        return (font_engine_ != nullptr && !face.empty())
                   ? cmd_advance(s, h)
                   : static_cast<double>(s.size()) * h * 0.55;
    };
    const std::string label = processor_ != nullptr ? processor_->current_prompt() : std::string{};
    const double lw = adv(label);
    const double gap = sub_entry_.empty() ? 0.0 : 6.0 * dpr;
    const double vw = adv(sub_entry_);
    const double bw = lw + gap + vw + 2.0 * padx + 10.0 * dpr; // trailing room for the caret

    // At-cursor anchor (follows the cursor), clamped on-screen.
    const double scrw = static_cast<double>(width()) * dpr;
    const double ax = std::min(cursor_px_x_.load(std::memory_order_relaxed) + 18.0 * dpr,
                               std::max(0.0, scrw - bw));
    const double ay = cursor_px_y_.load(std::memory_order_relaxed) + 18.0 * dpr;

    ui_quad(ui.box_fills, ax, ay, bw, row);
    const double baseline = ay + pady + h;
    append_glyphs(label, ax + padx, baseline, h, ui.glyph_fills);
    append_glyphs(sub_entry_, ax + padx + lw + gap, baseline, h, ui.glyph_fills);
    const double cx = ax + padx + lw + gap + vw + 2.0 * dpr;
    ui.lines.push_back({cx, ay + pady});
    ui.lines.push_back({cx, ay + pady + h});
    ui_outline(ui.lines, ax, ay, bw, row);
}

// On-canvas Dynamic Input keystroke routing. Returns true if the key was consumed by a
// canvas field. Called from MainWindow's app-wide event filter (so dimension keystrokes
// reach the fields even though the command line holds focus) AND from keyPressEvent.
bool ViewportWindow::dyn_handle_key(int key, const QString& text) {
    if (!dyn_capturing()) {
        return false;
    }
    if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {
        const int n = dyn_field_count();
        if (n <= 1) {
            return false;
        }
        dyn_active_slot_ = (dyn_active_slot_ + (key == Qt::Key_Tab ? 1 : n - 1)) % n;
        rebuild_overlay();
        return true;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        return dyn_commit(); // false if nothing typed -> Enter still ends the command
    }
    if (key == Qt::Key_Backspace) {
        std::string& b = dyn_buf_[static_cast<std::size_t>(dyn_active_slot_)];
        if (b.empty()) {
            return false;
        }
        b.pop_back();
        rebuild_overlay();
        return true;
    }
    if (key == Qt::Key_Escape || text.isEmpty()) {
        return false; // Esc -> cancel handled by the caller
    }
    const QChar c = text.at(0);
    if (c.isDigit() || c == QLatin1Char('.') || c == QLatin1Char('-') || c == QLatin1Char(',') ||
        c == QLatin1Char('@') || c == QLatin1Char('<')) {
        dyn_buf_[static_cast<std::size_t>(dyn_active_slot_)] += c.toLatin1();
        rebuild_overlay();
        return true;
    }
    if (c.isLetter()) {
        // An option keyword (e.g. Area/Dimensions/Rotation, Close/Undo) -> the pipeline.
        processor_->submit_line(std::string(1, c.toUpper().toLatin1()));
        dyn_reset();
        return true;
    }
    return false;
}

void ViewportWindow::keyPressEvent(QKeyEvent* event) {
    if (processor_ == nullptr) {
        QWindow::keyPressEvent(event);
        return;
    }
    if (dyn_handle_key(event->key(), event->text())) {
        return; // consumed by an on-canvas Dynamic Input field
    }
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

core::Vec2 ViewportWindow::world_to_widget(core::Vec2 world) {
    core::Vec2 px;
    {
        std::scoped_lock lock(camera_mutex_);
        px = camera_.world_to_screen(world);
    }
    const double dpr = devicePixelRatio();
    return {px.x / dpr, px.y / dpr};
}

void ViewportWindow::sync_selection_to_processor() {
    // The processor's selection count is a cache the host refreshes on a timer. Before an
    // Enter or right-click that a command will judge by "is anything selected?", hand it
    // the count this viewport has ALREADY been published, so a drag-then-Enter is judged
    // on the selection it just made rather than on a 100 ms-old copy.
    if (processor_ != nullptr) {
        processor_->set_selection_count(selection_count_.load(std::memory_order_relaxed));
    }
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
    // Cancel the active command, or clear the selection when idle. Rebuild the overlay so
    // the rubber-band + on-canvas DYN fields clear at once (in canvas mode no later focus
    // event would otherwise trigger a redraw).
    processor_->cancel();
    rebuild_overlay();
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
