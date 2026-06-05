#pragma once

#include <memory>

#include <QMainWindow>

#include "musacad/core/geometry_engine.hpp"
#include "musacad/ui/viewport_modes.hpp"

class QTimer;
class QLabel;
class QAction;

namespace musacad::command {
class CommandProcessor;
}

namespace musacad::ui {

class ViewportWindow;
class CommandLineWidget;

/// The application's top-level window: an OpenGL viewport (driven by a render
/// thread) hosted in the central area, with the window title showing live FPS.
/// The ribbon, command line, and status bar arrive in later phases.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    void seed_demo_scene();
    void build_toolbar();
    void build_status_bar();
    void update_mode_indicators();

    std::unique_ptr<core::GeometryEngine> engine_;
    std::unique_ptr<command::CommandProcessor> processor_;
    ViewportWindow* viewport_ = nullptr;          // owned by the window-container widget
    CommandLineWidget* command_widget_ = nullptr; // owned by its dock
    QLabel* coord_label_ = nullptr;
    QLabel* mode_label_ = nullptr;
    QTimer* title_timer_ = nullptr;

    ViewportModes modes_;
    QAction* osnap_action_ = nullptr;
    QAction* grid_action_ = nullptr;
    QAction* ortho_action_ = nullptr;
    QAction* snap_action_ = nullptr;
    QAction* polar_action_ = nullptr;
};

} // namespace musacad::ui
