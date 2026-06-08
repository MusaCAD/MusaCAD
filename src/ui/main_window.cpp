#include "musacad/ui/main_window.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include <chrono>
#include <filesystem>
#include <thread>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QDockWidget>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMessageBox>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPalette>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QStyle>
#include <QStatusBar>
#include <QString>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/snap.hpp"
#include "musacad/core/version.hpp"
#include "musacad/ui/command_icons.hpp"
#include "musacad/ui/command_line_widget.hpp"
#include "musacad/ui/layer_dialog.hpp"
#include "musacad/ui/parameter_dialog.hpp"
#include "musacad/ui/ribbon_bar.hpp"
#include "musacad/ui/viewport_window.hpp"

namespace musacad::ui {

namespace {
/// The declarative spec for the ARRAY dialog: a Type selector that gates the
/// Rectangular vs Polar parameter sets.
DialogSpec array_dialog_spec() {
    DialogSpec spec;
    spec.title = "Array";
    spec.controller_key = "type";
    spec.fields = {
        {"type", "Type", FieldType::Choice, 0, {"Rectangular", "Polar"}, 0, false, ""},
        {"rows", "Rows", FieldType::Integer, 2, {}, 0, false, "Rectangular"},
        {"cols", "Columns", FieldType::Integer, 3, {}, 0, false, "Rectangular"},
        {"dx", "Column spacing (X)", FieldType::Number, 10, {}, 0, false, "Rectangular"},
        {"dy", "Row spacing (Y)", FieldType::Number, 10, {}, 0, false, "Rectangular"},
        {"cx", "Center X", FieldType::Number, 0, {}, 0, false, "Polar"},
        {"cy", "Center Y", FieldType::Number, 0, {}, 0, false, "Polar"},
        {"count", "Item count", FieldType::Integer, 6, {}, 0, false, "Polar"},
        {"fill", "Angle to fill (deg)", FieldType::Number, 360, {}, 0, false, "Polar"},
        {"rotate", "Rotate items", FieldType::Bool, 0, {}, 0, true, "Polar"},
    };
    return spec;
}

/// The "Standard" dimension-style editor (minimal; full multi-style manager is
/// staged). All fields always visible.
// Colour-choice palette shared by the dimstyle dialog (index 0 = ByLayer).
const std::array<core::Rgb, 7> kDimColorPalette = {{
    core::Rgb{255, 255, 255}, // 0 ByLayer placeholder (value unused)
    core::Rgb{255, 0, 0},     // Red
    core::Rgb{255, 255, 0},   // Yellow
    core::Rgb{0, 255, 0},     // Green
    core::Rgb{0, 255, 255},   // Cyan
    core::Rgb{0, 128, 255},   // Blue
    core::Rgb{255, 255, 255}, // White
}};
core::ElementColor color_from_choice(int idx) {
    if (idx <= 0) {
        return core::ElementColor{true, {}}; // ByLayer
    }
    const auto i = static_cast<std::size_t>(idx);
    return core::ElementColor{false, i < kDimColorPalette.size() ? kDimColorPalette[i]
                                                                 : core::Rgb{255, 255, 255}};
}

DialogSpec dimstyle_dialog_spec() {
    DialogSpec spec;
    spec.title = "Dimension Style: Standard";
    const std::vector<std::string> colors = {"ByLayer", "Red",  "Yellow", "Green",
                                             "Cyan",    "Blue", "White"};
    spec.fields = {
        {"text_height", "Text height", FieldType::Number, 2.5, {}, 0, false, ""},
        {"arrow_size", "Arrow size", FieldType::Number, 2.5, {}, 0, false, ""},
        {"arrow_type", "Arrow type", FieldType::Choice, 0,
         {"Filled triangle", "Tick", "Open", "Dot"}, 0, false, ""},
        {"precision", "Decimal precision", FieldType::Integer, 2, {}, 0, false, ""},
        {"ext_offset", "Extension offset", FieldType::Number, 0.6, {}, 0, false, ""},
        {"ext_extension", "Extension beyond", FieldType::Number, 1.25, {}, 0, false, ""},
        {"dim_lineweight", "Dim line weight (1/100 mm)", FieldType::Integer, 25, {}, 0, false, ""},
        {"dim_color", "Dimension-line colour", FieldType::Choice, 0, colors, 0, false, ""},
        {"ext_color", "Extension-line colour", FieldType::Choice, 0, colors, 0, false, ""},
        {"text_color", "Text colour", FieldType::Choice, 0, colors, 0, false, ""},
        {"arrow_color", "Arrowhead colour", FieldType::Choice, 0, colors, 0, false, ""},
    };
    return spec;
}
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    const auto name = core::app_name();
    setWindowTitle(QString::fromUtf8(name.data(), static_cast<int>(name.size())));
    resize(1320, 860);

    engine_ = std::make_unique<core::GeometryEngine>();
    engine_->start();
    // Normal launch opens an empty Model space. The demo/benchmark scene is only
    // seeded when MUSACAD_DEMO is set (perf harnesses build their own scenes).
    if (std::getenv("MUSACAD_DEMO") != nullptr) {
        seed_demo_scene();
    }

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

    // Application-wide Delete/Backspace -> erase selection. An event filter on
    // the app catches the key no matter which window holds focus (the native GL
    // viewport included), while still letting the command-line field keep Delete
    // for text editing (see eventFilter()).
    qApp->installEventFilter(this);

    build_ribbon();
    build_status_bar();

