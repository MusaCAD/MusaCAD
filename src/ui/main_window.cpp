#include "musacad/ui/main_window.hpp"

#include <cmath>
#include <cstdio>

#include <QAbstractButton>
#include <QAction>
#include <QDockWidget>
#include <QKeySequence>
#include <QLabel>
#include <QStatusBar>
#include <QString>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/version.hpp"
#include "musacad/ui/command_icons.hpp"
#include "musacad/ui/command_line_widget.hpp"
#include "musacad/ui/ribbon_bar.hpp"
#include "musacad/ui/viewport_window.hpp"

namespace musacad::ui {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    const auto name = core::app_name();
    setWindowTitle(QString::fromUtf8(name.data(), static_cast<int>(name.size())));
    resize(1320, 860);

    engine_ = std::make_unique<core::GeometryEngine>();
    engine_->start();
    seed_demo_scene();

    viewport_ = new ViewportWindow(*engine_);
    viewport_->set_initial_view({0.0, 0.0}, {100.0, 100.0});
    viewport_->set_modes(&modes_);
    setCentralWidget(build_central());

    // Command-line palette (bottom dock).
    command_widget_ = new CommandLineWidget;
    auto* dock = new QDockWidget(QStringLiteral("Command Line"), this);
    dock->setObjectName(QStringLiteral("CommandDock"));
    dock->setWidget(command_widget_);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::BottomDockWidgetArea, dock);

    processor_ = std::make_unique<command::CommandProcessor>(
        [this](core::Command c) { engine_->submit(std::move(c)); }, viewport_, *command_widget_);
    processor_->set_grid_spacing(10.0);
    command_widget_->set_processor(processor_.get());
    viewport_->set_processor(processor_.get());
    command_widget_->focus_input();

    build_ribbon();
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

QWidget* MainWindow::build_central() {
    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("MusaFrame"));
    central->setAttribute(Qt::WA_StyledBackground, true);
    auto* col = new QVBoxLayout(central);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    // Drawing (file) tabs, just below the ribbon.
    auto* file_tabs = new QTabBar(central);
    file_tabs->setObjectName(QStringLiteral("FileTabs"));
    file_tabs->addTab(QStringLiteral("Drawing1"));
    file_tabs->setExpanding(false);
    col->addWidget(file_tabs);

    // Viewport.
    QWidget* container = QWidget::createWindowContainer(viewport_, central);
    container->setMinimumSize(320, 240);
    container->setFocusPolicy(Qt::StrongFocus);
    col->addWidget(container, 1);

    // Model/Layout tabs, bottom-left.
    auto* layout_tabs = new QTabBar(central);
    layout_tabs->setObjectName(QStringLiteral("LayoutTabs"));
    layout_tabs->addTab(QStringLiteral("Model"));
    layout_tabs->addTab(QStringLiteral("Layout1"));
    layout_tabs->addTab(QStringLiteral("Layout2"));
    layout_tabs->setExpanding(false);
    col->addWidget(layout_tabs);

    return central;
}

