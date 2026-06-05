#include "musacad/ui/main_window.hpp"

#include <cmath>

#include <QAction>
#include <QDockWidget>
#include <QKeySequence>
#include <QLabel>
#include <QStatusBar>
#include <QString>
#include <QTimer>
#include <QToolBar>
#include <QWidget>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/version.hpp"
#include "musacad/ui/command_line_widget.hpp"
#include "musacad/ui/viewport_window.hpp"

namespace musacad::ui {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    const auto name = core::app_name();
    setWindowTitle(QString::fromUtf8(name.data(), static_cast<int>(name.size())));
    resize(1280, 800);

    engine_ = std::make_unique<core::GeometryEngine>();
    engine_->start();
    seed_demo_scene();

    // Central viewport.
    viewport_ = new ViewportWindow(*engine_);
    viewport_->set_initial_view({0.0, 0.0}, {100.0, 100.0});
    viewport_->set_modes(&modes_);
    QWidget* container = QWidget::createWindowContainer(viewport_, this);
    container->setMinimumSize(320, 240);
    container->setFocusPolicy(Qt::StrongFocus);
    setCentralWidget(container);

    // Bottom-docked command line.
    command_widget_ = new CommandLineWidget;
    auto* dock = new QDockWidget(QStringLiteral("Command Line"), this);
    dock->setWidget(command_widget_);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::BottomDockWidgetArea, dock);

    processor_ = std::make_unique<command::CommandProcessor>(
        [this](core::Command c) { engine_->submit(std::move(c)); }, viewport_, *command_widget_);
    processor_->set_grid_spacing(10.0);
    command_widget_->set_processor(processor_.get());
    viewport_->set_processor(processor_.get());
    command_widget_->focus_input();

    build_toolbar();
    build_status_bar();

    title_timer_ = new QTimer(this);
    connect(title_timer_, &QTimer::timeout, this, [this] {
        const auto n = core::app_name();
        setWindowTitle(QStringLiteral("%1  —  %2 FPS")
                           .arg(QString::fromUtf8(n.data(), static_cast<int>(n.size())))
                           .arg(static_cast<int>(viewport_->fps() + 0.5)));
    });
    title_timer_->start(250);
}

MainWindow::~MainWindow() {
    delete takeCentralWidget(); // stops the viewport render thread before the engine
    if (engine_) {
        engine_->stop();
    }
}

