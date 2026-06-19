// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <QMainWindow>
#include <QString>

#include "musacad/core/geometry_engine.hpp"
#include "musacad/ui/plot.hpp"
#include "musacad/ui/viewport_modes.hpp"

class QTimer;
class QDockWidget;
class QLabel;
class QAction;
class QToolButton;
class QComboBox;
class QEvent;
class QObject;
class QCloseEvent;
class QTabBar;
class QPoint;

namespace musacad::command {
class CommandProcessor;
}

namespace musacad::ui {

class ViewportWindow;
class CommandLineWidget;
class RibbonBar;
class ParameterDialog;
class PropertiesPanel;
class DynInput;
class FanoutOutput;
class QtFontEngine;

/// The application's top-level window: an AutoCAD-2023-style Ribbon frame
/// (Quick Access Toolbar + tabbed ribbon panels + file/layout tabs), an OpenGL
/// viewport in the centre, the command-line palette, and a status bar with mode
/// toggle buttons and the live coordinate readout.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    /// Prints the widget tree and fires the Line/Circle ribbon buttons (for
    /// headless structural verification).
    void dump_ui();

    /// Real-window self-test: select an entity, post a Delete key event, and
    /// confirm it is erased. Returns true on success.
    bool selftest_delete();

    /// Real-window self-test: drive Scale (on a selected polyline) and Chamfer (a
    /// rectangle corner) through the real command line, and confirm the geometry
    /// changes AND the engine's result message reaches the command-line scrollback.
    bool selftest_modify();

    /// Real-window self-test: open the ARRAY dialog, set fields, accept, and
    /// confirm the array reaches the engine and grows the rendered geometry.
    bool selftest_dialog();

    /// Real-window self-test: save the drawing, New (clears + dirty cleared),
    /// reopen it, and confirm geometry returns and the title/dirty flag track.
    bool selftest_persist();

    /// Real-window self-test: the dark palette/style propagate to freshly-created
    /// dialogs (file picker, message box), so the UI is visually consistent.
    bool selftest_theme();

    /// Real-window self-test: the Layer Manager creates a layer, sets it current,
    /// toggles it off (hiding geometry) -- all via the dialog, observed in the
    /// published snapshot.
    bool selftest_layers();

    /// Real-window self-test: place TEXT and a DIMLINEAR via the command line,
    /// confirm they render, and the DIMSTYLE dialog uses the dark palette.
    bool selftest_annotation();

    /// Real-window self-test: grips show on selection, a grip drag previews without
    /// mutating the store, commits on release, undo restores; dim-line-offset drag.
    bool selftest_grips();

