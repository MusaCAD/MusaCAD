// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <QWindow>

#include "musacad/command/command_context.hpp"
#include "musacad/command/command_registry.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/render/camera.hpp"
#include "musacad/render/overlay.hpp"
#include "musacad/ui/viewport_modes.hpp"

class QExposeEvent;
class QMouseEvent;
class QWheelEvent;
class QResizeEvent;
class QKeyEvent;

namespace musacad::core {
class IFontEngine;
}

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
    void plot_dialog() override;

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

    /// Multi-document: the open-document list (tab strip) + the active document id, as of
    /// the latest consumed snapshot. The GUI thread reads these to render the tabs/title.
    [[nodiscard]] std::vector<core::DocumentInfo> documents() const {
        std::scoped_lock lock(docs_mutex_);
        return docs_cache_;
    }
    [[nodiscard]] std::uint64_t active_document_id() const {
        std::scoped_lock lock(docs_mutex_);
        return active_doc_id_cache_;
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
    /// PLOT command -> the MainWindow opens the plot dialog. ViewControl::plot_dialog()
    /// forwards here on the GUI thread.
    void set_plot_dialog_callback(std::function<void()> cb) { plot_dialog_callback_ = std::move(cb); }
    /// Arm "pick a plot window": the next left-drag rectangle is delivered to `cb`
    /// instead of selecting. `cb(true, mn, mx)` for a real drag; `cb(false, …)` for a
    /// click-without-drag or Esc (cancel). The callback ALWAYS fires exactly once so the
    /// host can re-open the dialog either way (the user is never left with no dialog).
    void begin_plot_window_pick(std::function<void(bool, core::Vec2, core::Vec2)> cb) {
        plot_pick_callback_ = std::move(cb);
        plot_picking_ = true;
        selecting_ = false;
    }
    /// Cancel an armed plot-window pick (Esc) -- fires the callback with ok=false.
    void cancel_plot_window_pick() {
        if (plot_picking_) {
            plot_picking_ = false;
            selecting_ = false;
            if (auto cb = std::move(plot_pick_callback_)) {
                plot_pick_callback_ = nullptr;
                cb(false, {}, {});
            }
            rebuild_overlay();
        }
    }
    /// The current view's world-space rectangle (for the "Display" plot area).
    void view_world_rect(core::Vec2& out_min, core::Vec2& out_max) {
        std::scoped_lock lock(camera_mutex_);
        out_min = camera_.visible_min();
        out_max = camera_.visible_max();
    }

    /// Project a world point to viewport-local (device-independent) pixels, for
    /// anchoring on-geometry overlays such as the Dynamic-Input field tooltips.
    [[nodiscard]] core::Vec2 world_to_screen_px(core::Vec2 world) {
        std::scoped_lock lock(camera_mutex_);
        return camera_.world_to_screen(world);
    }
    /// Dynamic Input (F12): enable the on-canvas value fields. When on and a
    /// dimensional rubber-band is active, the viewport draws the value boxes ON the
    /// geometry (overlay-rendered, never an OS window) and captures dimension
    /// keystrokes (type without a click; Tab/Enter/Esc).
    void set_dyn_enabled(bool on);
    /// True while DYN is on AND a tip-driven rubber-band is active -- the viewport is
    /// capturing dimension keystrokes for the on-canvas fields.
    [[nodiscard]] bool dyn_capturing() const;
    /// Route one key into the on-canvas fields (digits/'.'/'-' edit, Tab/Shift-Tab
    /// switch, Enter commit, Backspace delete, letters -> option keyword). Returns true
    /// if consumed. Called from the app-wide event filter so dimension keystrokes reach
    /// the canvas fields regardless of which widget holds focus (the command line does
    /// by default). Enter/Backspace on an empty field return false so normal handling
    /// (e.g. Enter ends a LINE) still works.
    bool dyn_handle_key(int key, const QString& text);

    /// The (UI-thread) font engine used to lay out TTF glyphs for the on-canvas
    /// command UI. A DEDICATED instance (not the geometry engine's) so the UI thread
    /// never shares a font face with the geometry thread.
    void set_font_engine(core::IFontEngine* fe) noexcept { font_engine_ = fe; }
    /// True while the on-canvas command-entry box is showing (idle command input).
    [[nodiscard]] bool cmd_entry_active() const noexcept { return cmd_active_; }
    /// Route one key into the idle command-entry box (letters/digits type, Down/Up/Tab
    /// navigate the autocomplete, Enter runs, Esc closes, Backspace edits). Returns true
    /// if consumed. Called from the app-wide event filter when idle + canvas mode.
    bool cmd_entry_handle_key(int key, const QString& text);

    /// True while a command is active but NOT in a dimensional rubber-band -- the
    /// on-canvas sub-prompt cell (FILLET radius, CHAMFER distances, option keywords) is
    /// the input surface for this step.
    [[nodiscard]] bool sub_prompt_active() const;
    /// Route one key into the on-canvas sub-prompt field (printable edits, Enter commits
    /// to the next step, Backspace edits). Returns true if consumed. Esc returns false
    /// (the caller cancels). Called from the app-wide event filter.
    bool sub_prompt_handle_key(int key, const QString& text);

    // --- test / diagnostic accessors (real-window verification) ---
    [[nodiscard]] int cmd_suggestion_count() const noexcept {
        return static_cast<int>(cmd_suggestions_.size());
    }
    [[nodiscard]] std::string cmd_entry_text() const { return cmd_entry_; }
    [[nodiscard]] std::string sub_prompt_value() const { return sub_entry_; }
    [[nodiscard]] int dyn_field_count() const;           ///< 2 (rect/line), 1 (circle), else 0
    [[nodiscard]] int dyn_active_slot() const noexcept { return dyn_active_slot_; }
    [[nodiscard]] std::string dyn_value(int slot) const; ///< typed-or-live shown value
    [[nodiscard]] bool dyn_typing() const;               ///< capturing AND a buffer non-empty
    void dyn_test_type(const std::string& chars);        ///< tests: type into the active field
    void dyn_test_tab();                                 ///< tests: Tab to the next field
    bool dyn_test_commit();                              ///< tests: Enter (compose + submit)
    /// Project a world point to viewport screen pixels (device) -- used by tests to
    /// check a label sits on the rendered geometry.
    [[nodiscard]] core::Vec2 dyn_project(core::Vec2 world) {
        std::scoped_lock lock(camera_mutex_);
        return camera_.world_to_screen(world);
    }


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
    // Per-document camera: each tab's view (zoom/pan) is remembered and restored on
    // switch. Render-thread-only (touched solely in the render loop), under camera_mutex_.
    std::unordered_map<std::uint64_t, render::Camera2D> doc_cameras_;
    std::uint64_t cam_doc_id_ = 0; // active document the camera currently reflects

    std::mutex overlay_mutex_;
    render::RenderOverlay overlay_; // guarded by overlay_mutex_

    // On-canvas Dynamic Input (UI thread). The viewport owns the dimension input:
    // per-slot typed buffers (Length/Width or Length/Angle or Radius), the active
    // slot, and the last constrained cursor (for composing on Enter). Labels are
    // rebuilt into the overlay each frame; a typed value also pins that DOF in the
    // rubber-band (apply_dyn_lock) so the preview reflects it.
    bool dyn_enabled_ = false;
    int dyn_active_slot_ = 0;
    std::array<std::string, 2> dyn_buf_{};
    core::Vec2 dyn_cursor_{}; // last constrained cursor (world)
    [[nodiscard]] bool dyn_dimensional() const; ///< the live preview has on-canvas fields
    bool dyn_commit();                          ///< Enter: compose + submit; false if nothing typed
    void dyn_reset() noexcept {
        dyn_buf_[0].clear();
        dyn_buf_[1].clear();
        dyn_active_slot_ = 0;
    }

    // On-canvas command entry (idle): the typed command text + autocomplete state. Built
    // into overlay_.command_ui each frame; keystrokes routed via the app-wide filter.
    core::IFontEngine* font_engine_ = nullptr; // UI-thread font face for TTF glyph fills
    std::string cmd_font_name_;                // a resolved outline face name (cached)
    std::string cmd_entry_;                    // typed command text
    bool cmd_active_ = false;                  // entry box showing
    core::Vec2 cmd_anchor_px_{};               // device-px anchor (fixed when it appears)
    int cmd_sel_ = 0;                          // highlighted suggestion
    std::vector<command::CommandSuggestion> cmd_suggestions_;
    void cmd_clear() noexcept;
    void cmd_recompute();                       ///< refresh suggestions + reset selection
    [[nodiscard]] std::string cmd_font() ;      ///< the resolved outline face (lazy)
    /// Append TTF glyph triangles (screen px, y-flipped) for `text` at (ox, baseline_y).
    void append_glyphs(const std::string& text, double ox, double baseline_y, double h,
                       std::vector<core::Vec2>& out);
    void build_command_ui(render::CanvasCommandUI& ui); ///< lay out box + dropdown near cursor

    // On-canvas mid-command sub-prompt (FILLET radius, CHAMFER distances, option
    // keywords): the prompt label + an editable value/keyword field at the cursor,
    // shown whenever a command is active but NOT in a dimensional rubber-band.
    std::string sub_entry_; // typed value/keyword for the current sub-prompt
    void build_sub_prompt_ui(render::CanvasCommandUI& ui);

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

    // Multi-document tab list, cached from the snapshot for the GUI thread.
    mutable std::mutex docs_mutex_;
    std::vector<core::DocumentInfo> docs_cache_;
    std::uint64_t active_doc_id_cache_ = 0;

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
    std::function<void()> plot_dialog_callback_;
    std::function<void(bool, core::Vec2, core::Vec2)> plot_pick_callback_; // armed window pick
    bool plot_picking_ = false;
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