void MainWindow::build_toolbar() {
    auto* bar = addToolBar(QStringLiteral("Draw"));
    bar->setObjectName(QStringLiteral("DrawToolbar"));

    const auto add_command = [&](const QString& text, const char* alias) {
        QAction* a = bar->addAction(text);
        connect(a, &QAction::triggered, this, [this, alias] {
            command_widget_->focus_input();
            processor_->submit_line(alias);
        });
    };
    add_command(QStringLiteral("Line"), "L");
    add_command(QStringLiteral("Circle"), "C");
    add_command(QStringLiteral("Rectangle"), "REC");
    add_command(QStringLiteral("Polyline"), "PL");
    add_command(QStringLiteral("Arc"), "A");
    add_command(QStringLiteral("Erase"), "ERASE");
    bar->addSeparator();

    QAction* undo = bar->addAction(QStringLiteral("Undo"));
    undo->setShortcut(QKeySequence::Undo); // Ctrl+Z
    undo->setShortcutContext(Qt::ApplicationShortcut);
    connect(undo, &QAction::triggered, this, [this] { processor_->undo(); });

    QAction* redo = bar->addAction(QStringLiteral("Redo"));
    redo->setShortcut(QKeySequence::Redo); // Ctrl+Y
    redo->setShortcutContext(Qt::ApplicationShortcut);
    connect(redo, &QAction::triggered, this, [this] { processor_->redo(); });
    bar->addSeparator();

    // Mode toggles with classic function-key shortcuts.
    const auto add_toggle = [&](const QString& text, const QKeySequence& key, bool initial) {
        QAction* a = bar->addAction(text);
        a->setCheckable(true);
        a->setChecked(initial);
        a->setShortcut(key);
        a->setShortcutContext(Qt::ApplicationShortcut);
        return a;
    };
    osnap_action_ = add_toggle(QStringLiteral("OSNAP"), QKeySequence(Qt::Key_F3), modes_.osnap);
    grid_action_ = add_toggle(QStringLiteral("GRID"), QKeySequence(Qt::Key_F7), modes_.grid);
    ortho_action_ = add_toggle(QStringLiteral("ORTHO"), QKeySequence(Qt::Key_F8), modes_.ortho);
    snap_action_ = add_toggle(QStringLiteral("SNAP"), QKeySequence(Qt::Key_F9), modes_.snap);
    polar_action_ = add_toggle(QStringLiteral("POLAR"), QKeySequence(Qt::Key_F10), modes_.polar);

    connect(osnap_action_, &QAction::toggled, this, [this](bool on) {
        modes_.osnap = on;
        update_mode_indicators();
    });
    connect(grid_action_, &QAction::toggled, this, [this](bool on) {
        modes_.grid = on;
        update_mode_indicators();
    });
    connect(ortho_action_, &QAction::toggled, this, [this](bool on) {
        modes_.ortho = on;
        if (on) {
            polar_action_->setChecked(false); // ortho and polar are mutually exclusive
        }
        processor_->set_ortho(on);
        update_mode_indicators();
    });
    connect(snap_action_, &QAction::toggled, this, [this](bool on) {
        modes_.snap = on;
        processor_->set_grid_snap(on);
        update_mode_indicators();
    });
    connect(polar_action_, &QAction::toggled, this, [this](bool on) {
        modes_.polar = on;
        if (on) {
            ortho_action_->setChecked(false);
        }
        processor_->set_polar(on);
        update_mode_indicators();
    });
}

void MainWindow::build_status_bar() {
    coord_label_ = new QLabel(QStringLiteral("0.000, 0.000"), this);
    mode_label_ = new QLabel(this);
    statusBar()->addPermanentWidget(mode_label_);
    statusBar()->addPermanentWidget(coord_label_);
    update_mode_indicators();

    connect(viewport_, &ViewportWindow::cursorWorldMoved, this, [this](double x, double y) {
        coord_label_->setText(QStringLiteral("%1, %2").arg(x, 0, 'f', 3).arg(y, 0, 'f', 3));
    });
}

void MainWindow::update_mode_indicators() {
    const auto tag = [](const QString& name, bool on) {
        return on ? QStringLiteral("<b>%1</b>").arg(name)
                  : QStringLiteral("<span style='color:gray'>%1</span>").arg(name);
    };
    mode_label_->setText(tag(QStringLiteral("OSNAP"), modes_.osnap) + "  " +
                         tag(QStringLiteral("GRID"), modes_.grid) + "  " +
                         tag(QStringLiteral("ORTHO"), modes_.ortho) + "  " +
                         tag(QStringLiteral("SNAP"), modes_.snap) + "  " +
                         tag(QStringLiteral("POLAR"), modes_.polar));
}

void MainWindow::seed_demo_scene() {
    using core::AddLineCommand;
    using core::Vec2;

    for (int i = 0; i <= 100; i += 10) {
        const double f = static_cast<double>(i);
        engine_->submit(AddLineCommand{Vec2{0.0, f}, Vec2{100.0, f}});
        engine_->submit(AddLineCommand{Vec2{f, 0.0}, Vec2{f, 100.0}});
    }
    const Vec2 center{50.0, 50.0};
    constexpr int kRays = 240;
    for (int i = 0; i < kRays; ++i) {
        const double a = (static_cast<double>(i) / kRays) * 2.0 * 3.14159265358979;
        engine_->submit(AddLineCommand{center, center + Vec2{40.0 * std::cos(a), 40.0 * std::sin(a)}});
    }
}

} // namespace musacad::ui