    title_timer_ = new QTimer(this);
    connect(title_timer_, &QTimer::timeout, this, [this] {
        update_title();
        // Drive selection-dependent UI from the published selection count.
        const int sel = viewport_->selection_count();
        processor_->set_selection_count(sel);
        processor_->set_hovered_kind(viewport_->hovered_kind()); // smart DIM preview
        for (QToolButton* b : selection_required_buttons_) {
            b->setEnabled(sel > 0);
        }
        sync_layer_combo();
        // Echo each new engine command-result (honest feedback) once.
        const std::uint64_t sv = viewport_->status_version();
        if (sv != last_status_version_) {
            last_status_version_ = sv;
            const std::string msg = viewport_->last_status();
            if (!msg.empty() && command_widget_ != nullptr) {
                command_widget_->append_line(msg);
            }
        }
    });
    title_timer_->start(100);
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
    const auto qat_file = [&](const QString& kind, const QString& tip, QKeySequence shortcut,
                              void (MainWindow::*slot)()) {
        auto* a = new QAction(make_icon(kind), tip, this);
        a->setShortcut(shortcut);
        a->setShortcutContext(Qt::ApplicationShortcut);
        connect(a, &QAction::triggered, this, slot);
        ribbon_->add_qat_action(a);
        addAction(a); // ensure the shortcut is active app-wide
    };
    qat_file(QStringLiteral("new"), QStringLiteral("New"), QKeySequence::New, &MainWindow::file_new);
    qat_file(QStringLiteral("open"), QStringLiteral("Open"), QKeySequence::Open,
             &MainWindow::file_open);
    qat_file(QStringLiteral("save"), QStringLiteral("Save"), QKeySequence::Save,
             &MainWindow::file_save);

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
                             const char* alias) -> QToolButton* {
        QToolButton* b = panel->add_button(make_icon(kind), label);
        b->setObjectName(QStringLiteral("ribbon.cmd.%1").arg(QString::fromUtf8(alias)));
        connect(b, &QToolButton::clicked, this, [this, alias] {
            command_widget_->focus_input();
            processor_->submit_line(alias);
        });
        return b;
    };

    // --- Home tab ---
    const int home = ribbon_->add_tab(QStringLiteral("Home"));

    // File panel: native New/Open/Save/Save As + DXF import/export.
    RibbonPanel* file = ribbon_->add_panel(home, QStringLiteral("File"));
    const auto file_btn = [&](const QString& kind, const QString& label, void (MainWindow::*slot)()) {
        QToolButton* b = file->add_button(make_icon(kind), label);
        connect(b, &QToolButton::clicked, this, slot);
        return b;
    };
    file_btn(QStringLiteral("new"), QStringLiteral("New"), &MainWindow::file_new);
    file_btn(QStringLiteral("open"), QStringLiteral("Open"), &MainWindow::file_open);
    file_btn(QStringLiteral("save"), QStringLiteral("Save"), &MainWindow::file_save);
    file_btn(QStringLiteral("save"), QStringLiteral("Save As"), &MainWindow::file_save_as);
    file_btn(QStringLiteral("open"), QStringLiteral("Import\nDXF"), &MainWindow::file_import_dxf);
    file_btn(QStringLiteral("save"), QStringLiteral("Export\nDXF"), &MainWindow::file_export_dxf);

    // Save As shortcut (Ctrl+Shift+S) -- New/Open/Save shortcuts live on the QAT.
    auto* save_as_act = new QAction(this);
    save_as_act->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    save_as_act->setShortcutContext(Qt::ApplicationShortcut);
    connect(save_as_act, &QAction::triggered, this, &MainWindow::file_save_as);
    addAction(save_as_act);

    RibbonPanel* draw = ribbon_->add_panel(home, QStringLiteral("Draw"));
    add_cmd(draw, QStringLiteral("line"), QStringLiteral("Line"), "L");
    add_cmd(draw, QStringLiteral("polyline"), QStringLiteral("Polyline"), "PL");
    add_cmd(draw, QStringLiteral("circle"), QStringLiteral("Circle"), "C");
    add_cmd(draw, QStringLiteral("arc"), QStringLiteral("Arc"), "A");
    add_cmd(draw, QStringLiteral("rectangle"), QStringLiteral("Rectangle"), "REC");

    RibbonPanel* modify = ribbon_->add_panel(home, QStringLiteral("Modify"));
    add_cmd(modify, QStringLiteral("erase"), QStringLiteral("Erase"), "ERASE");
    QToolButton* move_btn = add_cmd(modify, QStringLiteral("move"), QStringLiteral("Move"), "M");
    QToolButton* copy_btn = add_cmd(modify, QStringLiteral("copy"), QStringLiteral("Copy"), "CO");
    QToolButton* mirror_btn = add_cmd(modify, QStringLiteral("mirror"), QStringLiteral("Mirror"), "MI");
    add_cmd(modify, QStringLiteral("offset"), QStringLiteral("Offset"), "O"); // pick-based
    add_cmd(modify, QStringLiteral("trim"), QStringLiteral("Trim"), "TR");    // pick-based
    QToolButton* rotate_btn = add_cmd(modify, QStringLiteral("rotate"), QStringLiteral("Rotate"), "RO");
    QToolButton* scale_btn = add_cmd(modify, QStringLiteral("scale"), QStringLiteral("Scale"), "SC");
    // ARRAY opens the parametric dialog (typing "AR" still uses the command line).
    QToolButton* array_btn = modify->add_button(make_icon(QStringLiteral("array")),
                                                QStringLiteral("Array"));
    array_btn->setObjectName(QStringLiteral("ribbon.cmd.array"));
    connect(array_btn, &QToolButton::clicked, this, [this] { open_array_dialog(); });
    add_cmd(modify, QStringLiteral("extend"), QStringLiteral("Extend"), "EX");   // pick-based
    add_cmd(modify, QStringLiteral("fillet"), QStringLiteral("Fillet"), "F");    // pick-based
    add_cmd(modify, QStringLiteral("chamfer"), QStringLiteral("Chamfer"), "CHA"); // pick-based
    // Move/Copy/Mirror/Rotate/Scale/Array operate on an existing selection.
    selection_required_buttons_ = {move_btn, copy_btn, mirror_btn, rotate_btn, scale_btn, array_btn};
    for (QToolButton* b : selection_required_buttons_) {
        b->setEnabled(false);
    }