    /// Real-window self-test: create MTEXT (two corners + text) and QLEADER (arrow
    /// + vertices + text) via the command line; both render.
    bool selftest_mtext();
    /// Real-window self-test: PR palette multiplicity (none/one/many-same/mixed),
    /// universal + text property edits via the panel, observed store change + undo.
    bool selftest_properties();
    /// Real-window self-test: PR linetype dashes the entity (Ph22 gap closed) and
    /// LTSCALE re-dashes live; Continuous renders solid.
    bool selftest_linetype();
    /// Real-window self-test: PR dimension per-dim overrides (set/reset/undo/multi).
    bool selftest_dim_properties();
    /// Real-window self-test: Dynamic Input (toggle, type-at-cursor, exact length,
    /// and THE focus rule -- Delete/typing-guard/Esc still correct with DYN on).
    bool selftest_dyn();
    /// Real-window capture: drive a RECTANGLE/LINE/CIRCLE rubber-band with DYN on,
    /// print the anchor->project->tip-geometry diagnostic, and grab the app region to
    /// `out_png` for eyes-on verification of the on-geometry tooltips. `kind`: 0 REC,
    /// 1 LINE, 2 CIRCLE. Returns true if the tips landed on their geometry anchors.
    bool dyn_shot(int kind, const std::string& out_png);
    /// Real-window capture for the OFFSET-polyline fix + JOIN. Builds geometry, runs the
    /// op, zooms to extents, and grabs the app region. `kind`: 0 rectangle offset inward,
    /// 1 filleted-rectangle offset, 2 open-polyline offset, 3 over-large offset (graceful
    /// failure message), 4 JOIN four lines -> closed polyline -> uniform offset.
    bool offset_shot(int kind, const std::string& out_png);
    /// Real-window capture for multi-document (Phase A). `kind`: 0 two tabs (content +
    /// empty, dirty markers), 1 per-tab view preserved across a switch, 2 close-dirty
    /// Save/Discard/Cancel prompt, 3 Open makes a new tab (original intact), 4 undo
    /// per-tab. Sets up the scenario, then grabs the app region for eyes-on review.
    bool multidoc_shot(int kind, const std::string& out_png);
    /// Real-window capture for the TEXT-QUALITY phase: builds a representative title
    /// block (border + mixed-case single-line TEXT + a lowercase MTEXT note), zooms to
    /// fit, saves the scene to `<out_png>.musa` for the plot-unchanged proof, and grabs
    /// the app region. `kind` 0 = title block. Used for the before/after stroke-text
    /// comparison; the scene is rendering-neutral (same geometry before and after).
    bool text_shot(int kind, const std::string& out_png);
    /// Real-window capture for MATCHPROP / MA. `kind`: 0 cross-kind universal (line ->
    /// circle), 1 within text family (TEXT -> MTEXT), 2 within dim family (overrides), 3
    /// the Settings dialog (Color unchecked), 4 skips inapplicable (TEXT -> LINE, universal
    /// only). Builds the scene, runs the match, zooms, and grabs the app region.
    bool matchprop_shot(int kind, const std::string& out_png);
    /// Real-window capture for LTSCALE / CELTSCALE. `kind`: 0 Center geometry at LTSCALE 1.0
    /// (before), 1 same at LTSCALE 0.5 (after), 2 three short Center lines with the middle
    /// at CELTSCALE 0.3, 3 the user's ~22-unit Center line solid vs CELTSCALE 0.25. Saves
    /// the scene to "<out>.musa" for the plot-consistency proof.
    bool ltscale_shot(int kind, const std::string& out_png);
    /// Real-window self-test: parametric CIRCLE/RECTANGLE/ROTATE dialogs collect +
    /// submit the existing Command; the typed path converges; undo restores.
    bool selftest_param_dialogs();
    /// Real-window self-test: DWG import/export via a MOCK external converter --
    /// discovery, off-thread convert, fail-safe load, gap catalog, export round-trip.
    bool selftest_dwg();
    /// Headless plot of a real file through the EXACT app path (engine load -> prepare_plot
    /// -> paint_plot to PDF), bypassing the file dialogs. `area`: 0 Display, 1 Extents,
    /// 2 Window. For diagnosing plot output without the GUI.
    bool selftest_plot_file(const QString& in_path, const QString& out_pdf, int area);
    /// Headless reproduction of the REAL Ctrl+P GUI path: open the file, resolve the spec
    /// exactly as open_plot_dialog (orientation + the dialog's set/get round-trip), plot the
    /// dialog's INITIAL state, and assert the painted geometry maps within the page.
    bool selftest_gui_plot_file(const QString& in_path, const QString& out_pdf);

protected:
    /// Application-wide Delete/Backspace handling (erase selection unless a text
    /// field is focused).
    bool eventFilter(QObject* watched, QEvent* event) override;
    /// Quit guard: prompt to save every dirty document before the window closes.
    void closeEvent(QCloseEvent* event) override;

private:
    void seed_demo_scene();