void MainWindow::build_ribbon() {
    ribbon_ = new RibbonBar(this);

    // --- Quick Access Toolbar ---
    const auto placeholder_qat = [&](const QString& kind, const QString& tip) {
        auto* a = new QAction(make_icon(kind), tip, this);
        a->setEnabled(false);
        a->setToolTip(tip + QStringLiteral(" (coming soon)"));
        ribbon_->add_qat_action(a);
    };
    placeholder_qat(QStringLiteral("new"), QStringLiteral("New"));
    placeholder_qat(QStringLiteral("open"), QStringLiteral("Open"));
    placeholder_qat(QStringLiteral("save"), QStringLiteral("Save"));

    auto* qat_undo = new QAction(make_icon(QStringLiteral("undo")), QStringLiteral("Undo"), this);
    qat_undo->setShortcut(QKeySequence::Undo);
    qat_undo->setShortcutContext(Qt::ApplicationShortcut);
    connect(qat_undo, &QAction::triggered, this, [this] { processor_->undo(); });
    ribbon_->add_qat_action(qat_undo);

    auto* qat_redo = new QAction(make_icon(QStringLiteral("redo")), QStringLiteral("Redo"), this);
    qat_redo->setShortcut(QKeySequence::Redo);
    qat_redo->setShortcutContext(Qt::ApplicationShortcut);
    connect(qat_redo, &QAction::triggered, this, [this] { processor_->redo(); });
    ribbon_->add_qat_action(qat_redo);

    placeholder_qat(QStringLiteral("print"), QStringLiteral("Print"));

    // Wires a panel button to an existing command (typed alias).
    const auto add_cmd = [&](RibbonPanel* panel, const QString& kind, const QString& label,
                             const char* alias) {
        QToolButton* b = panel->add_button(make_icon(kind), label);
        b->setObjectName(QStringLiteral("ribbon.cmd.%1").arg(QString::fromUtf8(alias)));
        connect(b, &QToolButton::clicked, this, [this, alias] {
            command_widget_->focus_input();
            processor_->submit_line(alias);
        });
    };

    // --- Home tab ---
    const int home = ribbon_->add_tab(QStringLiteral("Home"));
    RibbonPanel* draw = ribbon_->add_panel(home, QStringLiteral("Draw"));
    add_cmd(draw, QStringLiteral("line"), QStringLiteral("Line"), "L");
    add_cmd(draw, QStringLiteral("polyline"), QStringLiteral("Polyline"), "PL");
    add_cmd(draw, QStringLiteral("circle"), QStringLiteral("Circle"), "C");
    add_cmd(draw, QStringLiteral("arc"), QStringLiteral("Arc"), "A");
    add_cmd(draw, QStringLiteral("rectangle"), QStringLiteral("Rectangle"), "REC");

    RibbonPanel* modify = ribbon_->add_panel(home, QStringLiteral("Modify"));
    add_cmd(modify, QStringLiteral("erase"), QStringLiteral("Erase"), "ERASE");
    modify->add_placeholder(make_icon(QStringLiteral("move")), QStringLiteral("Move"));
    modify->add_placeholder(make_icon(QStringLiteral("copy")), QStringLiteral("Copy"));
    modify->add_placeholder(make_icon(QStringLiteral("offset")), QStringLiteral("Offset"));
    modify->add_placeholder(make_icon(QStringLiteral("trim")), QStringLiteral("Trim"));

    RibbonPanel* layers = ribbon_->add_panel(home, QStringLiteral("Layers"));
    layers->add_placeholder(make_icon(QStringLiteral("layers")), QStringLiteral("Layer\nProperties"));

    RibbonPanel* props = ribbon_->add_panel(home, QStringLiteral("Properties"));
    props->add_placeholder(make_icon(QStringLiteral("properties")), QStringLiteral("Properties"));

    RibbonPanel* annot = ribbon_->add_panel(home, QStringLiteral("Annotation"));
    annot->add_placeholder(make_icon(QStringLiteral("dim")), QStringLiteral("Dimension"));
    annot->add_placeholder(make_icon(QStringLiteral("text")), QStringLiteral("Text"));

    // --- Insert tab (placeholder) ---
    const int insert = ribbon_->add_tab(QStringLiteral("Insert"));
    RibbonPanel* block = ribbon_->add_panel(insert, QStringLiteral("Block"));
    block->add_placeholder(make_icon(QStringLiteral("copy")), QStringLiteral("Insert"));

    // --- Annotate tab (Dimensions land in Phase 8) ---
    const int annotate = ribbon_->add_tab(QStringLiteral("Annotate"));
    RibbonPanel* dims = ribbon_->add_panel(annotate, QStringLiteral("Dimensions"));
    dims->add_placeholder(make_icon(QStringLiteral("dim")), QStringLiteral("Linear"));
    dims->add_placeholder(make_icon(QStringLiteral("dim")), QStringLiteral("Aligned"));

    // --- View tab (Zoom Extents is real) ---
    const int view = ribbon_->add_tab(QStringLiteral("View"));
    RibbonPanel* nav = ribbon_->add_panel(view, QStringLiteral("Navigate"));
    QToolButton* zext = nav->add_button(make_icon(QStringLiteral("zoom")), QStringLiteral("Zoom\nExtents"));
    connect(zext, &QToolButton::clicked, this, [this] { viewport_->zoom_extents(); });
    QToolButton* zoom = nav->add_button(make_icon(QStringLiteral("zoom")), QStringLiteral("Zoom"));
    connect(zoom, &QToolButton::clicked, this, [this] {
        command_widget_->focus_input();
        processor_->submit_line("ZOOM");
    });

    // --- Manage tab (placeholder) ---
    const int manage = ribbon_->add_tab(QStringLiteral("Manage"));
    ribbon_->add_panel(manage, QStringLiteral("Customization"))
        ->add_placeholder(make_icon(QStringLiteral("properties")), QStringLiteral("Settings"));

    setMenuWidget(ribbon_);
}

QAction* MainWindow::make_mode_action(const QString& text, int func_key, bool initial) {
    auto* a = new QAction(text, this);
    a->setCheckable(true);
    a->setChecked(initial);
    a->setShortcut(QKeySequence(func_key));
    a->setShortcutContext(Qt::ApplicationShortcut);
    addAction(a); // register so the function-key shortcut works app-wide
    return a;
}