    RibbonPanel* layers = ribbon_->add_panel(home, QStringLiteral("Layers"));
    layer_combo_ = new QComboBox(this);
    layer_combo_->setObjectName(QStringLiteral("CurrentLayerCombo"));
    layer_combo_->setMinimumWidth(120);
    layer_combo_->setToolTip(QStringLiteral("Current layer"));
    connect(layer_combo_, &QComboBox::activated, this, [this](int index) {
        if (index >= 0) {
            engine_->submit(core::SetCurrentLayerCommand{static_cast<std::uint16_t>(index)});
        }
    });
    layers->add_widget(layer_combo_);
    QToolButton* layer_btn = layers->add_button(make_icon(QStringLiteral("layers")),
                                                QStringLiteral("Layer\nProperties"));
    layer_btn->setObjectName(QStringLiteral("ribbon.layer_manager"));
    connect(layer_btn, &QToolButton::clicked, this, [this] { open_layer_dialog(); });

    RibbonPanel* props = ribbon_->add_panel(home, QStringLiteral("Properties"));
    QToolButton* color_btn = props->add_button(make_icon(QStringLiteral("properties")),
                                               QStringLiteral("Set\nColour"));
    color_btn->setObjectName(QStringLiteral("ribbon.set_color"));
    connect(color_btn, &QToolButton::clicked, this, [this] { set_selection_color(); });
    selection_required_buttons_.push_back(color_btn); // colour override needs a selection
    color_btn->setEnabled(false);

