#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <QMainWindow>
#include <QString>

#include "musacad/core/geometry_engine.hpp"
#include "musacad/ui/viewport_modes.hpp"

class QTimer;
class QDockWidget;
class QLabel;
class QAction;
class QToolButton;
class QComboBox;
class QEvent;
class QObject;

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
    /// Real-window self-test: parametric CIRCLE/RECTANGLE/ROTATE dialogs collect +
    /// submit the existing Command; the typed path converges; undo restores.
    bool selftest_param_dialogs();
    /// Real-window self-test: DWG import/export via a MOCK external converter --
    /// discovery, off-thread convert, fail-safe load, gap catalog, export round-trip.
    bool selftest_dwg();

protected:
    /// Application-wide Delete/Backspace handling (erase selection unless a text
    /// field is focused).
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void seed_demo_scene();
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

    // Layers (UI side: dialog + ribbon combo; issues commands, never the store).
    void open_layer_dialog();
    void set_selection_color();
    void sync_layer_combo();
    void toggle_properties();      ///< PR: show/hide the Properties palette dock
    void sync_properties_panel();  ///< push the latest selection summary to the panel
    void set_dyn_enabled(bool on); ///< F12: enable/disable Dynamic Input (persisted)
    void reposition_dyn(double local_px, double local_py); ///< anchor near the cursor
    void refocus_dyn();            ///< re-acquire DYN field focus after a viewport pick

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

    std::unique_ptr<core::GeometryEngine> engine_;
    std::unique_ptr<FanoutOutput> fanout_; // fans prompt/echo to the command line + DYN
    std::unique_ptr<command::CommandProcessor> processor_;
    RibbonBar* ribbon_ = nullptr;
    ViewportWindow* viewport_ = nullptr;          // owned by the window-container widget
    CommandLineWidget* command_widget_ = nullptr; // owned by its dock
    DynInput* dyn_ = nullptr;                     // cursor-anchored Dynamic Input (F12)
    QAction* dyn_action_ = nullptr;
    PropertiesPanel* properties_panel_ = nullptr; // owned by its dock
    QDockWidget* properties_dock_ = nullptr;
    QLabel* coord_label_ = nullptr;
    QTimer* title_timer_ = nullptr;
    std::vector<QToolButton*> selection_required_buttons_;
    std::uint64_t last_status_version_ = 0; // last engine status echoed to the command line
    QString current_path_;                  // path of the open .musa file (empty = untitled)
    QComboBox* layer_combo_ = nullptr;      // ribbon current-layer control

    ViewportModes modes_;
    QAction* osnap_action_ = nullptr;
    QAction* grid_action_ = nullptr;
    QAction* ortho_action_ = nullptr;
    QAction* snap_action_ = nullptr;
    QAction* polar_action_ = nullptr;
};

} // namespace musacad::ui