void MainWindow::build_status_bar() {
    osnap_action_ = make_mode_action(QStringLiteral("OSNAP"), Qt::Key_F3, modes_.osnap);
    grid_action_ = make_mode_action(QStringLiteral("GRID"), Qt::Key_F7, modes_.grid);
    ortho_action_ = make_mode_action(QStringLiteral("ORTHO"), Qt::Key_F8, modes_.ortho);
    snap_action_ = make_mode_action(QStringLiteral("SNAP"), Qt::Key_F9, modes_.snap);
    polar_action_ = make_mode_action(QStringLiteral("POLAR"), Qt::Key_F10, modes_.polar);

    connect(osnap_action_, &QAction::toggled, this, [this](bool on) { modes_.osnap = on; });
    connect(grid_action_, &QAction::toggled, this, [this](bool on) { modes_.grid = on; });
    connect(ortho_action_, &QAction::toggled, this, [this](bool on) {
        modes_.ortho = on;
        if (on) {
            polar_action_->setChecked(false);
        }
        processor_->set_ortho(on);
    });
    connect(snap_action_, &QAction::toggled, this, [this](bool on) {
        modes_.snap = on;
        processor_->set_grid_snap(on);
    });
    connect(polar_action_, &QAction::toggled, this, [this](bool on) {
        modes_.polar = on;
        if (on) {
            ortho_action_->setChecked(false);
        }
        processor_->set_polar(on);
    });

    const auto add_toggle = [&](QAction* act) {
        auto* b = new QToolButton(this);
        b->setDefaultAction(act);
        statusBar()->addWidget(b);
    };
    add_toggle(osnap_action_);
    add_toggle(grid_action_);
    add_toggle(ortho_action_);
    add_toggle(snap_action_);
    add_toggle(polar_action_);

    coord_label_ = new QLabel(QStringLiteral("0.000, 0.000"), this);
    coord_label_->setObjectName(QStringLiteral("CoordReadout"));
    statusBar()->addPermanentWidget(coord_label_);
    connect(viewport_, &ViewportWindow::cursorWorldMoved, this, [this](double x, double y) {
        coord_label_->setText(QStringLiteral("%1, %2").arg(x, 0, 'f', 3).arg(y, 0, 'f', 3));
    });
}

namespace {
void dump_widget(QObject* obj, int depth) {
    QString line;
    for (int i = 0; i < depth; ++i) {
        line += QStringLiteral("  ");
    }
    line += QString::fromUtf8(obj->metaObject()->className());
    if (!obj->objectName().isEmpty()) {
        line += QStringLiteral(" #%1").arg(obj->objectName());
    }
    if (auto* b = qobject_cast<QAbstractButton*>(obj); b != nullptr && !b->text().isEmpty()) {
        line += QStringLiteral(" \"%1\"").arg(b->text().replace(QChar('\n'), QChar(' ')));
    }
    if (auto* t = qobject_cast<QTabBar*>(obj)) {
        QString tabs;
        for (int i = 0; i < t->count(); ++i) {
            tabs += (i ? QStringLiteral(", ") : QString()) + t->tabText(i);
        }
        line += QStringLiteral(" [tabs: %1]").arg(tabs);
    }
    std::printf("%s\n", qPrintable(line));
    for (QObject* child : obj->children()) {
        if (qobject_cast<QWidget*>(child) != nullptr) {
            dump_widget(child, depth + 1);
        }
    }
}
} // namespace

void MainWindow::dump_ui() {
    std::printf("===== Ribbon (menu widget) =====\n");
    if (QWidget* mw = menuWidget()) {
        dump_widget(mw, 0);
    }
    std::printf("\n===== Central frame =====\n");
    if (QWidget* c = centralWidget()) {
        dump_widget(c, 0);
    }
    std::printf("\n===== Status bar =====\n");
    dump_widget(statusBar(), 0);

    std::printf("\n===== Ribbon buttons fire existing commands =====\n");
    const auto fire = [this](const char* obj_name, const char* label) {
        auto* btn = findChild<QToolButton*>(QString::fromUtf8(obj_name));
        if (btn == nullptr) {
            std::printf("  %-8s : button not found!\n", label);
            return;
        }
        btn->click();
        std::printf("  %-8s : active=%d, command=%s\n", label, processor_->has_active_command() ? 1 : 0,
                    processor_->last_command().c_str());
        processor_->cancel();
    };
    fire("ribbon.cmd.L", "Line");
    fire("ribbon.cmd.C", "Circle");
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
