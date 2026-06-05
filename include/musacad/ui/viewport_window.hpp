#pragma once

#include <atomic>
#include <mutex>
#include <thread>

#include <QWindow>

#include "musacad/command/command_context.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/render/camera.hpp"
#include "musacad/ui/viewport_modes.hpp"

class QExposeEvent;
class QMouseEvent;
class QWheelEvent;
class QResizeEvent;

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

private:
    void start_render_thread();
    void stop_render_thread() noexcept;
    void render_loop(std::stop_token token);
    void update_viewport_size() noexcept;

    core::GeometryEngine& engine_;

    std::mutex camera_mutex_;
    render::Camera2D camera_; // guarded by camera_mutex_

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