    // --- multi-document tab strip (Phase A) --------------------------------
    void sync_document_tabs();                  ///< mirror the engine's doc list into FileTabs
    void create_new_tab();                      ///< File->New / "+" : a new untitled tab
    void switch_to_document(std::uint64_t id);  ///< click/Ctrl+Tab: cancel-on-switch + Switch cmd
    void close_document_tab(std::uint64_t id);  ///< ×/Ctrl+W: dirty prompt, then Close cmd
    void cycle_document(int dir);               ///< Ctrl+Tab (+1) / Ctrl+Shift+Tab (-1)
    /// Tab-to-tab drag: if `global_pos` lands on a different document's tab, transfer the
    /// current selection there (copy -> switch -> paste). Returns true if it transferred.
    [[nodiscard]] bool drop_selection_on_tab(QPoint global_pos);
    /// Prompt Save/Discard/Cancel for one document (queues the Save). Returns false on Cancel.
    bool prompt_save_document(std::uint64_t id, const QString& name, const QString& path);
    [[nodiscard]] QString active_doc_path() const; ///< the active document's native path ("")
    [[nodiscard]] QString active_doc_name() const; ///< the active document's display name
    void build_ribbon();
    QWidget* build_central();
    void build_status_bar();
    QAction* make_mode_action(const QString& text, int func_key, bool initial);

    // ARRAY command dialog (AutoCAD-style parametric input). Draw/transform commands
    // are interactive (ribbon starts the command, pick on screen); the cursor value
    // surface is Dynamic Input (Ph25), which mirrors the command line.
    void open_array_dialog();
    void submit_array_from_dialog(const ParameterDialog& dlg);

    // Double-click text editor: a dark-themed modal editor pre-filled with the
    // entity's content; on confirm submits an EditTextContentCommand at (wx,wy).
    void open_text_editor(double wx, double wy, double pick_radius, const std::string& content,
                          bool multiline);

    // Annotation (UI side: dimension-style dialog).
    void open_dimstyle_dialog();
    void submit_dimstyle_from_dialog(const ParameterDialog& dlg);

    // MATCHPROP Settings: modal category dialog (dark palette) opened via "S" at the
    // destination prompt; choices persist in QSettings for the session. read_match_filter
    // is what the running MATCHPROP command reads for each target (via the viewport).
    void open_matchprop_dialog();
    [[nodiscard]] core::MatchPropFilter read_match_filter() const;
    void write_match_filter(const core::MatchPropFilter& f);

    // Layers (UI side: dialog + ribbon combo; issues commands, never the store).
    void open_layer_dialog();
    void set_selection_color();
    void sync_layer_combo();
    void toggle_properties();      ///< PR: show/hide the Properties palette dock
    void sync_properties_panel();  ///< push the latest selection summary to the panel
    void set_dyn_enabled(bool on); ///< F12: enable/disable Dynamic Input (persisted)
    void reposition_dyn(double local_px, double local_py); ///< anchor near the cursor
    void refocus_dyn();            ///< re-acquire DYN field focus after a viewport pick
    /// Pick the active DYN surface: during a tip-driven rubber-band (RECT/LINE/CIRCLE)
    /// the on-geometry tooltips take over and the cursor box hides; otherwise (keyword
    /// entry / idle / commands without tips) the box shows. One place to type per step.
    void update_dyn_surfaces();

    // Persistence (UI side: file dialogs + messages; never touches the store).
    void file_new();
    void file_open();
    void file_save();      // Save (Save As if untitled)
    void file_save_as();
    void file_import_dxf();
    void file_export_dxf();
    // DWG via an external converter subprocess (LGPL-clean: never linked/bundled).
    // Import = convert DWG->DXF then the existing fail-safe DXF load; export =
    // existing DXF export then convert DXF->DWG. Conversion runs off the UI thread.
    void file_import_dwg();
    void file_export_dwg();
    /// "DWG Converter Setup" dialog: shows the detected converter, lets the user
    /// Browse to one / auto-detect on PATH / open the download pages, and saves the
    /// io/dwg_converter_path setting. (No auto-download: a GPL/proprietary converter
    /// can't be fetched+installed for the user -- licensing/EULA/platform/security.)
    void configure_dwg_converter();
    /// No-converter dead-end recovery: show the hint with a "Configure…" button.
    /// Returns true if the user chose to configure (caller should re-discover).
    bool offer_dwg_setup(const QString& title);