    RibbonPanel* annot = ribbon_->add_panel(home, QStringLiteral("Annotation"));
    add_cmd(annot, QStringLiteral("text"), QStringLiteral("Text"), "DT");
    add_cmd(annot, QStringLiteral("dim"), QStringLiteral("Dim"), "DIM");
    add_cmd(annot, QStringLiteral("dim"), QStringLiteral("Linear"), "DLI");
    add_cmd(annot, QStringLiteral("dim"), QStringLiteral("Aligned"), "DAL");
    add_cmd(annot, QStringLiteral("dim"), QStringLiteral("Radius"), "DRA");
    add_cmd(annot, QStringLiteral("dim"), QStringLiteral("Diameter"), "DDI");
    add_cmd(annot, QStringLiteral("dim"), QStringLiteral("Angular"), "DAN");
    add_cmd(annot, QStringLiteral("text"), QStringLiteral("Leader"), "LE");
    QToolButton* dimstyle_btn =
        annot->add_button(make_icon(QStringLiteral("dim")), QStringLiteral("Dim\nStyle"));
    dimstyle_btn->setObjectName(QStringLiteral("ribbon.dimstyle"));
    connect(dimstyle_btn, &QToolButton::clicked, this, [this] { open_dimstyle_dialog(); });
    QToolButton* lwt_btn =
        annot->add_button(make_icon(QStringLiteral("dim")), QStringLiteral("LWT"));
    lwt_btn->setObjectName(QStringLiteral("ribbon.lwt"));
    lwt_btn->setCheckable(true);
    lwt_btn->setChecked(true); // LWDISPLAY defaults on
    connect(lwt_btn, &QToolButton::toggled, this,
            [this](bool on) { engine_->submit(core::SetLineweightDisplayCommand{on}); });

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
        return b;
    };
    QToolButton* osnap_btn = add_toggle(osnap_action_);
    add_toggle(grid_action_);
    add_toggle(ortho_action_);
    add_toggle(snap_action_);
    add_toggle(polar_action_);

    // OSNAP button dropdown: per-type running-osnap toggles (drive snap_mask).
    auto* osnap_menu = new QMenu(this);
    const std::pair<const char*, core::SnapType> kSnapTypes[] = {
        {"Endpoint", core::SnapType::Endpoint},     {"Midpoint", core::SnapType::Midpoint},
        {"Center", core::SnapType::Center},         {"Node", core::SnapType::Node},
        {"Quadrant", core::SnapType::Quadrant},     {"Intersection", core::SnapType::Intersection},
        {"Perpendicular", core::SnapType::Perpendicular}, {"Tangent", core::SnapType::Tangent},
        {"Centroid (Musa)", core::SnapType::Centroid},    {"Nearest", core::SnapType::Nearest}};
    for (const auto& [label, type] : kSnapTypes) {
        QAction* a = osnap_menu->addAction(QString::fromUtf8(label));
        a->setCheckable(true);
        a->setChecked((modes_.snap_mask.load() & core::snap_bit(type)) != 0);
        const std::uint32_t bit = core::snap_bit(type);
        connect(a, &QAction::toggled, this, [this, bit](bool on) {
            std::uint32_t m = modes_.snap_mask.load();
            m = on ? (m | bit) : (m & ~bit);
            modes_.snap_mask.store(m);
        });
    }
    osnap_btn->setMenu(osnap_menu);
    osnap_btn->setPopupMode(QToolButton::MenuButtonPopup);

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

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace) {
            // Block only when the user is actively editing the command-line field
            // (focused AND non-empty). An empty/idle command line -- which is the
            // usual state, since it holds focus by default -- must NOT block the
            // Delete-erases-selection binding.
            const bool typing = command_widget_ != nullptr && command_widget_->is_typing();
            if (!typing && processor_ != nullptr && viewport_ != nullptr &&
                !processor_->has_active_command() && viewport_->selection_count() > 0) {
                processor_->delete_selection();
                return true; // consume: erased the selection
            }
            // actively typing, or nothing selected -> let the key through
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::selftest_delete() {
    using core::AddLineCommand;
    using core::Vec2;
    const auto pump = [](auto pred) {
        for (int i = 0; i < 600; ++i) {
            QCoreApplication::processEvents();
            if (pred()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    };

    // Sends a real Delete key event to whichever widget currently holds focus
    // (mirroring an actual keystroke), so it traverses the app event filter.
    const auto send_delete = [this] {
        QWidget* target = QApplication::focusWidget();
        QObject* receiver = target != nullptr ? static_cast<QObject*>(target) : this;
        QKeyEvent ev(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
        QApplication::sendEvent(receiver, &ev);
    };
    const auto select_two = [&] {
        engine_->submit(AddLineCommand{Vec2{0, 0}, Vec2{10, 0}, 9001});
        engine_->submit(AddLineCommand{Vec2{0, 20}, Vec2{10, 20}, 9002});
        engine_->submit(core::SelectAllCommand{});
        return pump([this] { return viewport_->selection_count() == 2; });
    };

    if (!select_two()) {
        std::printf("[selftest] FAIL: selection did not reach 2\n");
        return false;
    }

    // Realistic main path: the command line holds focus but is EMPTY (the default
    // idle state). Delete must erase the selection.
    command_widget_->debug_set_input(QString());
    command_widget_->focus_input();
    QCoreApplication::processEvents();
    send_delete();
    const bool erased = pump([this] { return viewport_->selection_count() == 0; });
    std::printf("[selftest] Delete erases selection, command line empty+focused: %s\n",
                erased ? "PASS" : "FAIL");

    // Guard: while actively typing in the command line (focused AND non-empty),
    // Delete must edit text, NOT erase the selection.
    if (!select_two()) {
        std::printf("[selftest] FAIL: re-selection did not reach 2\n");
        return false;
    }
    command_widget_->debug_set_input(QStringLiteral("LIN"));
    QCoreApplication::processEvents();
    send_delete();
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    QCoreApplication::processEvents();
    const bool guard_held = viewport_->selection_count() == 2;
    std::printf("[selftest] guard (Delete while typing leaves selection): %s\n",
                guard_held ? "PASS" : "FAIL");
    command_widget_->debug_set_input(QString());

    return erased && guard_held;
}

bool MainWindow::selftest_modify() {
    using core::AddPolylineCommand;
    using core::Vec2;
    const auto pump = [](auto pred) {
        for (int i = 0; i < 1200; ++i) {
            QCoreApplication::processEvents();
            if (pred()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    };
    const auto type = [this](const char* line) { processor_->submit_line(line); };
    bool all = true;

    // A live cursor sets the pick aperture; headless, set one so picks resolve.
    processor_->set_pick_radius(1.0);

    // Start clean (selftest_delete may have left entities behind).
    engine_->submit(core::EraseCommand{core::EraseScope::All});
    pump([this] { return viewport_->line_vertex_count() == 0; });

    // A rectangle is a closed polyline -> 4 segments = 8 line vertices.
    engine_->submit(AddPolylineCommand{{{0, 0}, {10, 0}, {10, 5}, {0, 5}}, true, 7001});
    if (!pump([this] { return viewport_->line_vertex_count() == 8; })) {
        std::printf("[selftest] FAIL: rectangle not drawn\n");
        return false;
    }

    // ARRAY (selection-based) must visibly grow the rendered geometry (8 -> 32).
    engine_->submit(core::SelectAllCommand{});
    pump([this] { return viewport_->selection_count() == 1; });
    processor_->set_selection_count(viewport_->selection_count());
    command_widget_->debug_set_input(QString());
    command_widget_->focus_input();
    type("AR");
    type("R");
    type("2");
    type("2");
    type("20");
    type("20");
    const bool arrayed = pump([this] { return viewport_->line_vertex_count() == 32; });
    std::printf("[selftest] ARRAY reflects in GUI (8->32 verts): %s\n", arrayed ? "PASS" : "FAIL");
    all = all && arrayed;
    processor_->undo();
    pump([this] { return viewport_->line_vertex_count() == 8; });

    // CHAMFER a rectangle corner via the command line: geometry must change
    // (corner -> two vertices => 5-vertex closed polyline = 10 line verts) AND the
    // engine's honest result must reach the command-line scrollback.
    type("CHA");
    type("2");
    type("2");
    type("5,0");    // bottom edge
    type("10,2.5"); // right edge -> corner (10,0)
    const bool chamfered = pump([this] { return viewport_->line_vertex_count() == 10; });
    const bool status_ok = pump([this] {
        return command_widget_->debug_scrollback().find("Chamfered.") != std::string::npos;
    });
    std::printf("[selftest] CHAMFER reflects in GUI + status echoed: %s\n",
                (chamfered && status_ok) ? "PASS" : "FAIL");
    all = all && chamfered && status_ok;
    processor_->undo();
    pump([this] { return viewport_->line_vertex_count() == 8; });

    // Honest failure: picking two non-adjacent edges changes nothing and says so.
    type("CHA");
    type("2");
    type("2");
    type("5,0"); // bottom edge
    type("5,5"); // top edge (opposite, not adjacent)
    const bool honest = pump([this] {
        return command_widget_->debug_scrollback().find("adjacent") != std::string::npos;
    });
    const bool unchanged = viewport_->line_vertex_count() == 8;
    std::printf("[selftest] CHAMFER honest failure (no change + message): %s\n",
                (honest && unchanged) ? "PASS" : "FAIL");
    all = all && honest && unchanged;

    return all;
}

bool MainWindow::selftest_dialog() {
    using core::AddPolylineCommand;
    const auto pump = [](auto pred) {
        for (int i = 0; i < 1200; ++i) {
            QCoreApplication::processEvents();
            if (pred()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    };
    bool all = true;
    processor_->set_pick_radius(1.0);

    engine_->submit(core::EraseCommand{core::EraseScope::All});
    pump([this] { return viewport_->line_vertex_count() == 0; });
    engine_->submit(AddPolylineCommand{{{0, 0}, {10, 0}, {10, 5}, {0, 5}}, true, 8001});
    if (!pump([this] { return viewport_->line_vertex_count() == 8; })) {
        std::printf("[selftest] FAIL: dialog setup rectangle\n");
        return false;
    }
    engine_->submit(core::SelectAllCommand{});
    pump([this] { return viewport_->selection_count() == 1; });
    processor_->set_selection_count(viewport_->selection_count());

    // Rectangular array via the dialog: 2 rows x 3 cols = 6 instances * 8 = 48.
    {
        ParameterDialog dlg(array_dialog_spec(), this);
        dlg.set_choice("type", 0);
        dlg.set_number("rows", 2);
        dlg.set_number("cols", 3);
        dlg.set_number("dx", 20);
        dlg.set_number("dy", 20);
        submit_array_from_dialog(dlg);
    }
    const bool rect_ok = pump([this] { return viewport_->line_vertex_count() == 48; });
    std::printf("[selftest] ARRAY dialog (Rectangular) reflects in GUI: %s\n",
                rect_ok ? "PASS" : "FAIL");
    all = all && rect_ok;
    processor_->undo();
    pump([this] { return viewport_->line_vertex_count() == 8; });

    // Polar array via the dialog: 4 items full circle = 4 instances * 8 = 32.
    {
        ParameterDialog dlg(array_dialog_spec(), this);
        dlg.set_choice("type", 1);
        dlg.set_number("count", 4);
        dlg.set_number("fill", 360);
        submit_array_from_dialog(dlg);
    }
    const bool polar_ok = pump([this] { return viewport_->line_vertex_count() == 32; });
    std::printf("[selftest] ARRAY dialog (Polar) reflects in GUI: %s\n", polar_ok ? "PASS" : "FAIL");
    all = all && polar_ok;

    return all;
}

bool MainWindow::selftest_theme() {
    const auto is_dark = [](const QPalette& pal) {
        return pal.color(QPalette::Window).lightness() < 90;
    };
    const bool app_dark = is_dark(QApplication::palette());
    // Freshly-created dialogs must inherit the dark palette (this is what was
    // rendering light before): a file picker and a message box. (We don't assert
    // the style name -- a set stylesheet wraps it in a proxy with an empty name --
    // the dark palette reaching the dialog is the observable requirement.)
    QFileDialog fd(this);
    fd.setOption(QFileDialog::DontUseNativeDialog, true);
    const bool picker_dark = is_dark(fd.palette());
    QMessageBox mb(this);
    const bool msgbox_dark = is_dark(mb.palette());

    const bool ok = app_dark && picker_dark && msgbox_dark;
    std::printf("[selftest] dark theme reaches dialogs (app=%d picker=%d msgbox=%d): %s\n", app_dark,
                picker_dark, msgbox_dark, ok ? "PASS" : "FAIL");
    return ok;
}

bool MainWindow::selftest_layers() {
    using core::AddLineCommand;
    using core::Vec2;
    const auto pump = [](auto pred) {
        for (int i = 0; i < 1200; ++i) {
            QCoreApplication::processEvents();
            if (pred()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    };
    bool all = true;

    engine_->submit(core::NewDocumentCommand{});
    pump([this] { return viewport_->line_vertex_count() == 0; });

    // Drive a real Layer Manager (the same one the ribbon button opens).
    LayerDialog dlg([this] { return viewport_->layers(); },
                    [this] { return viewport_->current_layer(); },
                    [this](core::Command c) { engine_->submit(std::move(c)); }, this);
    dlg.show();

    const int before = static_cast<int>(viewport_->layers().size()); // 1 (layer "0")
    dlg.test_new_layer();
    const bool added = pump([this, before] {
        return static_cast<int>(viewport_->layers().size()) == before + 1;
    });
    std::printf("[selftest] Layer Manager creates a layer: %s\n", added ? "PASS" : "FAIL");
    all = all && added;

    // Make the new layer current and draw on it.
    dlg.test_set_current_row(1);
    const bool current_ok = pump([this] { return viewport_->current_layer() == 1; });
    engine_->submit(AddLineCommand{Vec2{0, 0}, Vec2{10, 0}, 1});
    const bool drawn = pump([this] { return viewport_->line_vertex_count() == 2; });

    // Turn the layer OFF from the dialog -> the line stops rendering.
    dlg.refresh();
    dlg.test_set_flag(1, /*kOn column*/ 1, false);
    const bool hidden = pump([this] { return viewport_->line_vertex_count() == 0; });
    std::printf("[selftest] Layer Manager toggles a layer off (geometry hides): %s\n",
                (current_ok && drawn && hidden) ? "PASS" : "FAIL");
    all = all && current_ok && drawn && hidden;

    // The Layer Manager dialog uses the dark palette (Phase 11 consistency).
    const bool dark = dlg.palette().color(QPalette::Window).lightness() < 90;
    std::printf("[selftest] Layer Manager dark palette: %s\n", dark ? "PASS" : "FAIL");
    all = all && dark;

    dlg.close();
    engine_->submit(core::NewDocumentCommand{});
    return all;
}

bool MainWindow::selftest_persist() {
    using core::AddLineCommand;
    using core::Vec2;
    const auto pump = [](auto pred) {
        for (int i = 0; i < 1200; ++i) {
            QCoreApplication::processEvents();
            if (pred()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    };
    bool all = true;
    const std::string path =
        (std::filesystem::temp_directory_path() / "musacad_selftest.musa").string();
    const QString qpath = QString::fromStdString(path);

    // Fresh drawing, then draw -> becomes dirty.
    engine_->submit(core::NewDocumentCommand{});
    pump([this] { return viewport_->line_vertex_count() == 0; });
    engine_->submit(AddLineCommand{Vec2{0, 0}, Vec2{10, 0}, 1});
    engine_->submit(AddLineCommand{Vec2{0, 5}, Vec2{10, 5}, 2});
    const bool became_dirty = pump([this] { return viewport_->dirty(); });
    std::printf("[selftest] edit marks drawing modified: %s\n", became_dirty ? "PASS" : "FAIL");
    all = all && became_dirty;
    const int verts = viewport_->line_vertex_count();

    // Save clears the modified flag and the title loses its '*'.
    save_to(qpath, false);
    const bool saved_clean = pump([this] { return !viewport_->dirty(); });
    update_title();
    const bool title_ok = !windowTitle().contains(QLatin1Char('*'));
    std::printf("[selftest] Save clears modified flag + title: %s\n",
                (saved_clean && title_ok) ? "PASS" : "FAIL");
    all = all && saved_clean && title_ok;

    // New empties the drawing (not dirty -> no prompt).
    file_new();
    const bool cleared = pump([this] { return viewport_->line_vertex_count() == 0; });

    // Reopen restores the saved geometry, clean.
    open_from(qpath, false);
    const bool reopened =
        pump([this, verts] { return viewport_->line_vertex_count() == verts && !viewport_->dirty(); });
    std::printf("[selftest] New clears + Open restores from disk: %s\n",
                (cleared && reopened) ? "PASS" : "FAIL");
    all = all && cleared && reopened;

    std::error_code ec;
    std::filesystem::remove(path, ec);
    return all;
}

void MainWindow::sync_layer_combo() {
    if (layer_combo_ == nullptr || viewport_ == nullptr) {
        return;
    }
    const std::vector<core::Layer> layers = viewport_->layers();
    bool changed = static_cast<int>(layers.size()) != layer_combo_->count();
    for (int i = 0; !changed && i < static_cast<int>(layers.size()); ++i) {
        changed = layer_combo_->itemText(i).toStdString() != layers[i].name;
    }
    if (changed) {
        const QSignalBlocker block(layer_combo_);
        layer_combo_->clear();
        for (const core::Layer& l : layers) {
            layer_combo_->addItem(QString::fromStdString(l.name));
        }
    }
    const int cur = static_cast<int>(viewport_->current_layer());
    if (cur >= 0 && cur < layer_combo_->count() && layer_combo_->currentIndex() != cur) {
        const QSignalBlocker block(layer_combo_);
        layer_combo_->setCurrentIndex(cur);
    }
}

void MainWindow::open_layer_dialog() {
    auto* dlg = new LayerDialog([this] { return viewport_->layers(); },
                                [this] { return viewport_->current_layer(); },
                                [this](core::Command c) { engine_->submit(std::move(c)); }, this);
    dlg->setObjectName(QStringLiteral("LayerManager"));
    connect(dlg, &QDialog::finished, dlg, &QObject::deleteLater);
    dlg->show();
}

void MainWindow::set_selection_color() {
    if (viewport_ == nullptr || viewport_->selection_count() == 0) {
        return;
    }
    const QColor c = QColorDialog::getColor(Qt::white, this, QStringLiteral("Object Colour"),
                                            QColorDialog::DontUseNativeDialog);
    if (!c.isValid()) {
        return;
    }
    const std::uint64_t g = processor_->begin_group();
    engine_->submit(core::SetEntityColorCommand{
        false,
        core::Rgb{static_cast<std::uint8_t>(c.red()), static_cast<std::uint8_t>(c.green()),
                  static_cast<std::uint8_t>(c.blue())},
        g});
}

void MainWindow::submit_dimstyle_from_dialog(const ParameterDialog& dlg) {
    core::DimStyle s;
    s.name = "Standard";
    s.text_height = dlg.number("text_height");
    s.arrow_size = dlg.number("arrow_size");
    s.arrow_type = static_cast<std::uint8_t>(dlg.choice_index("arrow_type"));
    s.precision = static_cast<std::uint8_t>(dlg.integer("precision"));
    s.ext_offset = dlg.number("ext_offset");
    s.ext_extension = dlg.number("ext_extension");
    s.dim_lineweight = static_cast<std::uint8_t>(dlg.integer("dim_lineweight"));
    s.dim_color = color_from_choice(dlg.choice_index("dim_color"));
    s.ext_color = color_from_choice(dlg.choice_index("ext_color"));
    s.text_color = color_from_choice(dlg.choice_index("text_color"));
    s.arrow_color = color_from_choice(dlg.choice_index("arrow_color"));
    engine_->submit(core::SetDimStyleCommand{0, s});
}

void MainWindow::open_dimstyle_dialog() {
    auto* dlg = new ParameterDialog(dimstyle_dialog_spec(), this);
    connect(dlg, &QDialog::accepted, this, [this, dlg] { submit_dimstyle_from_dialog(*dlg); });
    connect(dlg, &QDialog::finished, dlg, &QObject::deleteLater);
    dlg->show();
}

bool MainWindow::selftest_annotation() {
    const auto pump = [](auto pred) {
        for (int i = 0; i < 1200; ++i) {
            QCoreApplication::processEvents();
            if (pred()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    };
    const auto type = [this](const char* line) { processor_->submit_line(line); };
    bool all = true;
    processor_->set_pick_radius(1.0);
    engine_->submit(core::NewDocumentCommand{});
    pump([this] { return viewport_->line_vertex_count() == 0; });

    // Place text via the command line.
    type("DT");
    type("5,5");
    type("4");
    type("0");
    type("HELLO 12");
    const bool text_ok = pump([this] { return viewport_->line_vertex_count() > 0; });
    std::printf("[selftest] TEXT command renders: %s\n", text_ok ? "PASS" : "FAIL");
    all = all && text_ok;
    const int after_text = viewport_->line_vertex_count();

    // Place a linear dimension; geometry grows further (ext/dim lines + arrows + label).
    type("DLI");
    type("0,0");
    type("20,0");
    type("10,-5");
    const bool dim_ok = pump([this, after_text] { return viewport_->line_vertex_count() > after_text; });
    std::printf("[selftest] DIMLINEAR command renders: %s\n", dim_ok ? "PASS" : "FAIL");
    all = all && dim_ok;

    // Radius, diameter, angular, and leader each add geometry.
    int prev = viewport_->line_vertex_count();
    const auto place = [&](const char* name, std::initializer_list<const char*> steps) {
        for (const char* s : steps) {
            type(s);
        }
        const bool ok = pump([this, prev] { return viewport_->line_vertex_count() > prev; });
        std::printf("[selftest] %s renders: %s\n", name, ok ? "PASS" : "FAIL");
        all = all && ok;
        prev = viewport_->line_vertex_count();
    };
    place("LEADER", {"LE", "60,60", "70,66", "see note"});

    // --- Object-aware dimensioning (Phase 15): dimension entities by SELECTING
    // them. Draw the source geometry, then pick the objects themselves.
    engine_->submit(core::AddCircleCommand{{40, 0}, 8.0, 0});      // for DIMRADIUS
    engine_->submit(core::AddCircleCommand{{0, 40}, 8.0, 0});      // for DIMDIAMETER
    engine_->submit(core::AddLineCommand{{-20, -20}, {0, -20}, 0});  // angular line 1
    engine_->submit(core::AddLineCommand{{-20, -20}, {-20, 0}, 0});  // angular line 2
    engine_->submit(core::AddLineCommand{{20, 20}, {40, 20}, 0});    // for DIMLINEAR
    engine_->submit(core::AddCircleCommand{{70, 0}, 5.0, 0});        // for smart DIM
    pump([this, prev] { return viewport_->line_vertex_count() > prev; });
    prev = viewport_->line_vertex_count();
    place("DIMRADIUS (select circle)", {"DRA", "48,0", "62,0"});
    place("DIMDIAMETER (select circle)", {"DDI", "8,40", "0,56"});
    place("DIMANGULAR (select two lines)", {"DAN", "-10,-20", "-20,-10", "-14,-14"});
    place("DIMLINEAR (select line)", {"DLI", "O", "30,20", "30,28"});

    // Smart DIM: hover a circle -> it creates a diameter dimension. The hover kind
    // is normally pushed by the UI timer; set it directly here (no real mouse move)
    // right before the pick so it isn't clobbered by an intervening timer tick.
    type("DIM");
    processor_->set_hovered_kind(core::EntityKind::Circle);
    type("75,0"); // pick the circle at (70,0) r=5
    type("88,0"); // placement
    const bool smart_ok = pump([this, prev] { return viewport_->line_vertex_count() > prev; });
    std::printf("[selftest] DIM smart (circle -> diameter) renders: %s\n",
                smart_ok ? "PASS" : "FAIL");
    all = all && smart_ok;
    prev = viewport_->line_vertex_count();

    // A DIMSTYLE change (precision) re-renders the dimensions using it.
    const int before_style = viewport_->line_vertex_count();
    core::DimStyle s;
    s.name = "Standard";
    s.precision = 0;
    s.arrow_color = {false, core::Rgb{255, 0, 0}}; // red arrowheads
    engine_->submit(core::SetDimStyleCommand{0, s});
    const bool restyle =
        pump([this, before_style] { return viewport_->line_vertex_count() != before_style; });
    std::printf("[selftest] DIMSTYLE change re-renders dims: %s\n", restyle ? "PASS" : "FAIL");
    all = all && restyle;

    // LWDISPLAY toggle is accepted (pixel-width proof is in the offscreen harness).
    engine_->submit(core::SetLineweightDisplayCommand{false});
    engine_->submit(core::SetLineweightDisplayCommand{true});
    const bool lwt_ok = pump([this] { return viewport_->line_vertex_count() > 0; });
    std::printf("[selftest] LWDISPLAY toggle accepted: %s\n", lwt_ok ? "PASS" : "FAIL");
    all = all && lwt_ok;

    // The DIMSTYLE dialog (dark) now exposes per-element colour fields.
    ParameterDialog dlg(dimstyle_dialog_spec(), this);
    const bool dark = dlg.palette().color(QPalette::Window).lightness() < 90;
    std::printf("[selftest] DIMSTYLE dialog dark palette: %s\n", dark ? "PASS" : "FAIL");
    all = all && dark;

    engine_->submit(core::NewDocumentCommand{});
    return all;
}

bool MainWindow::selftest_grips() {
    const auto pump = [](auto pred) {
        for (int i = 0; i < 1200; ++i) {
            QCoreApplication::processEvents();
            if (pred()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    };
    bool all = true;
    processor_->set_pick_radius(1.0);
    engine_->submit(core::NewDocumentCommand{});
    pump([this] { return viewport_->line_vertex_count() == 0; });

    using G = core::GripDragCommand;
    using P = core::GripDragCommand::Phase;

    // A line: select it -> grips appear (2 endpoints + midpoint).
    engine_->submit(core::AddLineCommand{{0, 0}, {10, 0}, 0});
    pump([this] { return viewport_->line_vertex_count() > 0; });
    engine_->submit(core::SelectPickCommand{{5, 0}, 1.0, false});
    const bool line_grips = pump([this] { return viewport_->grip_count() == 3; });
    std::printf("[selftest] line selection shows grips: %s\n", line_grips ? "PASS" : "FAIL");
    all = all && line_grips;

    // Drag an endpoint grip: Begin -> Move (transient preview) -> Commit on release.
    int eb = -1;
    for (int i = 0; i < viewport_->grip_count(); ++i) {
        if (viewport_->grip_info(i).index == 1) {
            eb = i;
        }
    }
    bool drag_ok = false;
    if (eb >= 0) {
        const core::GripInfo g = viewport_->grip_info(eb);
        engine_->submit(G{P::Begin, g.handle, g.index, {}, 0});
        engine_->submit(G{P::Move, {}, 0, {10, 8}, 0});
        pump([] { return false; }); // let a few frames publish the preview
        engine_->submit(G{P::Commit, {}, 0, {10, 8}, processor_->begin_group()});
        drag_ok = pump([this] { return viewport_->line_vertex_count() > 0; });
        engine_->submit(core::UndoLastGroupCommand{}); // one undo group restores it
        drag_ok = drag_ok && pump([this] { return viewport_->line_vertex_count() > 0; });
    }
    std::printf("[selftest] line endpoint grip drag commits + undoes: %s\n",
                drag_ok ? "PASS" : "FAIL");
    all = all && drag_ok;

    // A dimension: select it -> grips (def a, def b, dim-line offset) and drag the
    // dim-line-offset grip (the headline ask).
    engine_->submit(core::NewDocumentCommand{});
    pump([this] { return viewport_->line_vertex_count() == 0; });
    engine_->submit(core::AddDimensionCommand{static_cast<std::uint8_t>(core::DimType::Linear),
                                              {0, 0}, {20, 0}, {10, 5}, 0, 0});
    pump([this] { return viewport_->line_vertex_count() > 0; });
    engine_->submit(core::SelectPickCommand{{10, 5}, 2.0, false});
    const bool dim_grips = pump([this] { return viewport_->grip_count() == 3; });
    std::printf("[selftest] dimension shows grips (def + dim-line offset): %s\n",
                dim_grips ? "PASS" : "FAIL");
    all = all && dim_grips;

    bool offset_ok = false;
    int off = -1;
    for (int i = 0; i < viewport_->grip_count(); ++i) {
        if (viewport_->grip_info(i).index == 2) {
            off = i; // the dim-line offset grip
        }
    }
    if (off >= 0) {
        const core::GripInfo g = viewport_->grip_info(off);
        engine_->submit(G{P::Begin, g.handle, g.index, {}, 0});
        engine_->submit(G{P::Move, {}, 0, {10, 12}, 0}); // push the dim line farther
        pump([] { return false; });
        engine_->submit(G{P::Commit, {}, 0, {10, 12}, processor_->begin_group()});
        offset_ok = pump([this] { return viewport_->line_vertex_count() > 0; });
    }
    std::printf("[selftest] dim-line offset grip drag (headline) commits: %s\n",
                offset_ok ? "PASS" : "FAIL");
    all = all && offset_ok;

    engine_->submit(core::NewDocumentCommand{});
    return all;
}

void MainWindow::open_array_dialog() {
    if (viewport_ == nullptr || viewport_->selection_count() == 0) {
        if (command_widget_ != nullptr) {
            command_widget_->append_line("Select objects first, then ARRAY.");
        }
        return;
    }
    auto* dlg = new ParameterDialog(array_dialog_spec(), this);
    connect(dlg, &QDialog::accepted, this, [this, dlg] { submit_array_from_dialog(*dlg); });
    connect(dlg, &QDialog::finished, dlg, &QObject::deleteLater);
    dlg->show();
}

void MainWindow::submit_array_from_dialog(const ParameterDialog& dlg) {
    if (processor_ == nullptr) {
        return;
    }
    processor_->set_selection_count(viewport_->selection_count());
    const std::uint64_t group = processor_->begin_group();
    if (dlg.choice_value("type") == "Polar") {
        processor_->submit(core::ArrayPolarCommand{core::Vec2{dlg.number("cx"), dlg.number("cy")},
                                                   dlg.integer("count"),
                                                   core::to_radians(dlg.number("fill")),
                                                   dlg.boolean("rotate"), group});
    } else {
        processor_->submit(core::ArrayRectCommand{dlg.integer("rows"), dlg.integer("cols"),
                                                  dlg.number("dx"), dlg.number("dy"), group});
    }
}

void MainWindow::update_title() {
    const auto n = core::app_name();
    const QString app = QString::fromUtf8(n.data(), static_cast<int>(n.size()));
    const QString name = current_path_.isEmpty() ? QStringLiteral("Drawing1")
                                                  : QFileInfo(current_path_).fileName();
    const QString mark = (viewport_ != nullptr && viewport_->dirty()) ? QStringLiteral("*")
                                                                      : QString();
    const int fps = viewport_ != nullptr ? static_cast<int>(viewport_->fps() + 0.5) : 0;
    setWindowTitle(QStringLiteral("%1%2  —  %3  —  %4 FPS").arg(name, mark, app).arg(fps));
}

bool MainWindow::confirm_discard_if_dirty() {
    if (viewport_ == nullptr || !viewport_->dirty()) {
        return true;
    }
    const auto choice = QMessageBox::warning(
        this, QStringLiteral("Unsaved changes"),
        QStringLiteral("The current drawing has unsaved changes. Save before continuing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (choice == QMessageBox::Cancel) {
        return false;
    }
    if (choice == QMessageBox::Save) {
        file_save();
    }
    return true;
}

void MainWindow::save_to(const QString& path, bool dxf) {
    if (path.isEmpty()) {
        return;
    }
    if (!dxf) {
        current_path_ = path;
    }
    engine_->submit(core::SaveDocumentCommand{path.toStdString(), dxf});
}

void MainWindow::open_from(const QString& path, bool dxf) {
    if (path.isEmpty()) {
        return;
    }
    if (!dxf) {
        current_path_ = path;
    }
    engine_->submit(core::OpenDocumentCommand{path.toStdString(), dxf});
}

void MainWindow::file_new() {
    if (!confirm_discard_if_dirty()) {
        return;
    }
    current_path_.clear();
    engine_->submit(core::NewDocumentCommand{});
}

void MainWindow::file_open() {
    if (!confirm_discard_if_dirty()) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open Drawing"),
                                                      QString(), QStringLiteral("Musa CAD (*.musa)"),
                                                      nullptr, QFileDialog::DontUseNativeDialog);
    open_from(path, false);
}

void MainWindow::file_save() {
    if (current_path_.isEmpty()) {
        file_save_as();
        return;
    }
    save_to(current_path_, false);
}

void MainWindow::file_save_as() {
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save Drawing As"), QString(),
                                                QStringLiteral("Musa CAD (*.musa)"), nullptr,
                                                QFileDialog::DontUseNativeDialog);
    if (!path.isEmpty() && !path.endsWith(QStringLiteral(".musa"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".musa");
    }
    save_to(path, false);
}

void MainWindow::file_import_dxf() {
    if (!confirm_discard_if_dirty()) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Import DXF"), QString(),
                                                      QStringLiteral("DXF (*.dxf)"), nullptr,
                                                      QFileDialog::DontUseNativeDialog);
    open_from(path, true);
}

void MainWindow::file_export_dxf() {
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export DXF"), QString(),
                                                QStringLiteral("DXF (*.dxf)"), nullptr,
                                                QFileDialog::DontUseNativeDialog);
    if (!path.isEmpty() && !path.endsWith(QStringLiteral(".dxf"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".dxf");
    }
    save_to(path, true);
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
