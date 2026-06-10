#pragma once

#include <atomic>
#include <functional>
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
    void open_properties() override;
    void import_dwg() override;
    void export_dwg() override;

Q_SIGNALS:
    void cursorWorldMoved(double x, double y);
    /// Cursor position in the viewport's logical local pixels (for anchoring DYN).
    void cursorScreenMoved(double px, double py);
    /// The constrained (ortho/polar/snap) world cursor during a preview (DYN reads
    /// this to show live length/angle/radius without recomputing geometry).
    void constrainedCursorMoved(double cx, double cy);
    /// A viewport pick/select interaction completed (so the host can re-acquire DYN
    /// focus -- it is only re-grabbed after a viewport mouse event, never on a timer).
    void pickerInteracted();

public:
    /// Cancel a grip drag if one is active, else the active command / selection.
    /// Public so the Dynamic Input field can route Esc here (it holds focus when on).
    void handle_escape();

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
    /// World AABB of the current geometry (for self-tests). Returns false if empty.
    [[nodiscard]] bool content_bounds(core::Vec2& mn, core::Vec2& mx) const {
        std::scoped_lock lock(grips_mutex_);
        mn = bounds_min_;
        mx = bounds_max_;
        return has_bounds_;
    }

    /// Number of grips published for the selected set (test hook: proves grips show).
    [[nodiscard]] int grip_count() const {
        std::scoped_lock lock(grips_mutex_);
        return static_cast<int>(grips_cache_.size());
    }
    /// The i-th cached grip (test hook for the real-window grip self-test).
    [[nodiscard]] core::GripInfo grip_info(int i) const {
        std::scoped_lock lock(grips_mutex_);
        return (i >= 0 && i < static_cast<int>(grips_cache_.size()))
                   ? grips_cache_[static_cast<std::size_t>(i)]
                   : core::GripInfo{};
    }

    /// The kind of entity under the cursor in the last snapshot, encoded as its
    /// EntityKind value + 1 (0 = nothing hovered). The smart DIM command reads this
    /// (via the processor) to preview the dimension type it will create.
    [[nodiscard]] std::optional<core::EntityKind> hovered_kind() const noexcept {
        const int v = hovered_kind_.load(std::memory_order_relaxed);
        return v == 0 ? std::nullopt
                      : std::optional<core::EntityKind>{static_cast<core::EntityKind>(v - 1)};
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

    /// A double-click on a text-bearing entity (TEXT/MTEXT/QLEADER label): where it
    /// was hit (world), the pick aperture, the current content, and whether the
    /// editor should be multi-line. The host opens a (dark-themed) editor and, on
    /// confirm, submits an EditTextContentCommand at `at`.
    struct TextEditRequest {
        render::Vec2 at;
        double pick_radius = 0.0;
        std::string content;
        bool multiline = false;
    };
    void set_text_edit_callback(std::function<void(const TextEditRequest&)> cb) {
        text_edit_callback_ = std::move(cb);
    }

    /// The aggregated property view of the current selection (PR palette). Copied
    /// lock-free under the cache lock; the UI never queries the store.
    [[nodiscard]] core::SelectionSummary selection_summary() const {
        std::scoped_lock lock(grips_mutex_);
        return selection_summary_;
    }
    /// PR command -> toggle the palette. The MainWindow (which owns the dock) sets
    /// this; ViewControl::open_properties() forwards to it on the GUI thread.
    void set_properties_toggle_callback(std::function<void()> cb) {
        properties_toggle_callback_ = std::move(cb);
    }
    /// DWGIN/DWGOUT command -> the MainWindow (which owns the file dialogs + converter)
    /// runs the import/export. ViewControl::import_dwg/export_dwg forward to these.
    void set_dwg_import_callback(std::function<void()> cb) { dwg_import_callback_ = std::move(cb); }
    void set_dwg_export_callback(std::function<void()> cb) { dwg_export_callback_ = std::move(cb); }


    /// Snapshot of the editable text contents (for self-tests / observed-outcome
    /// checks). Copied under the cache lock.
    [[nodiscard]] std::vector<std::string> text_contents() const {
        std::scoped_lock lock(grips_mutex_);
        std::vector<std::string> out;
        out.reserve(text_targets_.size());
        for (const core::TextEditTarget& t : text_targets_) {
            out.push_back(t.content);
        }
        return out;
    }

protected:
    void exposeEvent(QExposeEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
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
    /// Index of the cached grip within `radius_world` of `world`, or -1 (hit-test).
    [[nodiscard]] int grip_at(core::Vec2 world, double radius_world) const;

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
    std::atomic<int> hovered_kind_{0}; // EntityKind+1, or 0 for nothing hovered
    std::atomic<bool> dirty_{false};
    std::atomic<std::uint64_t> document_version_{0};

    // Resolved object-dimension def points + the Standard style, cached from the
    // snapshot for the GUI-thread placement preview (Phase 16 Part C).
    mutable std::mutex pending_dim_mutex_;
    bool pending_dim_valid_ = false;
    core::Vec2 pdim_a_{};
    core::Vec2 pdim_b_{};
    core::Vec2 pdim_line_pt_{};
    std::uint8_t pdim_type_ = 0;
    core::DimStyle pdim_style_{};

    // Grips of the selected set, cached from the snapshot for GUI-thread hit-testing,
    // plus the active grip-drag state (direct manipulation, Phase 17).
    mutable std::mutex grips_mutex_;
    std::vector<core::GripInfo> grips_cache_;
    std::vector<core::TextEditTarget> text_targets_; // for double-click-to-edit (same mutex)
    std::function<void(const TextEditRequest&)> text_edit_callback_;
    core::SelectionSummary selection_summary_; // PR palette (same mutex)
    core::Vec2 bounds_min_{}; // content AABB (same mutex; for self-tests)
    core::Vec2 bounds_max_{};
    bool has_bounds_ = false;
    std::function<void()> properties_toggle_callback_;
    std::function<void()> dwg_import_callback_;
    std::function<void()> dwg_export_callback_;
    bool dragging_grip_ = false;
    core::Vec2 grip_origin_{};

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
