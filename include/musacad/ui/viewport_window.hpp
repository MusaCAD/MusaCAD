#pragma once

#include <atomic>
#include <mutex>
#include <thread>

#include <QWindow>

#include "musacad/command/command_context.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/render/camera.hpp"
#include "musacad/render/overlay.hpp"
#include "musacad/ui/viewport_modes.hpp"

class QExposeEvent;
class QMouseEvent;
class QWheelEvent;
class QResizeEvent;
class QKeyEvent;

namespace musacad::command {
class CommandProcessor;
}

namespace musacad::ui {

/// An OpenGL viewport hosted in a QWindow. A dedicated render thread owns the GL
/// context and draws at display refresh, consuming render snapshots lock-free
/// from the GeometryEngine (it never touches the GeometryStore). Camera input is
/// handled on the GUI thread and shared with the render thread under a small
/// lock, so pan/zoom never involve the geometry thread. Implements ViewControl
/// so the ZOOM command can drive the view without a geometry round-trip.
class ViewportWindow : public QWindow, public command::ViewControl {
    Q_OBJECT

public:
    explicit ViewportWindow(core::GeometryEngine& engine, QWindow* parent = nullptr);
    ~ViewportWindow() override;

    // ViewControl (callable from the GUI thread).
    void zoom_extents() override;
    void zoom_scale(double factor) override;

Q_SIGNALS:
    void cursorWorldMoved(double x, double y);

public:
    /// Latest measured frames-per-second (thread-safe).
    [[nodiscard]] double fps() const noexcept { return fps_.load(std::memory_order_relaxed); }

    /// Number of currently-selected entities (for enabling Modify buttons).
    [[nodiscard]] int selection_count() const noexcept {
        return selection_count_.load(std::memory_order_relaxed);
    }

    /// Latest engine command-result message and its monotonically-rising version
    /// (so the command line can echo each new result once). Honest feedback from
    /// the geometry thread via the snapshot.
    [[nodiscard]] std::uint64_t status_version() const noexcept {
        return status_version_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::string last_status() const {
        std::scoped_lock lock(status_mutex_);
        return status_;
    }

    /// Number of line-segment vertices in the last consumed snapshot (test hook:
    /// proves geometry edits actually reach the rendered snapshot).
    [[nodiscard]] int line_vertex_count() const noexcept {
        return line_vertex_count_.load(std::memory_order_relaxed);
    }

    /// Unsaved-changes flag and the document version (bumps on save/open/new),
    /// from the published snapshot -- drives the title bar and dirty prompts.
    [[nodiscard]] bool dirty() const noexcept { return dirty_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t document_version() const noexcept {
        return document_version_.load(std::memory_order_relaxed);
    }

    /// The published layer table + current layer (for the Layer Manager / combo).
    [[nodiscard]] std::vector<core::Layer> layers() const {
        std::scoped_lock lock(layers_mutex_);
        return layers_;
    }
    [[nodiscard]] std::uint16_t current_layer() const noexcept {
        return current_layer_.load(std::memory_order_relaxed);
    }

    /// Requests the camera frame this world-space AABB once the viewport size is
    /// known (applied on the first rendered frame).
    void set_initial_view(render::Vec2 min_world, render::Vec2 max_world) noexcept;

    void set_processor(command::CommandProcessor* processor) noexcept { processor_ = processor; }
    void set_modes(ViewportModes* modes) noexcept { modes_ = modes; }

protected:
    void exposeEvent(QExposeEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void start_render_thread();
    void stop_render_thread() noexcept;
    void render_loop(std::stop_token token);
    void update_viewport_size() noexcept;
    void rebuild_overlay();

    core::GeometryEngine& engine_;

    std::mutex camera_mutex_;
    render::Camera2D camera_; // guarded by camera_mutex_

    std::mutex overlay_mutex_;
    render::RenderOverlay overlay_; // guarded by overlay_mutex_

    // Selection drag state (UI thread).
    bool selecting_ = false;
    bool sel_additive_ = false;
    core::Vec2 sel_start_screen_{};
    core::Vec2 sel_start_world_{};
    core::Vec2 sel_cur_world_{};
    std::atomic<int> selection_count_{0};
    std::atomic<int> line_vertex_count_{0};
    std::atomic<bool> dirty_{false};
    std::atomic<std::uint64_t> document_version_{0};

    // Published layer table + current layer (for the Layer Manager / ribbon combo).
    mutable std::mutex layers_mutex_;
    std::vector<core::Layer> layers_;
    std::atomic<std::uint16_t> current_layer_{0};

    // Engine command-result status, copied from the published snapshot.
    mutable std::mutex status_mutex_;
    std::string status_;
    std::atomic<std::uint64_t> status_version_{0};

    std::atomic<int> fb_width_{1};
    std::atomic<int> fb_height_{1};
    std::atomic<double> fps_{0.0};
    std::atomic<int> frames_rendered_{0};
    std::atomic<bool> zoom_extents_requested_{false};

    // Cursor (device px) shared GUI->render for the crosshair.
    std::atomic<bool> cursor_inside_{false};
    std::atomic<double> cursor_px_x_{0.0};
    std::atomic<double> cursor_px_y_{0.0};
    // Latest snap (from the snapshot) shared render->GUI for click-time picking.
    std::atomic<bool> snap_has_{false};
    std::atomic<double> snap_x_{0.0};
    std::atomic<double> snap_y_{0.0};

    command::CommandProcessor* processor_ = nullptr;
    ViewportModes* modes_ = nullptr;

    bool panning_ = false;
    double last_x_ = 0.0;
    double last_y_ = 0.0;
    bool camera_initialized_ = false;
    bool has_initial_view_ = false;
    render::Vec2 init_min_{};
    render::Vec2 init_max_{};

    std::jthread render_thread_;
};

} // namespace musacad::ui
