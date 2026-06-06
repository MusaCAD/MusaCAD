#pragma once

#include <memory>
#include <vector>

#include <QMainWindow>
#include <QString>

#include "musacad/core/geometry_engine.hpp"
#include "musacad/ui/viewport_modes.hpp"

class QTimer;
class QLabel;
class QAction;
class QToolButton;
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

    ViewportModes modes_;
    QAction* osnap_action_ = nullptr;
    QAction* grid_action_ = nullptr;
    QAction* ortho_action_ = nullptr;
    QAction* snap_action_ = nullptr;
    QAction* polar_action_ = nullptr;
};

} // namespace musacad::ui