    // --- Plot / print (Phase 30) -------------------------------------------
    /// Open the PLOT dialog (PDF + printers). Wired to PLOT/PRINT + Ctrl+P + the ribbon.
    void show_about(); // Help -> About: logo + name + version + build stamp + LGPL
    void open_plot_dialog();
    void open_plot_dialog_seeded(const PlotSpec& seed); // re-opens after a window pick
    /// Resolve the plot area to a world rect and build the fine plot snapshot. Returns
    /// false (with a message) if there's nothing to plot.
    [[nodiscard]] bool prepare_plot(const PlotSpec& spec, core::Vec2& amin, core::Vec2& amax);
    /// Render the plot to its target (PDF file or printer) off the UI thread, fail-safe.
    void do_plot(const PlotSpec& spec);
    /// Render a raster preview of the plot (reuses paint_plot) into a small dialog.
    void plot_preview(const PlotSpec& spec);
    // Saved page setups (Phase 30 Part C): names for the dialog, save current, recall.
    [[nodiscard]] std::vector<std::string> page_setup_names();
    void save_page_setup(const PlotSpec& spec);
    [[nodiscard]] bool recall_page_setup(const std::string& name, PlotSpec& out);
    PlotSpec last_plot_spec_; // remembered between dialog opens
    /// Run `work` (a blocking converter call) on a worker thread behind a modal
    /// indeterminate progress dialog while the UI stays responsive. Returns work's
    /// result; fills `err`.
    bool run_with_progress(const QString& label, const std::function<bool(QString&)>& work,
                           QString& err);
    /// Pump the event loop behind a modal indeterminate progress dialog until `done`
    /// returns true or `timeout_ms` elapses. For async waits the geometry thread drives
    /// (a big DXF parse/index) so the window isn't a frozen blank during a slow load.
    bool pump_with_progress(const QString& label, const std::function<bool()>& done,
                            int timeout_ms);
    void save_to(const QString& path, bool dxf); // testable: send SaveDocumentCommand
    void open_from(const QString& path, bool dxf); // testable: send OpenDocumentCommand
    [[nodiscard]] bool confirm_discard_if_dirty(); // returns true to proceed
    void update_title();

    std::unique_ptr<QtFontEngine> font_engine_; // before engine_: outlives the geometry thread
    std::unique_ptr<QtFontEngine> ui_font_engine_; // UI-thread font for the on-canvas command UI
    std::unique_ptr<core::GeometryEngine> engine_;
    std::unique_ptr<FanoutOutput> fanout_; // fans prompt/echo to the command line + DYN
    std::unique_ptr<command::CommandProcessor> processor_;
    RibbonBar* ribbon_ = nullptr;
    ViewportWindow* viewport_ = nullptr;          // owned by the window-container widget
    QWidget* viewport_container_ = nullptr;       // createWindowContainer host (reliable mapToGlobal)
    CommandLineWidget* command_widget_ = nullptr; // owned by its dock
    QDockWidget* command_dock_ = nullptr;         // bottom command-line dock (hidden in canvas-only DYN)
    DynInput* dyn_ = nullptr;                     // cursor-anchored Dynamic Input (F12)
    QAction* dyn_action_ = nullptr;
    PropertiesPanel* properties_panel_ = nullptr; // owned by its dock
    QDockWidget* properties_dock_ = nullptr;
    QLabel* coord_label_ = nullptr;
    QTimer* title_timer_ = nullptr;
    std::vector<QToolButton*> selection_required_buttons_;
    std::uint64_t last_status_version_ = 0; // last engine status echoed to the command line
    QTabBar* file_tabs_ = nullptr;          // multi-document tab strip (mirrors the engine)
    core::Vec2 last_cursor_world_{};        // latest cursor world pos (paste-at-cursor)
    QComboBox* layer_combo_ = nullptr;      // ribbon current-layer control

    ViewportModes modes_;
    QAction* osnap_action_ = nullptr;
    QAction* grid_action_ = nullptr;
    QAction* ortho_action_ = nullptr;
    QAction* snap_action_ = nullptr;
    QAction* polar_action_ = nullptr;
};

} // namespace musacad::ui
