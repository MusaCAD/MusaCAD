#pragma once

#include <memory>
#include <string>
#include <vector>

#include <QMainWindow>
#include <QString>

#include "musacad/core/geometry_engine.hpp"
#include "musacad/ui/viewport_modes.hpp"

class QTimer;
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

    // ARRAY command dialog (AutoCAD-style parametric input).
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

    // Persistence (UI side: file dialogs + messages; never touches the store).
    void file_new();
    void file_open();
    void file_save();      // Save (Save As if untitled)
    void file_save_as();
    void file_import_dxf();
    void file_export_dxf();
    void save_to(const QString& path, bool dxf); // testable: send SaveDocumentCommand
    void open_from(const QString& path, bool dxf); // testable: send OpenDocumentCommand
    [[nodiscard]] bool confirm_discard_if_dirty(); // returns true to proceed
    void update_title();

    std::unique_ptr<core::GeometryEngine> engine_;
    std::unique_ptr<command::CommandProcessor> processor_;
    RibbonBar* ribbon_ = nullptr;
    ViewportWindow* viewport_ = nullptr;          // owned by the window-container widget
    CommandLineWidget* command_widget_ = nullptr; // owned by its dock
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
