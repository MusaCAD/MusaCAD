#pragma once

#include <memory>
#include <vector>

#include <QMainWindow>

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

    std::unique_ptr<core::GeometryEngine> engine_;
    std::unique_ptr<command::CommandProcessor> processor_;
    RibbonBar* ribbon_ = nullptr;
    ViewportWindow* viewport_ = nullptr;          // owned by the window-container widget
    CommandLineWidget* command_widget_ = nullptr; // owned by its dock
    QLabel* coord_label_ = nullptr;
    QTimer* title_timer_ = nullptr;
    std::vector<QToolButton*> selection_required_buttons_;

    ViewportModes modes_;
    QAction* osnap_action_ = nullptr;
    QAction* grid_action_ = nullptr;
    QAction* ortho_action_ = nullptr;
    QAction* snap_action_ = nullptr;
    QAction* polar_action_ = nullptr;
};

} // namespace musacad::ui
