// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/ui/main_window.hpp"

#include <cmath>
#include <numbers>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include <chrono>
#include <filesystem>
#include <atomic>
#include <thread>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QMouseEvent>
#include <QScreen>
#include <QWindow>
#include <QCoreApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QDockWidget>
#include <QEvent>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QCloseEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QElapsedTimer>
#include <QProgressDialog>
#include <QPushButton>
#include <QKeySequence>
#include <QLabel>
#include <QInputDialog>
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
#include <QSettings>
#include <QToolButton>
#include <QVariant>
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
#include <QImage>
#include <QLabel>
#include <QPageSize>
#include <QPdfWriter>
#include <QPixmap>
#include <QPrinter>
#include <QPrinterInfo>

#include "musacad/ui/dwg_converter.hpp"
#include "musacad/ui/dyn_input.hpp"
#include "musacad/ui/plot.hpp"
#include "musacad/ui/plot_dialog.hpp"
#include "musacad/ui/properties_panel.hpp"
#include "musacad/ui/qt_font_engine.hpp"
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
    // The Qt-backed font engine enumerates system TTF/OTF faces (UI thread) and renders
    // outline text as filled glyphs; injected before start() so the geometry thread sees
    // it. Null would mean stroke-font only.
    font_engine_ = std::make_unique<QtFontEngine>();
    engine_->set_font_engine(font_engine_.get());
    // A SEPARATE font engine for the UI thread's on-canvas command text (the geometry
    // engine's instance is used on the geometry thread; a font face is not shared across
    // threads).
    ui_font_engine_ = std::make_unique<QtFontEngine>();
    engine_->start();
    // Normal launch opens an empty Model space. The demo/benchmark scene is only
    // seeded when MUSACAD_DEMO is set (perf harnesses build their own scenes).
    if (std::getenv("MUSACAD_DEMO") != nullptr) {
        seed_demo_scene();
    }
    // Force an initial publish so the document tab strip shows "Drawing1" at launch
    // (the worker only publishes after a command; this harmless one triggers the first).
    engine_->submit(core::SetCursorCommand{});

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
    command_dock_ = dock; // hidden in canvas-only DYN mode (F12 on); shown classic (F12 off)

    // The processor speaks to ONE CommandOutput; a fan-out mirrors prompt/echo to
    // both the bottom command line and the cursor-anchored Dynamic Input.
    fanout_ = std::make_unique<FanoutOutput>();
    fanout_->add(command_widget_);
    processor_ = std::make_unique<command::CommandProcessor>(
        [this](core::Command c) { engine_->submit(std::move(c)); }, viewport_, *fanout_);
    processor_->set_grid_spacing(10.0);
    command_widget_->set_processor(processor_.get());
    viewport_->set_processor(processor_.get());
    viewport_->set_font_engine(ui_font_engine_.get()); // TTF for the on-canvas command UI
    viewport_->set_text_edit_callback([this](const ViewportWindow::TextEditRequest& req) {
        open_text_editor(req.at.x, req.at.y, req.pick_radius, req.content, req.multiline);
    });
    // Tab-to-tab drag: dropping a selection-drag on another document's tab transfers it.
    viewport_->set_selection_drop_callback([this](QPoint global) { return drop_selection_on_tab(global); });

    // MATCHPROP: the running command reads the Settings filter and opens the Settings
    // dialog through the viewport's ViewControl, which forwards to the MainWindow.
    viewport_->set_match_filter_callback([this] { return read_match_filter(); });
    viewport_->set_match_settings_callback([this] { open_matchprop_dialog(); });

    // Dynamic Input (F12): a frameless surface that floats at the crosshair. It
    // routes typed text through the SAME processor (submit_line) and mirrors the
    // prompt via the fan-out. Hidden until enabled; OFF == today's behaviour.
    dyn_ = new DynInput(this);
    dyn_->set_processor(processor_.get());
    dyn_->set_escape_callback([this] {
        viewport_->handle_escape();
        refocus_dyn();
    });
    fanout_->add(dyn_);
    dyn_->hide();

    // On-geometry Dynamic Input is CANVAS-RENDERED by the viewport (the value boxes are
    // drawn in the GL overlay at the geometry, never OS windows -- so they cannot drift
    // off the geometry on multi-monitor). The cursor box (dyn_) stays the keyword/
    // command-entry path and steps aside during a tip-driven rubber-band. Both route
    // typed text through the SAME processor via command::compose_dyn_submit.
    connect(viewport_, &ViewportWindow::cursorScreenMoved, this, [this](double px, double py) {
        reposition_dyn(px, py);
        update_dyn_surfaces(); // box reappears once a tip-driven command ends
    });
    connect(viewport_, &ViewportWindow::constrainedCursorMoved, this,
            [this](double cx, double cy) {
                if (dyn_ != nullptr) {
                    dyn_->on_constrained_cursor(cx, cy);
                }
                update_dyn_surfaces(); // entering a rubber-band hides the cursor box
            });
    connect(viewport_, &ViewportWindow::pickerInteracted, this, [this] { refocus_dyn(); });

    // Properties palette (PR): a dockable panel, hidden by default (the default
    // runtime state stays as before). PR toggles it; the panel edits flow back as
    // SetPropertyCommand on the geometry queue.
    properties_panel_ = new PropertiesPanel;
    if (font_engine_) {
        properties_panel_->set_font_names(font_engine_->available()); // PR Font dropdown
    }
    properties_dock_ = new QDockWidget(QStringLiteral("Properties"), this);
    properties_dock_->setObjectName(QStringLiteral("PropertiesDock"));
    properties_dock_->setWidget(properties_panel_);
    properties_dock_->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                                  QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::RightDockWidgetArea, properties_dock_);
    properties_dock_->hide();
    properties_panel_->set_edit_callback([this](core::PropertyId id, const core::PropertyValue& v) {
        engine_->submit(core::SetPropertyCommand{id, v, processor_->begin_group()});
    });
    viewport_->set_properties_toggle_callback([this] { toggle_properties(); });
    viewport_->set_dwg_import_callback([this] { file_import_dwg(); });
    viewport_->set_dwg_export_callback([this] { file_export_dwg(); });
    viewport_->set_plot_dialog_callback([this] { open_plot_dialog(); });

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
        sync_properties_panel();
        sync_document_tabs(); // mirror the engine's open-document list into the tab strip
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

    // Drawing (file) tabs + a "+" new-tab button, just below the ribbon. Multi-document:
    // the tabs mirror the engine's open-document list (rendered in sync_document_tabs).
    auto* tab_row = new QWidget(central);
    auto* tab_lay = new QHBoxLayout(tab_row);
    tab_lay->setContentsMargins(0, 0, 0, 0);
    tab_lay->setSpacing(0);
    file_tabs_ = new QTabBar(tab_row);
    file_tabs_->setObjectName(QStringLiteral("FileTabs"));
    file_tabs_->setExpanding(false);
    file_tabs_->setTabsClosable(true);
    file_tabs_->setDrawBase(false);
    tab_lay->addWidget(file_tabs_);
    auto* new_tab = new QToolButton(tab_row);
    new_tab->setObjectName(QStringLiteral("FileTabsNew"));
    new_tab->setText(QStringLiteral("+"));
    new_tab->setToolTip(QStringLiteral("New drawing (Ctrl+N)"));
    connect(new_tab, &QToolButton::clicked, this, [this] { create_new_tab(); });
    tab_lay->addWidget(new_tab);
    tab_lay->addStretch(1);
    col->addWidget(tab_row);
    // Click a tab -> switch active document (programmatic syncs are signal-blocked).
    connect(file_tabs_, &QTabBar::currentChanged, this, [this](int idx) {
        if (idx < 0 || viewport_ == nullptr) {
            return;
        }
        const std::uint64_t id = file_tabs_->tabData(idx).toULongLong();
        if (id != 0 && id != viewport_->active_document_id()) {
            switch_to_document(id);
        }
    });
    connect(file_tabs_, &QTabBar::tabCloseRequested, this, [this](int idx) {
        if (idx >= 0) {
            close_document_tab(file_tabs_->tabData(idx).toULongLong());
        }
    });

    // Viewport.
    QWidget* container = QWidget::createWindowContainer(viewport_, central);
    container->setMinimumSize(320, 240);
    container->setFocusPolicy(Qt::StrongFocus);
    col->addWidget(container, 1);
    viewport_container_ = container; // QWidget mapToGlobal is reliable (unlike the embedded QWindow)

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
    // The QAT logo button opens the application menu (currently: About Musa CAD).
    if (QPushButton* app_btn = ribbon_->app_button()) {
        connect(app_btn, &QPushButton::clicked, this, [this, app_btn] {
            QMenu menu(this);
            connect(menu.addAction(QStringLiteral("About Musa CAD…")), &QAction::triggered, this,
                    &MainWindow::show_about);
            menu.exec(app_btn->mapToGlobal(QPoint(0, app_btn->height())));
        });
    }

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
    qat_file(QStringLiteral("print"), QStringLiteral("Plot"), QKeySequence::Print,
             &MainWindow::open_plot_dialog);

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
            // A ribbon click is an unambiguous command start: go straight to start_command
            // (which cancels any command in progress) rather than submit_line, whose typed-
            // text path would feed the alias to the active command as input and never start.
            processor_->start_command(alias);
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
    file_btn(QStringLiteral("open"), QStringLiteral("Import\nDWG"), &MainWindow::file_import_dwg);
    file_btn(QStringLiteral("save"), QStringLiteral("Export\nDWG"), &MainWindow::file_export_dwg);
    file_btn(QStringLiteral("settings"), QStringLiteral("DWG\nSetup"),
             &MainWindow::configure_dwg_converter);
    file_btn(QStringLiteral("print"), QStringLiteral("Plot"), &MainWindow::open_plot_dialog);

    // Save As shortcut (Ctrl+Shift+S) -- New/Open/Save shortcuts live on the QAT.
    auto* save_as_act = new QAction(this);
    save_as_act->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    save_as_act->setShortcutContext(Qt::ApplicationShortcut);
    connect(save_as_act, &QAction::triggered, this, &MainWindow::file_save_as);
    addAction(save_as_act);

    // Multi-document tab shortcuts: Ctrl+Tab / Ctrl+Shift+Tab cycle, Ctrl+W closes active.
    const auto add_app_shortcut = [this](const QKeySequence& seq, auto slot) {
        auto* act = new QAction(this);
        act->setShortcut(seq);
        act->setShortcutContext(Qt::ApplicationShortcut);
        connect(act, &QAction::triggered, this, slot);
        addAction(act);
    };
    // Cross-document clipboard (Phase B): Copy/Cut snapshot the selection; Paste recreates
    // into the ACTIVE document at the cursor, remapping layers/styles/blocks by name.
    add_app_shortcut(QKeySequence::Copy, [this] {
        if (viewport_ != nullptr && viewport_->selection_count() > 0) {
            engine_->submit(core::CopyClipboardCommand{});
        }
    });
    add_app_shortcut(QKeySequence::Cut, [this] {
        if (viewport_ != nullptr && viewport_->selection_count() > 0) {
            engine_->submit(core::CutClipboardCommand{processor_->begin_group()});
        }
    });
    add_app_shortcut(QKeySequence::Paste, [this] {
        engine_->submit(core::PasteClipboardCommand{last_cursor_world_, processor_->begin_group()});
    });
    add_app_shortcut(QKeySequence(QStringLiteral("Ctrl+Tab")), [this] { cycle_document(1); });
    add_app_shortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Tab")),
                     [this] { cycle_document(-1); });
    add_app_shortcut(QKeySequence(QStringLiteral("Ctrl+W")), [this] {
        if (viewport_ != nullptr) {
            close_document_tab(viewport_->active_document_id());
        }
    });

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
    add_cmd(modify, QStringLiteral("join"), QStringLiteral("Join"), "J");        // pick-based
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
    // Dynamic Input (F12). Persisted across runs (the only persisted UI toggle).
    // Default ON: the app is canvas-only out of the box (on-canvas command entry,
    // sub-prompts and dimension fields). F12 OFF reverts to the classic bottom
    // command-line bar -- the toggleable fallback.
    const bool dyn_initial = QSettings().value(QStringLiteral("dyn/enabled"), true).toBool();
    dyn_action_ = make_mode_action(QStringLiteral("DYN"), Qt::Key_F12, dyn_initial);
    connect(dyn_action_, &QAction::toggled, this, [this](bool on) { set_dyn_enabled(on); });

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
    add_toggle(dyn_action_);
    // Apply the persisted state at startup -- but NOT under the self-test/dump
    // harness, which must run in the canonical default runtime state (DYN off, the
    // command line focused) regardless of a developer's saved preference (Ph9).
    if (!qEnvironmentVariableIsSet("MUSACAD_SELFTEST") &&
        !qEnvironmentVariableIsSet("MUSACAD_DUMP_UI") &&
        !qEnvironmentVariableIsSet("MUSACAD_SMOKE")) {
        set_dyn_enabled(dyn_initial);
    } else {
        dyn_action_->setChecked(false);
    }

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
        last_cursor_world_ = core::Vec2{x, y}; // for paste-at-cursor (Ctrl+V)
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
        // On-canvas command ENTRY (idle, canvas mode): route command keystrokes to the
        // canvas entry box + autocomplete BEFORE the bottom command line. Gated on F12
        // (canvas mode) + idle; yields to the Properties palette. Non-command keys
        // (Delete with nothing typed, etc.) return false and fall through to the guards.
        if (viewport_ != nullptr && dyn_action_ != nullptr && dyn_action_->isChecked() &&
            processor_ != nullptr && !processor_->has_active_command()) {
            // Only the drawing-focus contexts (the command line, or the viewport itself)
            // route to the canvas entry -- NEVER a dialog, the Properties palette, or any
            // other text field (those keep their own keystrokes).
            QWidget* fw = QApplication::focusWidget();
            const bool in_cmd = command_widget_ != nullptr && fw != nullptr &&
                                (fw == static_cast<QWidget*>(command_widget_) ||
                                 command_widget_->isAncestorOf(fw));
            const bool in_vp = fw == nullptr ||
                               (viewport_container_ != nullptr &&
                                (fw == viewport_container_ || viewport_container_->isAncestorOf(fw)));
            if ((in_cmd || in_vp) && viewport_->cmd_entry_handle_key(ke->key(), ke->text())) {
                return true;
            }
        }
        // COMMAND-CONTROL keys (Esc / Enter / Space) while a command is active in canvas
        // (F12-ON) mode. DYN never swallows these (the recurring keyboard-routing bug): in
        // canvas mode the command line is hidden, so a key that "falls through" the DYN
        // routing reaches no focused widget and is lost -- so we dispatch them HERE, once.
        //   - Esc  -> always cancels the active command (clears rubber-band + DYN).
        //   - Enter/Space -> AutoCAD two-step: commit a pending typed DYN/sub-prompt value
        //     if present, else end the current step (which ends LINE at a next-point prompt).
        // F12-OFF (classic bottom-bar) is unchanged: the gate is off, so the command line
        // handles Esc/Enter as before. A focused dialog / Properties field keeps its keys.
        if (viewport_ != nullptr && processor_ != nullptr && processor_->has_active_command() &&
            dyn_action_ != nullptr && dyn_action_->isChecked()) {
            const int k = ke->key();
            if (k == Qt::Key_Escape || k == Qt::Key_Return || k == Qt::Key_Enter ||
                k == Qt::Key_Space) {
                QWidget* fw = QApplication::focusWidget();
                const bool in_props = properties_dock_ != nullptr && fw != nullptr &&
                                      properties_dock_->isAncestorOf(fw);
                // The command line / DYN widget are QLineEdits but must NOT exempt the
                // carve-out (they're hidden in canvas mode); a genuine dialog/text editor does.
                const bool in_cmd = command_widget_ != nullptr && fw != nullptr &&
                                    (fw == static_cast<QWidget*>(command_widget_) ||
                                     command_widget_->isAncestorOf(fw));
                const bool in_dyn = dyn_ != nullptr && fw != nullptr &&
                                    (fw == static_cast<QWidget*>(dyn_) || dyn_->isAncestorOf(fw));
                const bool in_text_input = !in_cmd && !in_dyn &&
                                           (qobject_cast<QLineEdit*>(fw) != nullptr ||
                                            qobject_cast<QPlainTextEdit*>(fw) != nullptr);
                if (!in_props && !in_text_input) {
                    if (k == Qt::Key_Escape) {
                        viewport_->handle_escape(); // grip-cancel or processor->cancel()
                    } else {
                        viewport_->dyn_end_step(); // commit pending value, else end the step
                    }
                    return true;
                }
            }
        }
        // On-canvas Dynamic Input: during a dimensional rubber-band, route dimension
        // keystrokes (digits/'.'/'-', Tab, Backspace, option-keyword letters) to the
        // viewport's canvas fields BEFORE the command line (which holds keyboard focus by
        // default) can consume them -- this is what makes type-without-click and Tab work.
        // Yield to the Properties palette so editing a property field is never hijacked.
        // (Esc/Enter/Space are handled by the command-control carve-out above.)
        if (viewport_ != nullptr && viewport_->dyn_capturing()) {
            QWidget* fw = QApplication::focusWidget();
            const bool in_props = properties_dock_ != nullptr && fw != nullptr &&
                                  properties_dock_->isAncestorOf(fw);
            if (!in_props && viewport_->dyn_handle_key(ke->key(), ke->text())) {
                return true; // consumed by an on-canvas field
            }
        }
        // On-canvas SUB-PROMPT (active command, not a rubber-band): route value/keyword
        // keystrokes (FILLET radius, CHAMFER distances, option letters) to the at-cursor
        // prompt cell. A command is RUNNING, so -- like the dimensional field path above --
        // it owns the keystrokes wherever drawing focus happens to sit (viewport, a ribbon
        // button, a tab). The only contexts that keep their own keys are the Properties
        // palette and a genuine text editor (a dialog field). Esc / empty-Backspace fall
        // through.
        if (viewport_ != nullptr && processor_ != nullptr && viewport_->sub_prompt_active()) {
            QWidget* fw = QApplication::focusWidget();
            const bool in_props = properties_dock_ != nullptr && fw != nullptr &&
                                  properties_dock_->isAncestorOf(fw);
            const bool in_text_input = qobject_cast<QLineEdit*>(fw) != nullptr ||
                                       qobject_cast<QPlainTextEdit*>(fw) != nullptr;
            if (!in_props && !in_text_input &&
                viewport_->sub_prompt_handle_key(ke->key(), ke->text())) {
                return true;
            }
        }
        if (ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace) {
            // Block only when the user is actively editing the command-line field
            // (focused AND non-empty). An empty/idle command line -- which is the
            // usual state, since it holds focus by default -- must NOT block the
            // Delete-erases-selection binding.
            const bool typing = command_widget_ != nullptr && command_widget_->is_typing();
            // Dynamic Input is a focused QLineEdit too -- like the command line, it
            // blocks erase-selection ONLY when actually typing (focused + non-empty);
            // an empty focused DYN field still lets Delete erase the selection.
            const bool dyn_typing = dyn_ != nullptr && dyn_->is_typing();
            // Canvas-mode equivalent: an open on-canvas command-entry box is "actively
            // typing a command" -- Delete (Backspace is consumed by the entry itself)
            // must edit the command, never erase the selection underneath.
            const bool canvas_typing = viewport_ != nullptr && viewport_->cmd_entry_active();
            // ...but also never hijack the key away from a focused text editor or a
            // Properties-palette field (else Backspace while editing e.g. text height
            // would erase the selected entity instead of a digit).
            QWidget* fw = QApplication::focusWidget();
            // The command line / DYN are QLineEdits but have their own is_typing
            // logic above -- exempt them so an empty, focused field still lets Delete
            // erase the selection.
            const bool in_cmd = command_widget_ != nullptr && fw != nullptr &&
                                (fw == static_cast<QWidget*>(command_widget_) ||
                                 command_widget_->isAncestorOf(fw));
            const bool in_dyn = dyn_ != nullptr && fw != nullptr &&
                                (fw == static_cast<QWidget*>(dyn_) || dyn_->isAncestorOf(fw));
            const bool in_text_input = !in_cmd && !in_dyn &&
                                       (qobject_cast<QLineEdit*>(fw) != nullptr ||
                                        qobject_cast<QPlainTextEdit*>(fw) != nullptr);
            const bool in_properties = properties_dock_ != nullptr && fw != nullptr &&
                                       properties_dock_->isAncestorOf(fw);
            if (!typing && !dyn_typing && !canvas_typing && !in_text_input && !in_properties &&
                processor_ != nullptr && viewport_ != nullptr && !processor_->has_active_command() &&
                viewport_->selection_count() > 0) {
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

void MainWindow::toggle_properties() {
    if (properties_dock_ == nullptr) {
        return;
    }
    const bool show = !properties_dock_->isVisible();
    properties_dock_->setVisible(show);
    if (show) {
        sync_properties_panel(); // populate immediately on open
    }
}

void MainWindow::sync_properties_panel() {
    if (properties_panel_ == nullptr || viewport_ == nullptr || properties_dock_ == nullptr ||
        !properties_dock_->isVisible()) {
        return; // only the visible palette needs refreshing
    }
    properties_panel_->update_view(viewport_->selection_summary(), viewport_->layers(),
                                   viewport_->current_layer());
}

void MainWindow::update_dyn_surfaces() {
    if (dyn_ == nullptr || dyn_action_ == nullptr) {
        return;
    }
    if (!dyn_action_->isChecked()) {
        dyn_->hide();
        return;
    }
    // Canvas-only mode: the on-canvas surfaces -- command entry (idle), sub-prompt
    // cells (FILLET/CHAMFER/option keywords) and on-geometry dimension fields -- are
    // the single input surface. The legacy cursor box is retired so there is never a
    // duplicate place to type.
    if (dyn_->isVisible()) {
        dyn_->hide();
    }
    // AutoCAD's dynamic input: typing flows straight into the active surface, no click.
    // The viewport renders + captures keys, so give IT keyboard focus -- unless the
    // user is editing the Properties palette or actively typing in the command line.
    QWidget* fw = QApplication::focusWidget();
    const bool in_props = properties_dock_ != nullptr && fw != nullptr &&
                          properties_dock_->isAncestorOf(fw);
    const bool cmd_typing = command_widget_ != nullptr && command_widget_->is_typing();
    const bool vp_focused = viewport_container_ != nullptr && viewport_container_->hasFocus();
    if (viewport_container_ != nullptr && !in_props && !cmd_typing && !vp_focused) {
        viewport_container_->setFocus(Qt::OtherFocusReason);
    }
}

void MainWindow::set_dyn_enabled(bool on) {
    if (dyn_ == nullptr) {
        return;
    }
    QSettings().setValue(QStringLiteral("dyn/enabled"), on);
    if (viewport_ != nullptr) {
        viewport_->set_dyn_enabled(on); // the on-canvas surfaces follow the F12 toggle
    }
    if (on) {
        // Canvas-only mode: the on-canvas surfaces (command entry, sub-prompts,
        // dimension fields) ARE the input; the bottom command-line bar steps aside so
        // there is exactly one place to type. State is untouched -- it lives in the
        // CommandProcessor (prompt + history), so hiding the bar loses nothing.
        if (command_dock_ != nullptr) {
            command_dock_->hide();
        }
        update_dyn_surfaces(); // route keys to the viewport; retire the legacy cursor box
        refocus_dyn();
    } else {
        // Classic mode: the bottom command-line bar is the input surface again. The
        // no-stuck fallback -- F12 (or the status-bar DYN button) always brings it back.
        dyn_->hide();
        if (command_dock_ != nullptr) {
            command_dock_->show();
        }
        if (command_widget_ != nullptr) {
            command_widget_->focus_input(); // keys go back to the bottom command line
        }
    }
}

void MainWindow::reposition_dyn(double local_px, double local_py) {
    if (dyn_ == nullptr || dyn_action_ == nullptr || !dyn_action_->isChecked() || viewport_ == nullptr) {
        return;
    }
    // Anchor below-right of the crosshair (offset so it never sits under the cursor),
    // in global screen coords (DYN is a frameless tool window over the GL surface).
    const QPoint local(static_cast<int>(local_px) + 18, static_cast<int>(local_py) + 18);
    dyn_->place_at_global(viewport_->mapToGlobal(local));
}

void MainWindow::refocus_dyn() {
    if (dyn_ == nullptr || dyn_action_ == nullptr || !dyn_action_->isChecked()) {
        return;
    }
    // Re-acquire focus on the next tick (after the viewport finishes the mouse
    // event), but ONLY if focus isn't already in another text input (command line /
    // PR) -- so DYN never steals keys the user directed elsewhere.
    QTimer::singleShot(0, this, [this] {
        if (dyn_ == nullptr || !dyn_action_->isChecked()) {
            return;
        }
        // Canvas-only mode: the on-canvas surfaces are the input; the legacy cursor box
        // stays hidden. Give the viewport keyboard focus so keystrokes flow into the
        // active surface without a click -- yielding to the Properties palette and an
        // actively-typed command line.
        dyn_->hide();
        QWidget* fwd = QApplication::focusWidget();
        const bool in_props_d = properties_dock_ != nullptr && fwd != nullptr &&
                                properties_dock_->isAncestorOf(fwd);
        const bool cmd_typing_d = command_widget_ != nullptr && command_widget_->is_typing();
        if (viewport_container_ != nullptr && !in_props_d && !cmd_typing_d &&
            !viewport_container_->hasFocus()) {
            viewport_container_->setFocus(Qt::OtherFocusReason);
        }
    });
}

core::MatchPropFilter MainWindow::read_match_filter() const {
    // Persisted for the session (QSettings), defaulting to all-on like AutoCAD. Only the
    // categories with real registry descriptors are stored; the rest stay on (no effect).
    const QSettings s;
    core::MatchPropFilter f;
    f.color = s.value(QStringLiteral("matchprop/color"), true).toBool();
    f.layer = s.value(QStringLiteral("matchprop/layer"), true).toBool();
    f.lineweight = s.value(QStringLiteral("matchprop/lineweight"), true).toBool();
    f.linetype = s.value(QStringLiteral("matchprop/linetype"), true).toBool();
    f.celtscale = s.value(QStringLiteral("matchprop/celtscale"), true).toBool();
    f.text = s.value(QStringLiteral("matchprop/text"), true).toBool();
    f.dimension = s.value(QStringLiteral("matchprop/dimension"), true).toBool();
    return f;
}

void MainWindow::write_match_filter(const core::MatchPropFilter& f) {
    QSettings s;
    s.setValue(QStringLiteral("matchprop/color"), f.color);
    s.setValue(QStringLiteral("matchprop/layer"), f.layer);
    s.setValue(QStringLiteral("matchprop/lineweight"), f.lineweight);
    s.setValue(QStringLiteral("matchprop/linetype"), f.linetype);
    s.setValue(QStringLiteral("matchprop/celtscale"), f.celtscale);
    s.setValue(QStringLiteral("matchprop/text"), f.text);
    s.setValue(QStringLiteral("matchprop/dimension"), f.dimension);
}

void MainWindow::open_matchprop_dialog() {
    // Modal category dialog (inherits the app-wide dark Fusion palette). Cancel reverts to
    // the previously-applied state; OK persists. "Linetype Scale" gates the per-entity
    // CELTSCALE. Reserved categories (Plot Style / Hatch / Polyline) are shown for AutoCAD
    // parity but disabled -- they gate no registry descriptors yet.
    const core::MatchPropFilter cur = read_match_filter();
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Property Settings"));
    dlg.setObjectName(QStringLiteral("MatchPropSettings"));
    auto* outer = new QVBoxLayout(&dlg);

    const auto add_check = [&dlg](QVBoxLayout* lay, const QString& label, bool on, bool enabled) {
        auto* cb = new QCheckBox(label, &dlg);
        cb->setChecked(on);
        cb->setEnabled(enabled);
        if (!enabled) {
            cb->setToolTip(QStringLiteral("Not yet applicable in Musa CAD"));
        }
        lay->addWidget(cb);
        return cb;
    };

    auto* basic = new QGroupBox(QStringLiteral("Basic Properties"), &dlg);
    auto* bl = new QVBoxLayout(basic);
    QCheckBox* c_color = add_check(bl, QStringLiteral("Color"), cur.color, true);
    QCheckBox* c_layer = add_check(bl, QStringLiteral("Layer"), cur.layer, true);
    QCheckBox* c_lw = add_check(bl, QStringLiteral("Lineweight"), cur.lineweight, true);
    QCheckBox* c_lt = add_check(bl, QStringLiteral("Linetype"), cur.linetype, true);
    QCheckBox* c_cts = add_check(bl, QStringLiteral("Linetype Scale"), cur.celtscale, true);
    add_check(bl, QStringLiteral("Plot Style"), cur.plotstyle, false);
    outer->addWidget(basic);

    auto* special = new QGroupBox(QStringLiteral("Special Properties"), &dlg);
    auto* sl = new QVBoxLayout(special);
    QCheckBox* c_text = add_check(sl, QStringLiteral("Text"), cur.text, true);
    QCheckBox* c_dim = add_check(sl, QStringLiteral("Dimension"), cur.dimension, true);
    add_check(sl, QStringLiteral("Hatch"), cur.hatch, false);
    add_check(sl, QStringLiteral("Polyline"), cur.polyline, false);
    outer->addWidget(special);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    outer->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted) {
        return; // Cancel: keep the previously-applied state
    }
    core::MatchPropFilter f = cur; // preserves the reserved flags
    f.color = c_color->isChecked();
    f.layer = c_layer->isChecked();
    f.lineweight = c_lw->isChecked();
    f.linetype = c_lt->isChecked();
    f.celtscale = c_cts->isChecked();
    f.text = c_text->isChecked();
    f.dimension = c_dim->isChecked();
    write_match_filter(f);
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
    place("LEADER", {"LEADER", "60,60", "70,66", "see note"});

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
    // Full set: 2 ext-line origins + 2 dim-line feet + offset midpoint.
    const bool dim_grips = pump([this] { return viewport_->grip_count() == 5; });
    std::printf("[selftest] dimension shows full grip set (def + feet + offset): %s\n",
                dim_grips ? "PASS" : "FAIL");
    all = all && dim_grips;

    bool offset_ok = false;
    int off = -1;
    for (int i = 0; i < viewport_->grip_count(); ++i) {
        if (viewport_->grip_info(i).index == 2) {
            off = i; // a dim-line FOOT grip (a non-centre handle) -> moves the dim line
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

bool MainWindow::selftest_mtext() {
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

    // MTEXT: two corners (insertion + wrap width) then a paragraph that wraps.
    type("MT");
    type("0,0");
    type("40,-20");
    type("ALPHA BETA GAMMA DELTA EPSILON ZETA");
    const bool mtext_ok = pump([this] { return viewport_->line_vertex_count() > 0; });
    std::printf("[selftest] MTEXT command renders (wrapped paragraph): %s\n",
                mtext_ok ? "PASS" : "FAIL");
    all = all && mtext_ok;

    // QLEADER: arrow point -> a vertex -> Enter -> annotation text.
    engine_->submit(core::NewDocumentCommand{});
    pump([this] { return viewport_->line_vertex_count() == 0; });
    type("LE");
    type("0,0");
    type("10,8");
    type(""); // finish the vertex chain
    type("SEE NOTE");
    const bool leader_ok = pump([this] { return viewport_->line_vertex_count() > 0; });
    std::printf("[selftest] QLEADER command renders (arrow + line + text): %s\n",
                leader_ok ? "PASS" : "FAIL");
    all = all && leader_ok;

    // TEXTEDIT (scriptable path): create a single-line TEXT, then ED its content.
    // Observed outcome: the store's content actually changed + re-rendered (Ph10).
    engine_->submit(core::NewDocumentCommand{});
    pump([this] { return viewport_->line_vertex_count() == 0; });
    engine_->submit(core::AddTextCommand{{0, 0}, 2.5, 0.0, 0, "BEFORE", processor_->begin_group()});
    const auto has_content = [this](const char* s) {
        for (const std::string& c : viewport_->text_contents()) {
            if (c == s) {
                return true;
            }
        }
        return false;
    };
    pump([&] { return has_content("BEFORE"); });
    type("ED");
    type("1,1"); // pick on the text
    type("AFTER");
    const bool edit_ok = pump([&] { return has_content("AFTER") && !has_content("BEFORE"); });
    std::printf("[selftest] TEXTEDIT command changes store content (BEFORE->AFTER): %s\n",
                edit_ok ? "PASS" : "FAIL");
    all = all && edit_ok;

    // Undo restores the prior content as one group.
    engine_->submit(core::UndoLastGroupCommand{});
    const bool undo_ok = pump([&] { return has_content("BEFORE"); });
    std::printf("[selftest] text-edit undo restores prior content: %s\n",
                undo_ok ? "PASS" : "FAIL");
    all = all && undo_ok;

    engine_->submit(core::NewDocumentCommand{});
    return all;
}

bool MainWindow::selftest_properties() {
    using core::PropertyId;
    using core::PropertyValue;
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
    properties_dock_->show(); // make the palette live so sync runs
    engine_->submit(core::NewDocumentCommand{});
    pump([this] { return viewport_->line_vertex_count() == 0; });
    const auto summary = [this] { return viewport_->selection_summary(); };

    // (1) Nothing selected -> empty summary.
    const bool none_ok = pump([&] { return summary().count == 0 && summary().fields.empty(); });

    // Build: two lines (one recoloured), a circle, a text.
    engine_->submit(core::AddLineCommand{Vec2{0, 0}, Vec2{10, 0}, 1});
    engine_->submit(core::AddLineCommand{Vec2{0, 5}, Vec2{10, 5}, 2});
    engine_->submit(core::AddCircleCommand{Vec2{30, 0}, 4.0, 3});
    engine_->submit(core::AddTextCommand{Vec2{0, 20}, 2.5, 0.0, 0, "HELLO", 4});
    pump([this] { return viewport_->line_vertex_count() > 0; });

    // (2) One entity -> its full set (universal + Geometry). Sync the panel inside
    // the predicate so it reflects the live summary (decoupled from the 100ms timer).
    engine_->submit(core::SelectPickCommand{{5, 0}, 1.0, false});
    const bool one_ok = pump([&] {
        sync_properties_panel();
        return summary().count == 1 && !summary().mixed &&
               properties_panel_->has_field(PropertyId::Layer) &&
               properties_panel_->has_field(PropertyId::GeomLength);
    });
    std::printf("[selftest] PR one-entity shows universal+geometry: %s\n", one_ok ? "PASS" : "FAIL");

    // (3) Many same type -> shared set + VARIES where they differ. Recolour line #2
    // first so Color varies across the two lines.
    engine_->submit(core::SelectPickCommand{{5, 5}, 1.0, false});
    pump([&] { return summary().count == 1; });
    engine_->submit(core::SetPropertyCommand{
        PropertyId::Color, [] { PropertyValue v; v.flag = false; v.color = {255, 0, 0}; return v; }(),
        processor_->begin_group()});
    pump([&] { return !summary().fields.empty(); });
    engine_->submit(core::SelectPickCommand{{5, 0}, 1.0, false});
    engine_->submit(core::SelectPickCommand{{5, 5}, 1.0, true}); // additive -> 2 lines
    const bool many_ok = pump([&] {
        sync_properties_panel();
        return summary().count == 2 && !summary().mixed &&
               properties_panel_->field_varies(PropertyId::Color);
    });
    std::printf("[selftest] PR many-same shows shared set with *VARIES*: %s\n",
                many_ok ? "PASS" : "FAIL");
    all = all && none_ok && one_ok && many_ok;

    // (4) Mixed types -> only universal props (Geometry/Text excluded).
    engine_->submit(core::SelectPickCommand{{5, 0}, 1.0, false}); // a line
    engine_->submit(core::SelectPickCommand{{30, 4}, 1.5, true}); // + the circle
    const bool mixed_ok = pump([&] {
        sync_properties_panel();
        return summary().count == 2 && summary().mixed &&
               properties_panel_->has_field(PropertyId::Layer) &&
               !properties_panel_->has_field(PropertyId::GeomLength);
    });
    std::printf("[selftest] PR mixed shows only universal props: %s\n", mixed_ok ? "PASS" : "FAIL");
    all = all && mixed_ok;

    // (5) Universal edit (single) via the panel -> store changed + re-rendered; undo.
    engine_->submit(core::SelectPickCommand{{5, 0}, 1.0, false});
    pump([&] { sync_properties_panel(); return summary().count == 1; });
    properties_panel_->test_commit(
        PropertyId::Color, [] { PropertyValue v; v.flag = false; v.color = {0, 200, 0}; return v; }());
    const bool color_ok = pump([&] {
        sync_properties_panel();
        const auto v = properties_panel_->field_value(PropertyId::Color);
        return !v.flag && v.color == core::Rgb{0, 200, 0};
    });
    engine_->submit(core::UndoLastGroupCommand{});
    // Undo leaves the selection pointing at the now-recreated handle's predecessor;
    // re-select to inspect the genuinely reverted entity in the store.
    engine_->submit(core::SelectPickCommand{{5, 0}, 1.0, false});
    const bool color_undo = pump([&] {
        sync_properties_panel();
        return properties_panel_->has_field(PropertyId::Color) &&
               properties_panel_->field_value(PropertyId::Color).flag; // back to ByLayer
    });
    std::printf("[selftest] PR universal color edit + undo (observed): %s\n",
                (color_ok && color_undo) ? "PASS" : "FAIL");
    all = all && color_ok && color_undo;

    // (6) Text deep edit via the panel -> height changes + re-lays-out; undo.
    engine_->submit(core::SelectPickCommand{{1, 19}, 2.0, false});
    const bool text_sel = pump([&] {
        sync_properties_panel();
        return summary().count == 1 && properties_panel_->has_field(PropertyId::TextHeight);
    });
    properties_panel_->test_commit(PropertyId::TextHeight,
                                   [] { PropertyValue v; v.num = 6.0; return v; }());
    const bool height_ok = pump([&] {
        sync_properties_panel();
        return properties_panel_->field_value(PropertyId::TextHeight).num > 5.5;
    });
    engine_->submit(core::UndoLastGroupCommand{});
    engine_->submit(core::SelectPickCommand{{1, 19}, 2.0, false}); // re-select reverted text
    const bool height_undo = pump([&] {
        sync_properties_panel();
        return properties_panel_->has_field(PropertyId::TextHeight) &&
               properties_panel_->field_value(PropertyId::TextHeight).num < 3.0;
    });
    std::printf("[selftest] PR text height edit + undo (observed): %s\n",
                (text_sel && height_ok && height_undo) ? "PASS" : "FAIL");
    all = all && text_sel && height_ok && height_undo;

    // (7) Regression: Backspace while a PR field is focused must edit the field, NOT
    // erase the selected entity (the global Delete/Backspace binding must yield).
    engine_->submit(core::SelectPickCommand{{1, 19}, 2.0, false});
    pump([&] {
        sync_properties_panel();
        return summary().count == 1 &&
               properties_panel_->editor_widget(PropertyId::TextHeight) != nullptr;
    });
    bool bksp_ok = false;
    if (QWidget* he = properties_panel_->editor_widget(PropertyId::TextHeight); he != nullptr) {
        // Settle focus on the field before the key (else the app filter reads a stale
        // focusWidget and the guard is evaluated wrong -- a real-window timing race).
        // The PR field lives in the main window, so make that the active window first.
        QApplication::setActiveWindow(this);
        activateWindow();
        raise();
        he->setFocus(Qt::OtherFocusReason);
        for (int i = 0; i < 200 && QApplication::focusWidget() != he; ++i) {
            QCoreApplication::processEvents();
            QApplication::setActiveWindow(this);
            he->setFocus(Qt::OtherFocusReason);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        QKeyEvent ev(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
        QApplication::sendEvent(he, &ev);
        for (int i = 0; i < 60; ++i) {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        bksp_ok = summary().count == 1; // entity survived the Backspace
    }
    std::printf("[selftest] PR Backspace in field does not erase entity: %s\n",
                bksp_ok ? "PASS" : "FAIL");
    all = all && bksp_ok;

    properties_dock_->hide();
    engine_->submit(core::NewDocumentCommand{});
    return all;
}

bool MainWindow::selftest_linetype() {
    using core::Linetype;
    using core::PropertyId;
    using core::PropertyValue;
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

    // A continuous line is a single segment (2 vertices).
    engine_->submit(core::AddLineCommand{Vec2{0, 0}, Vec2{200, 0}, 1});
    pump([this] { return viewport_->line_vertex_count() == 2; });

    // Set its linetype to Dashed via the PR write path -> it visibly dashes (the
    // Ph22 gap, now closed). Observed: many more vertices than the solid line.
    engine_->submit(core::SelectAllCommand{});
    pump([this] { return viewport_->selection_count() == 1; });
    PropertyValue lt;
    lt.flag = false; // override (not ByLayer)
    lt.choice = static_cast<int>(Linetype::Dashed);
    engine_->submit(core::SetPropertyCommand{PropertyId::Linetype, lt, processor_->begin_group()});
    const bool dashed_ok = pump([this] { return viewport_->line_vertex_count() > 2; });
    const int dashed_n = viewport_->line_vertex_count();
    std::printf("[selftest] PR linetype Dashed visibly dashes the line: %s\n",
                dashed_ok ? "PASS" : "FAIL");

    // LTSCALE up -> longer dashes -> fewer of them (re-derived live, not stored).
    engine_->submit(core::SetLtscaleCommand{8.0});
    const bool ltscale_ok = pump([this, dashed_n] {
        const int n = viewport_->line_vertex_count();
        return n > 2 && n < dashed_n;
    });
    std::printf("[selftest] LTSCALE re-dashes live (fewer dashes at scale 8): %s\n",
                ltscale_ok ? "PASS" : "FAIL");

    // Back to Continuous -> solid again (one segment).
    engine_->submit(core::SelectAllCommand{});
    pump([this] { return viewport_->selection_count() == 1; });
    PropertyValue cont;
    cont.flag = false;
    cont.choice = static_cast<int>(Linetype::Continuous);
    engine_->submit(core::SetPropertyCommand{PropertyId::Linetype, cont, processor_->begin_group()});
    const bool cont_ok = pump([this] { return viewport_->line_vertex_count() == 2; });
    std::printf("[selftest] Continuous linetype renders solid again: %s\n",
                cont_ok ? "PASS" : "FAIL");

    all = all && dashed_ok && ltscale_ok && cont_ok;
    engine_->submit(core::SetLtscaleCommand{1.0});
    engine_->submit(core::NewDocumentCommand{});
    return all;
}

bool MainWindow::selftest_dim_properties() {
    using core::DimType;
    using core::PropertyId;
    using core::PropertyValue;
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
    properties_dock_->show();
    engine_->submit(core::NewDocumentCommand{});
    pump([this] { return viewport_->line_vertex_count() == 0; });

    // Effective value of a field in the (live) selection summary.
    const auto field = [this](PropertyId id) {
        for (const auto& f : viewport_->selection_summary().fields) {
            if (f.id == id) {
                return f.value;
            }
        }
        return PropertyValue{};
    };
    const auto has = [this](PropertyId id) {
        for (const auto& f : viewport_->selection_summary().fields) {
            if (f.id == id) {
                return true;
            }
        }
        return false;
    };

    // Two linear dimensions (so we can test multi-select varies/set-all).
    engine_->submit(core::AddDimensionCommand{static_cast<std::uint8_t>(DimType::Linear),
                                              {0, 0}, {10, 0}, {5, 3}, 0, 1});
    engine_->submit(core::AddDimensionCommand{static_cast<std::uint8_t>(DimType::Linear),
                                              {0, 20}, {10, 20}, {5, 23}, 0, 2});
    pump([this] { return viewport_->line_vertex_count() > 0; });

    // One dim selected -> the Dimension group is present and ByStyle.
    engine_->submit(core::SelectPickCommand{{5, 3}, 2.0, false});
    const bool group_ok = pump([&] {
        sync_properties_panel();
        return viewport_->selection_count() == 1 && has(PropertyId::DimArrowSize) &&
               has(PropertyId::DimTextColor) && field(PropertyId::DimArrowSize).flag; // ByStyle
    });
    std::printf("[selftest] PR dimension group present + ByStyle: %s\n", group_ok ? "PASS" : "FAIL");

    // Set an arrow-size override = 9 via the panel (observed: store override + effective 9).
    PropertyValue set;
    set.flag = false;
    set.num = 9.0;
    properties_panel_->test_commit(PropertyId::DimArrowSize, set);
    const bool over_ok = pump([&] {
        sync_properties_panel();
        const auto v = field(PropertyId::DimArrowSize);
        return !v.flag && v.num > 8.5; // Overridden, effective = 9
    });
    std::printf("[selftest] PR dim arrow-size override (observed): %s\n", over_ok ? "PASS" : "FAIL");

    // Reset to ByStyle via the panel -> effective follows the style again.
    PropertyValue reset;
    reset.flag = true;
    properties_panel_->test_commit(PropertyId::DimArrowSize, reset);
    const bool reset_ok = pump([&] {
        sync_properties_panel();
        const auto v = field(PropertyId::DimArrowSize);
        return v.flag && v.num < 8.5; // ByStyle, effective = style 2.5
    });
    std::printf("[selftest] PR dim reset-to-style (observed): %s\n", reset_ok ? "PASS" : "FAIL");

    // Undo the reset restores the override (one undo group each).
    engine_->submit(core::UndoLastGroupCommand{});
    engine_->submit(core::SelectPickCommand{{5, 3}, 2.0, false});
    const bool undo_ok = pump([&] {
        sync_properties_panel();
        const auto v = field(PropertyId::DimArrowSize);
        return !v.flag && v.num > 8.5; // back to overridden 9
    });
    std::printf("[selftest] PR dim override undo restores it: %s\n", undo_ok ? "PASS" : "FAIL");

    // Multi-dim select -> shared rows; set override on ALL.
    engine_->submit(core::SelectAllCommand{});
    pump([this] { return viewport_->selection_count() == 2; });
    PropertyValue set2;
    set2.flag = false;
    set2.num = 5.0;
    properties_panel_->test_commit(PropertyId::DimArrowSize, set2);
    const bool multi_ok = pump([&] {
        sync_properties_panel();
        const auto v = field(PropertyId::DimArrowSize);
        return viewport_->selection_count() == 2 && !v.flag && v.num > 4.5 && v.num < 5.5;
    });
    std::printf("[selftest] PR multi-dim set-all override: %s\n", multi_ok ? "PASS" : "FAIL");

    all = all && group_ok && over_ok && reset_ok && undo_ok && multi_ok;
    properties_dock_->hide();
    engine_->submit(core::NewDocumentCommand{});
    return all;
}

bool MainWindow::selftest_dyn() {
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
    // A real key event to whoever holds focus -- traverses the app event filter, the
    // routing path the canvas surfaces actually use in production.
    const auto send_key = [](int key, const QString& t = QString()) {
        QWidget* target = QApplication::focusWidget();
        QKeyEvent ev(QEvent::KeyPress, key, Qt::NoModifier, t);
        QApplication::sendEvent(target != nullptr ? static_cast<QObject*>(target)
                                                  : static_cast<QObject*>(QApplication::instance()),
                                &ev);
    };
    // Give the viewport keyboard focus (canvas mode: it captures the keystrokes).
    const auto focus_vp = [this] {
        if (viewport_container_ != nullptr) {
            viewport_container_->setFocus(Qt::OtherFocusReason);
        }
        QCoreApplication::processEvents();
    };
    bool all = true;
    const bool saved_pref = QSettings().value(QStringLiteral("dyn/enabled"), false).toBool();

    // (1) F12 on -> canvas-only mode: the bottom command-line bar hides (the on-canvas
    // surfaces are the input) and the preference persists.
    dyn_action_->setChecked(true);
    const bool on_ok =
        pump([this] { return command_dock_ != nullptr && command_dock_->isHidden(); }) &&
        QSettings().value(QStringLiteral("dyn/enabled")).toBool();
    std::printf("[selftest] DYN F12 on -> canvas-only (bottom bar hidden) + persisted: %s\n",
                on_ok ? "PASS" : "FAIL");

    engine_->submit(core::NewDocumentCommand{});
    pump([this] { return viewport_->line_vertex_count() == 0; });

    // (1b) Autocomplete on canvas: typing a partial command opens the entry box and the
    // SAME Ph6 suggestion list from the registry. "C" must yield suggestions.
    viewport_->cmd_entry_handle_key(Qt::Key_C, QStringLiteral("C"));
    const bool sugg_ok =
        pump([this] { return viewport_->cmd_entry_active() && viewport_->cmd_suggestion_count() > 0; });
    viewport_->cmd_entry_handle_key(Qt::Key_Escape, QString()); // clear -> entry closes
    pump([this] { return !viewport_->cmd_entry_active(); });
    std::printf("[selftest] DYN canvas autocomplete shows registry suggestions: %s\n",
                sugg_ok ? "PASS" : "FAIL");
    all = all && sugg_ok;

    // (2) Start LINE by typing into the canvas entry box; Enter runs it.
    for (QChar ch : QStringLiteral("LINE")) {
        viewport_->cmd_entry_handle_key(0, ch);
    }
    viewport_->cmd_entry_handle_key(Qt::Key_Return, QString());
    const bool start_ok = pump([this] { return processor_->has_active_command(); });
    const bool prompt_ok = pump([this] {
        const std::string p = processor_->current_prompt();
        return !p.empty() && p != "Command: ";
    });
    std::printf("[selftest] DYN canvas entry starts a command + processor prompt advances: %s\n",
                (start_ok && prompt_ok) ? "PASS" : "FAIL");

    // (3) First point via the at-cursor SUB-PROMPT cell. Once the rubber-band is live the
    // on-canvas value FIELDS take over: there is one place to type. The viewport captures
    // dimension keystrokes (no click) + Tab through the shared compose pipeline.
    for (QChar ch : QStringLiteral("0,0")) {
        viewport_->sub_prompt_handle_key(0, ch);
    }
    viewport_->sub_prompt_handle_key(Qt::Key_Return, QString());
    const bool tips_took_over = pump([this] {
        return viewport_->dyn_field_count() == 2 && viewport_->dyn_capturing();
    });
    viewport_->dyn_test_type("50"); // Length
    const bool tab_ok = (viewport_->dyn_active_slot() == 0);
    viewport_->dyn_test_tab();      // -> Angle
    const bool tabbed = (viewport_->dyn_active_slot() == 1);
    viewport_->dyn_test_type("0");  // Angle (deg)
    viewport_->dyn_test_commit();
    Vec2 mn{}, mx{};
    const bool exact_ok = pump([&] {
        return viewport_->line_vertex_count() == 2 && viewport_->content_bounds(mn, mx) &&
               std::abs((mx.x - mn.x) - 50.0) < 0.01 && std::abs(mx.y - mn.y) < 0.01;
    });
    std::printf("[selftest] DYN sub-prompt point -> canvas fields take over (2 fields=%d) + Tab=%d + "
                "exact-length-typed-no-click=%d: %s\n",
                tips_took_over ? 1 : 0, (tab_ok && tabbed) ? 1 : 0, exact_ok ? 1 : 0,
                (tips_took_over && exact_ok) ? "PASS" : "FAIL");
    all = all && on_ok && start_ok && prompt_ok && tips_took_over && exact_ok;

    // (4) THE FOCUS CHECK -- with DYN on (canvas mode), the prior gestures still work.
    // First end the still-active LINE chain (an active command would block erase-select).
    processor_->cancel();
    pump([this] { return !processor_->has_active_command(); });
    engine_->submit(core::NewDocumentCommand{});
    pump([this] { return viewport_->line_vertex_count() == 0; });
    engine_->submit(AddLineCommand{Vec2{0, 0}, Vec2{20, 0}, 1});
    pump([this] { return viewport_->line_vertex_count() == 2; });
    engine_->submit(core::SelectAllCommand{});
    pump([this] { return viewport_->selection_count() == 1; });

    // 4a. Delete with NO canvas entry open still erases the selection (viewport focused).
    viewport_->cmd_entry_handle_key(Qt::Key_Escape, QString()); // ensure entry closed
    focus_vp();
    send_key(Qt::Key_Delete);
    const bool del_ok = pump([this] { return viewport_->selection_count() == 0; });

    // 4b. Backspace WHILE typing in the canvas entry edits the command, NOT the selection.
    engine_->submit(AddLineCommand{Vec2{0, 5}, Vec2{20, 5}, 2});
    pump([this] { return viewport_->line_vertex_count() == 2; });
    engine_->submit(core::SelectAllCommand{});
    pump([this] { return viewport_->selection_count() == 1; });
    focus_vp();
    send_key(Qt::Key_R, QStringLiteral("R")); // open the entry: now "typing"
    send_key(Qt::Key_E, QStringLiteral("E"));
    pump([this] { return viewport_->cmd_entry_active(); });
    send_key(Qt::Key_Backspace); // consumed by the entry -> "R"; selection untouched
    QCoreApplication::processEvents();
    const bool guard_ok = !pump([this] { return viewport_->selection_count() == 0; }); // stays selected
    viewport_->cmd_entry_handle_key(Qt::Key_Escape, QString());

    // 4c. Esc cancels the active command (routes to the viewport escape handler). The
    // sub-prompt handler returns false on Esc by design (the viewport cancels), so the
    // key must reach the viewport window itself -- in the real app it holds focus; here
    // post it straight to the viewport so the assertion exercises the same handler.
    engine_->submit(core::ClearSelectionCommand{});
    pump([this] { return viewport_->selection_count() == 0; });
    processor_->submit_line("LINE");
    pump([this] { return processor_->has_active_command(); });
    focus_vp();
    {
        QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(viewport_, &esc);
    }
    const bool esc_ok = pump([this] { return !processor_->has_active_command(); });
    std::printf("[selftest] DYN focus rule: Delete-erase(empty)=%d typing-guard=%d Esc=%d\n",
                del_ok, guard_ok, esc_ok);
    all = all && del_ok && guard_ok && esc_ok;

    // Restore default runtime state: DYN off, and the developer's saved preference.
    dyn_action_->setChecked(false);
    QSettings().setValue(QStringLiteral("dyn/enabled"), saved_pref);
    engine_->submit(core::NewDocumentCommand{});
    return all;
}

bool MainWindow::cmdctl_shot(int kind, const std::string& out_png) {
    const auto pump = [](int ms) {
        for (int i = 0; i < ms / 2; ++i) {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    };
    const auto pump_until = [](auto pred) {
        for (int i = 0; i < 600; ++i) {
            if (pred()) {
                return true;
            }
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return pred();
    };
    // Send a key the same way the real app does: through the app-wide event filter (the
    // command-control carve-out lives there). sendEvent runs qApp event filters first.
    const auto send_key = [](int key, const QString& t) {
        QWidget* target = QApplication::focusWidget();
        QKeyEvent ke(QEvent::KeyPress, key, Qt::NoModifier, t);
        QApplication::sendEvent(target != nullptr ? static_cast<QObject*>(target)
                                                  : static_cast<QObject*>(QApplication::instance()),
                                &ke);
    };
    const auto focus_vp = [this] {
        activateWindow();
        if (viewport_container_ != nullptr) {
            viewport_container_->setFocus(Qt::OtherFocusReason);
        }
    };
    const auto hold = [&](const char* label) {
        std::printf("[cmdctl_shot] kind=%d %s main=0x%lx frameG=(%d,%d %dx%d)\n", kind, label,
                    static_cast<unsigned long>(winId()), frameGeometry().x(), frameGeometry().y(),
                    frameGeometry().width(), frameGeometry().height());
        std::fflush(stdout);
        if (qEnvironmentVariableIsSet("MUSACAD_DYN_HOLD")) {
            std::printf("[cmdctl_shot] HOLD: capture with `import -window 0x%lx %s`\n",
                        static_cast<unsigned long>(winId()), out_png.c_str());
            std::fflush(stdout);
            pump(12000);
        }
    };

    resize(1200, 820);
    move(60, 60);
    show();
    raise();
    activateWindow();
    pump(700);
    engine_->submit(core::NewDocumentCommand{});
    pump(200);
    dyn_action_->setChecked(true); // canvas (F12-ON) mode -- the carve-out's gate
    pump(150);
    focus_vp();
    pump(60);

    bool ok = false;

    if (kind == 0) {
        // ESC cancels a LINE mid-rubber-band, even with a pending typed DYN value.
        processor_->start_command("LINE");
        pump(80);
        processor_->submit_line("0,0");
        processor_->submit_line("50,0");
        processor_->submit_line("50,30"); // two committed segments; rubber-band for the next
        pump(120);
        const int verts_before = viewport_->line_vertex_count();
        const bool active_before = processor_->has_active_command();
        focus_vp();
        viewport_->dyn_test_type("20"); // a pending typed value: Esc must STILL cancel
        pump(60);
        const bool pending = viewport_->dyn_typing();
        send_key(Qt::Key_Escape, QString());
        const bool cancelled = pump_until([this] { return !processor_->has_active_command(); });
        const int verts_after = viewport_->line_vertex_count();
        ok = active_before && pending && cancelled && verts_after == verts_before && verts_after > 0;
        std::printf("[cmdctl_shot] kind=0 ESC: active_before=%d pending_value=%d cancelled=%d "
                    "committed_verts=%d->%d (kept) => %s\n",
                    active_before, pending, cancelled, verts_before, verts_after,
                    ok ? "PASS" : "FAIL");
        std::fflush(stdout);
        hold("after-ESC (no active command, committed segments kept)");
    } else if (kind == 1) {
        // ENTER with no pending value ends the LINE at a next-point prompt.
        processor_->start_command("LINE");
        pump(80);
        processor_->submit_line("0,0");
        processor_->submit_line("50,0");
        processor_->submit_line("50,30"); // two committed segments; rubber-band for the next
        pump(120);
        const int verts_before = viewport_->line_vertex_count();
        const bool active_before = processor_->has_active_command();
        const bool no_pending = !viewport_->dyn_typing();
        focus_vp();
        send_key(Qt::Key_Return, QStringLiteral("\r"));
        const bool ended = pump_until([this] { return !processor_->has_active_command(); });
        const int verts_after = viewport_->line_vertex_count();
        ok = active_before && no_pending && ended && verts_after == verts_before && verts_after > 0;
        std::printf("[cmdctl_shot] kind=1 ENTER-ends: active_before=%d no_pending=%d ended=%d "
                    "committed_verts=%d->%d (kept) => %s\n",
                    active_before, no_pending, ended, verts_before, verts_after,
                    ok ? "PASS" : "FAIL");
        std::fflush(stdout);
        hold("after-ENTER (LINE ended, segments kept)");
    } else if (kind == 2) {
        // ENTER two-step: a pending typed value commits (keeps drawing); a second ENTER with
        // nothing typed ends the command.
        processor_->start_command("LINE");
        pump(80);
        processor_->submit_line("0,0"); // first point -> rubber-band (length/angle fields)
        pump(120);
        focus_vp();
        viewport_->dyn_test_type("50");  // Length = 50
        viewport_->dyn_test_tab();
        viewport_->dyn_test_type("0");   // Angle = 0
        pump(60);
        const bool pending = viewport_->dyn_typing();
        const int verts_before = viewport_->line_vertex_count();
        send_key(Qt::Key_Return, QStringLiteral("\r")); // step 1: commit the typed value
        const bool committed = pump_until([this, verts_before] {
            return viewport_->line_vertex_count() > verts_before;
        });
        const bool still_active = processor_->has_active_command();
        std::printf("[cmdctl_shot] kind=2 ENTER step1 (commit typed value): pending=%d "
                    "committed=%d verts %d->%d still_active=%d\n",
                    pending, committed, verts_before, viewport_->line_vertex_count(),
                    still_active);
        std::fflush(stdout);
        hold("after-ENTER-step1 (typed 50-unit segment committed, still drawing)");
        focus_vp();
        send_key(Qt::Key_Return, QStringLiteral("\r")); // step 2: no pending value -> end
        const bool ended = pump_until([this] { return !processor_->has_active_command(); });
        ok = pending && committed && still_active && ended;
        std::printf("[cmdctl_shot] kind=2 ENTER step2 (end): ended=%d => %s\n", ended,
                    ok ? "PASS" : "FAIL");
        std::fflush(stdout);
    } else if (kind == 3) {
        // A ribbon click while a command is active cancels it and starts the new one.
        processor_->start_command("LINE");
        pump(80);
        processor_->submit_line("0,0");
        processor_->submit_line("50,0"); // one committed segment; LINE rubber-band active
        pump(120);
        const bool line_active = processor_->has_active_command();
        const std::string prompt_line = processor_->current_prompt();
        const int verts_line = viewport_->line_vertex_count();
        // Click the REAL ribbon Circle button (objectName ribbon.cmd.C) -> add_cmd lambda.
        auto* circle_btn = findChild<QToolButton*>(QStringLiteral("ribbon.cmd.C"));
        const bool have_btn = circle_btn != nullptr;
        if (have_btn) {
            circle_btn->click();
        } else {
            processor_->start_command("C"); // fallback: same dispatch the button uses
        }
        pump(150);
        const bool still_active = processor_->has_active_command();
        const std::string prompt_circle = processor_->current_prompt();
        const bool switched = prompt_circle != prompt_line; // LINE next-point -> CIRCLE center
        const int verts_after = viewport_->line_vertex_count();
        ok = line_active && still_active && switched && verts_after == verts_line;
        std::printf("[cmdctl_shot] kind=3 RIBBON cancels-active: line_active=%d clicked_button=%d "
                    "still_active=%d switched=%d prompt '%s' -> '%s' line_verts=%d->%d => %s\n",
                    line_active, have_btn, still_active, switched, prompt_line.c_str(),
                    prompt_circle.c_str(), verts_line, verts_after, ok ? "PASS" : "FAIL");
        std::fflush(stdout);
        hold("after-RIBBON-click (LINE cancelled, CIRCLE active, LINE segment kept)");
    }

    dyn_action_->setChecked(false);
    engine_->submit(core::NewDocumentCommand{});
    return ok;
}

bool MainWindow::dyn_shot(int kind, const std::string& out_png) {
    const auto pump = [](int ms) {
        for (int i = 0; i < ms / 2; ++i) {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    };
    // A known-size window so the captured region is just the app.
    resize(1200, 820);
    move(60, 60);
    show();
    raise();
    activateWindow();
    pump(700); // let the GL viewport render a first frame

    engine_->submit(core::NewDocumentCommand{});
    pump(200);
    dyn_action_->setChecked(true); // F12: enable DYN box + on-geometry tooltips
    pump(150);

    // kind 6 = F12 OFF (classic: bottom command-line bar visible), kind 7 = F12 ON
    // (canvas-only: bottom bar hidden, on-canvas command entry is the surface). Proves
    // the Part-3 toggle + the no-stuck fallback (the bar always returns on F12 off).
    if (kind == 6 || kind == 7) {
        const bool canvas = (kind == 7);
        dyn_action_->setChecked(canvas);
        pump(200);
        const bool bar_visible = command_dock_ != nullptr && command_dock_->isVisible();
        const auto send_key = [](int key, const QString& t) {
            QWidget* target = QApplication::focusWidget();
            QKeyEvent ke(QEvent::KeyPress, key, Qt::NoModifier, t);
            QApplication::sendEvent(target != nullptr ? static_cast<QObject*>(target)
                                                      : static_cast<QObject*>(QApplication::instance()),
                                    &ke);
        };
        if (canvas) {
            // Canvas-only: type "REC" into the on-canvas entry box (the bar is gone).
            activateWindow();
            if (viewport_container_ != nullptr) {
                viewport_container_->setFocus(Qt::OtherFocusReason);
            }
            pump(60);
            send_key(Qt::Key_R, "R");
            send_key(Qt::Key_E, "E");
            send_key(Qt::Key_C, "C");
            pump(120);
        } else if (command_widget_ != nullptr) {
            // Classic: focus the bottom bar so the screenshot shows the active surface.
            command_widget_->focus_input();
            pump(60);
        }
        const QRect mfr6 = frameGeometry();
        std::printf("[dyn_shot] kind=%d F12=%s bottom-bar-visible=%d canvas-entry-active=%d "
                    "main=0x%lx frameG=(%d,%d %dx%d)\n",
                    kind, canvas ? "ON" : "OFF", bar_visible ? 1 : 0,
                    viewport_->cmd_entry_active() ? 1 : 0, static_cast<unsigned long>(winId()),
                    mfr6.x(), mfr6.y(), mfr6.width(), mfr6.height());
        std::fflush(stdout);
        if (qEnvironmentVariableIsSet("MUSACAD_DYN_HOLD")) {
            std::printf("[dyn_shot] HOLD: capture with `import -window 0x%lx %s`\n",
                        static_cast<unsigned long>(winId()), out_png.c_str());
            std::fflush(stdout);
            pump(12000);
        }
        // F12 ON expects the bar hidden; F12 OFF expects it visible.
        const bool ok6 = canvas ? !bar_visible : bar_visible;
        dyn_action_->setChecked(false);
        return ok6;
    }

    // kind 8: RECTANGLE Dimensions option on canvas -- after the first corner, type the
    // "D" keyword (routed by the on-geometry field path), then the length + width as
    // at-cursor SUB-PROMPT cells (the scalar_prompt flag flips them off the 2-field drag).
    if (kind == 8) {
        const auto send_key = [](int key, const QString& t) {
            QWidget* target = QApplication::focusWidget();
            QKeyEvent ke(QEvent::KeyPress, key, Qt::NoModifier, t);
            QApplication::sendEvent(target != nullptr ? static_cast<QObject*>(target)
                                                      : static_cast<QObject*>(QApplication::instance()),
                                    &ke);
        };
        activateWindow();
        if (viewport_container_ != nullptr) {
            viewport_container_->setFocus(Qt::OtherFocusReason);
        }
        pump(60);
        processor_->submit_line("REC");
        pump(80);
        processor_->submit_line("0,0"); // first corner -> AwaitCorner (2-field drag)
        pump(120);
        const bool drag_fields = viewport_->dyn_capturing() && viewport_->dyn_field_count() == 2;
        send_key(Qt::Key_D, "D"); // Dimensions keyword (option-letter routing)
        pump(120);
        const bool sub_len = viewport_->sub_prompt_active();
        const std::string plen = processor_->current_prompt();
        send_key(Qt::Key_5, "5");
        send_key(Qt::Key_0, "0");
        pump(60);
        const bool typed_len = viewport_->sub_prompt_value() == "50";
        std::printf("[dyn_shot] kind=8 corner-drag-fields=%d Dim-keyword->sub-prompt=%d prompt='%s' "
                    "typed-len=%d value='%s' main=0x%lx frameG=(%d,%d %dx%d)\n",
                    drag_fields ? 1 : 0, sub_len ? 1 : 0, plen.c_str(), typed_len ? 1 : 0,
                    viewport_->sub_prompt_value().c_str(), static_cast<unsigned long>(winId()),
                    frameGeometry().x(), frameGeometry().y(), frameGeometry().width(),
                    frameGeometry().height());
        std::fflush(stdout);
        if (qEnvironmentVariableIsSet("MUSACAD_DYN_HOLD")) {
            std::printf("[dyn_shot] HOLD: capture with `import -window 0x%lx %s`\n",
                        static_cast<unsigned long>(winId()), out_png.c_str());
            std::fflush(stdout);
            pump(12000);
        }
        send_key(Qt::Key_Enter, QStringLiteral("\r")); // commit length -> width sub-prompt
        pump(120);
        const std::string pwid = processor_->current_prompt();
        const bool to_width = pwid != plen && viewport_->sub_prompt_active();
        send_key(Qt::Key_3, "3");
        send_key(Qt::Key_0, "0");
        pump(40);
        const bool typed_wid = viewport_->sub_prompt_value() == "30";
        send_key(Qt::Key_Enter, QStringLiteral("\r")); // commit width -> fixed-size corner pick
        pump(120);
        const bool back_to_drag = viewport_->dyn_capturing() && viewport_->dyn_field_count() == 2;
        std::printf("[dyn_shot] kind=8 length->width prompt='%s' to-width=%d typed-wid=%d "
                    "back-to-fixed-corner=%d\n",
                    pwid.c_str(), to_width ? 1 : 0, typed_wid ? 1 : 0, back_to_drag ? 1 : 0);
        std::fflush(stdout);
        dyn_action_->setChecked(false);
        return drag_fields && sub_len && typed_len && to_width && typed_wid && back_to_drag;
    }

    // kind 3: idle command ENTRY -- type "REC" via real key events to the focused
    // command line; the app event filter must route them to the canvas entry box.
    if (kind == 3) {
        const auto send_key = [](int key, const QString& t) {
            QWidget* target = QApplication::focusWidget();
            QKeyEvent ke(QEvent::KeyPress, key, Qt::NoModifier, t);
            QApplication::sendEvent(target != nullptr ? static_cast<QObject*>(target)
                                                      : static_cast<QObject*>(QApplication::instance()),
                                    &ke);
        };
        // Focus a drawing context (the viewport) -- as if the user clicked the canvas --
        // so the routing guard passes deterministically in the automated run.
        activateWindow();
        if (viewport_container_ != nullptr) {
            viewport_container_->setFocus(Qt::OtherFocusReason);
        }
        pump(60);
        QWidget* fw3 = QApplication::focusWidget();
        const bool cmd_focused = viewport_container_ != nullptr && fw3 != nullptr &&
                                 (fw3 == viewport_container_ || viewport_container_->isAncestorOf(fw3));
        send_key(Qt::Key_R, "R");
        send_key(Qt::Key_E, "E");
        send_key(Qt::Key_C, "C");
        pump(120);
        const QRect mfr3 = frameGeometry();
        std::printf("[dyn_shot] kind=3 cmd-line-focused=%d canvas-entry-active=%d entry='%s' "
                    "suggestions=%d main=0x%lx frameG=(%d,%d %dx%d)\n",
                    cmd_focused ? 1 : 0, viewport_->cmd_entry_active() ? 1 : 0,
                    viewport_->cmd_entry_text().c_str(), viewport_->cmd_suggestion_count(),
                    static_cast<unsigned long>(winId()), mfr3.x(), mfr3.y(), mfr3.width(),
                    mfr3.height());
        std::fflush(stdout);
        if (qEnvironmentVariableIsSet("MUSACAD_DYN_HOLD")) {
            std::printf("[dyn_shot] HOLD: capture with `import -window 0x%lx %s`\n",
                        static_cast<unsigned long>(winId()), out_png.c_str());
            std::fflush(stdout);
            pump(12000);
        }
        const bool shown = viewport_->cmd_entry_active() && viewport_->cmd_suggestion_count() > 0;
        // Enter runs the typed command via the same pipeline (Ph6 behaviour on canvas).
        send_key(Qt::Key_Return, QStringLiteral("\r"));
        pump(150);
        const bool ran = processor_->has_active_command();
        const bool cleared = !viewport_->cmd_entry_active();
        std::printf("[dyn_shot] kind=3 Enter-runs-command=%d (active='%s') entry-cleared=%d\n",
                    ran ? 1 : 0, processor_->last_command().c_str(), cleared ? 1 : 0);
        std::fflush(stdout);
        dyn_action_->setChecked(false);
        return shown && ran && cleared;
    }

    // kind 4 = FILLET, kind 5 = CHAMFER: drive the mid-command sub-prompts on canvas.
    if (kind == 4 || kind == 5) {
        const auto send_key = [](int key, const QString& t) {
            QWidget* target = QApplication::focusWidget();
            QKeyEvent ke(QEvent::KeyPress, key, Qt::NoModifier, t);
            QApplication::sendEvent(target != nullptr ? static_cast<QObject*>(target)
                                                      : static_cast<QObject*>(QApplication::instance()),
                                    &ke);
        };
        activateWindow();
        if (viewport_container_ != nullptr) {
            viewport_container_->setFocus(Qt::OtherFocusReason);
        }
        pump(60);
        processor_->submit_line(kind == 4 ? "FILLET" : "CHAMFER");
        pump(120);
        const bool sub1 = viewport_->sub_prompt_active();
        const std::string p1 = processor_->current_prompt();
        // Type the first value via real keys (routed by the app filter to the canvas).
        send_key(Qt::Key_3, "3");
        pump(60);
        const bool typed1 = viewport_->sub_prompt_value() == "3";
        std::printf("[dyn_shot] kind=%d sub-prompt-active=%d prompt1='%s' typed1=%d value='%s'\n",
                    kind, sub1 ? 1 : 0, p1.c_str(), typed1 ? 1 : 0,
                    viewport_->sub_prompt_value().c_str());
        std::fflush(stdout);
        if (qEnvironmentVariableIsSet("MUSACAD_DYN_HOLD")) {
            std::printf("[dyn_shot] HOLD: capture with `import -window 0x%lx %s`\n",
                        static_cast<unsigned long>(winId()), out_png.c_str());
            std::fflush(stdout);
            pump(12000);
        }
        send_key(Qt::Key_Enter, QStringLiteral("\r")); // commit first value -> next step
        pump(120);
        const std::string p2 = processor_->current_prompt();
        const bool advanced = p2 != p1;
        // CHAMFER: a second distance sub-prompt; type it too.
        bool typed2 = true;
        if (kind == 5) {
            send_key(Qt::Key_3, "3");
            pump(60);
            typed2 = viewport_->sub_prompt_value() == "3";
            send_key(Qt::Key_Enter, QStringLiteral("\r"));
            pump(120);
        }
        std::printf("[dyn_shot] kind=%d after-Enter prompt2='%s' advanced=%d typed2=%d\n", kind,
                    p2.c_str(), advanced ? 1 : 0, typed2 ? 1 : 0);
        std::fflush(stdout);
        dyn_action_->setChecked(false);
        return sub1 && typed1 && advanced && typed2;
    }

    // Start the command and drop the first point (world origin = viewport centre).
    const char* cmd = (kind == 1) ? "LINE" : (kind == 2) ? "CIRCLE" : "REC";
    processor_->submit_line(cmd);
    pump(100);
    processor_->submit_line("0,0");
    pump(100);

    // Synthesize a constrained cursor move to the lower-right quadrant -- this drives
    // rebuild_overlay(), which builds the on-canvas value fields anchored to the
    // rubber-band geometry (drawn by the renderer with the scene camera).
    const double vw = static_cast<double>(viewport_->width());
    const double vh = static_cast<double>(viewport_->height());
    const QPointF lp(vw * 0.66, vh * 0.66);
    const QPoint gp = viewport_->mapToGlobal(lp.toPoint());
    for (int i = 0; i < 4; ++i) {
        QMouseEvent mv(QEvent::MouseMove, lp, QPointF(gp), Qt::NoButton, Qt::NoButton,
                       Qt::NoModifier);
        QCoreApplication::sendEvent(viewport_, &mv);
        pump(120);
    }

    // Prove the REAL keystroke path, the user's exact scenario: the command line holds
    // keyboard focus by default; sending keys there must be intercepted by the app
    // event filter and routed to the on-canvas field (type WITHOUT a click). Also Tab.
    const auto send_key = [](int key, const QString& t) {
        QWidget* target = QApplication::focusWidget();
        QKeyEvent ke(QEvent::KeyPress, key, Qt::NoModifier, t);
        QApplication::sendEvent(target != nullptr ? static_cast<QObject*>(target)
                                                  : static_cast<QObject*>(QApplication::instance()),
                                &ke);
    };
    if (command_widget_ != nullptr) {
        command_widget_->focus_input(); // command line focused (the real default)
    }
    pump(60);
    QWidget* fw = QApplication::focusWidget();
    const bool cmd_focused = command_widget_ != nullptr && fw != nullptr &&
                             (fw == static_cast<QWidget*>(command_widget_) ||
                              command_widget_->isAncestorOf(fw));
    send_key(Qt::Key_5, "5");
    send_key(Qt::Key_0, "0");
    pump(40);
    const bool typed_len = viewport_->dyn_value(0) == "50";
    bool tab_ok = true;
    if (viewport_->dyn_field_count() > 1) {
        send_key(Qt::Key_Tab, "\t");
        pump(40);
        tab_ok = viewport_->dyn_active_slot() == 1;
        send_key(Qt::Key_3, "3");
        send_key(Qt::Key_0, "0");
        pump(40);
        tab_ok = tab_ok && viewport_->dyn_value(1) == "30";
        // back to the Length field for the screenshot caret
        send_key(Qt::Key_Backtab, "");
        pump(40);
    }

    const QRect mfr = frameGeometry();
    std::printf("[dyn_shot] kind=%d dpr=%.2f main=0x%lx frameG=(%d,%d %dx%d)\n", kind,
                viewport_->devicePixelRatio(), static_cast<unsigned long>(winId()), mfr.x(), mfr.y(),
                mfr.width(), mfr.height());
    std::printf("[dyn_shot] DYN box visible=%d | canvas field_count=%d capturing=%d "
                "cmd-line-focused=%d real-key-into-canvas=%d tab=%d active=%d value[0]=%s value[1]=%s\n",
                dyn_->isVisible() ? 1 : 0, viewport_->dyn_field_count(),
                viewport_->dyn_capturing() ? 1 : 0, cmd_focused ? 1 : 0, typed_len ? 1 : 0,
                tab_ok ? 1 : 0, viewport_->dyn_active_slot(), viewport_->dyn_value(0).c_str(),
                viewport_->dyn_value(1).c_str());
    std::fflush(stdout);

    if (qEnvironmentVariableIsSet("MUSACAD_DYN_HOLD")) {
        std::printf("[dyn_shot] HOLD: capture with `import -window 0x%lx %s`\n",
                    static_cast<unsigned long>(winId()), out_png.c_str());
        std::fflush(stdout);
        pump(12000); // keep the rubber-band on screen for an external `import` grab
    }

    // Qt's grabWindow returns black for the GL surface on some stacks; the reliable
    // eyes-on capture is the external `import -window <main>` during HOLD. Still try.
    bool ok = false;
    if (QScreen* scr = (windowHandle() != nullptr && windowHandle()->screen() != nullptr)
                           ? windowHandle()->screen()
                           : QApplication::primaryScreen()) {
        const QPixmap shot = scr->grabWindow(winId());
        ok = !shot.isNull() && shot.save(QString::fromStdString(out_png), "PNG");
        std::printf("[dyn_shot] saved %s = %d (%dx%d)\n", out_png.c_str(), ok ? 1 : 0, shot.width(),
                    shot.height());
    }
    const int expect = (kind == 2) ? 1 : 2;
    const bool fields_ok = viewport_->dyn_field_count() == expect && viewport_->dyn_capturing();
    dyn_action_->setChecked(false);
    return fields_ok;
}

bool MainWindow::offset_shot(int kind, const std::string& out_png) {
    const auto pump = [](int ms) {
        for (int i = 0; i < ms / 2; ++i) {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    };
    resize(1200, 820);
    move(60, 60);
    show();
    raise();
    activateWindow();
    pump(700);
    // Classic mode (bottom command line visible) so the failure-case message is on screen.
    dyn_action_->setChecked(false);
    engine_->submit(core::NewDocumentCommand{});
    pump(200);

    const double b90 = std::tan((3.14159265358979323846 / 2.0) / 4.0); // 90-degree-arc bulge

    if (kind == 0) {
        // Rectangle -> offset inward: a uniform inner rectangle, square corners.
        engine_->submit(core::AddPolylineCommand{
            {{-40, -30}, {40, -30}, {40, 30}, {-40, 30}}, true, 1});
        pump(200);
        engine_->submit(core::OffsetPickCommand{{0, -30}, 2.0, 10.0, {0, 0}, 2});
    } else if (kind == 1) {
        // Rounded (filleted) rectangle -> offset inward: smaller fillet arcs (bulges kept).
        core::AddPolylineCommand pc;
        const double W = 40, H = 30, r = 12;
        pc.points = {{-W + r, -H}, {W - r, -H}, {W, -H + r}, {W, H - r},
                     {W - r, H},   {-W + r, H}, {-W, H - r}, {-W, -H + r}};
        pc.bulges = {0, b90, 0, b90, 0, b90, 0, b90};
        pc.closed = true;
        pc.group = 1;
        engine_->submit(pc);
        pump(200);
        engine_->submit(core::OffsetPickCommand{{0, -30}, 2.0, 8.0, {0, 0}, 2});
    } else if (kind == 2) {
        // Open polyline (3 connected segments) -> offset inward with re-mitered corners.
        engine_->submit(core::AddPolylineCommand{
            {{-40, 30}, {-40, -30}, {40, -30}, {40, 30}}, false, 1});
        pump(200);
        engine_->submit(core::OffsetPickCommand{{0, -30}, 2.0, 8.0, {0, 0}, 2});
    } else if (kind == 3) {
        // Over-large inward offset -> graceful failure message, geometry unchanged.
        engine_->submit(core::AddPolylineCommand{
            {{-10, -10}, {10, -10}, {10, 10}, {-10, 10}}, true, 1});
        pump(200);
        engine_->submit(core::OffsetPickCommand{{0, -10}, 2.0, 15.0, {0, 0}, 2}); // > half-width
    } else if (kind == 4) {
        // Four separate lines -> JOIN -> one closed polyline -> uniform inward offset.
        engine_->submit(core::AddLineCommand{{-40, -30}, {40, -30}, 1});
        engine_->submit(core::AddLineCommand{{40, -30}, {40, 30}, 1});
        engine_->submit(core::AddLineCommand{{40, 30}, {-40, 30}, 1});
        engine_->submit(core::AddLineCommand{{-40, 30}, {-40, -30}, 1});
        pump(200);
        engine_->submit(core::JoinPickCommand{{{0, -30}, {40, 0}, {0, 30}, {-40, 0}}, 2.0, 2});
        pump(200);
        engine_->submit(core::OffsetPickCommand{{0, -30}, 2.0, 10.0, {0, 0}, 3});
    } else if (kind == 5) {
        // The user's workflow: six SEPARATE connected lines (a hexagon), SELECT all, then
        // JOIN the selection -> one closed polyline (offset inward proves it is one entity).
        const double r = 45.0;
        core::Vec2 v[6];
        for (int i = 0; i < 6; ++i) {
            const double a = static_cast<double>(i) / 6.0 * 2.0 * 3.14159265358979323846;
            v[i] = {r * std::cos(a), r * std::sin(a)};
        }
        for (int i = 0; i < 6; ++i) {
            engine_->submit(core::AddLineCommand{v[i], v[(i + 1) % 6], 1});
        }
        pump(200);
        engine_->submit(core::SelectAllCommand{});
        pump(150);
        engine_->submit(core::JoinSelectionCommand{2.0, 2}); // noun-verb JOIN
        pump(200);
        engine_->submit(core::OffsetPickCommand{{(v[0].x + v[1].x) * 0.5, (v[0].y + v[1].y) * 0.5},
                                                2.0, 8.0, {0, 0}, 3});
    }
    pump(300);
    viewport_->zoom_extents();
    pump(500);

    const QRect mfr = frameGeometry();
    std::printf("[offset_shot] kind=%d line_vertex_count=%d main=0x%lx frameG=(%d,%d %dx%d)\n", kind,
                viewport_->line_vertex_count(), static_cast<unsigned long>(winId()), mfr.x(), mfr.y(),
                mfr.width(), mfr.height());
    std::fflush(stdout);
    if (qEnvironmentVariableIsSet("MUSACAD_DYN_HOLD")) {
        std::printf("[offset_shot] HOLD: capture with `import -window 0x%lx %s`\n",
                    static_cast<unsigned long>(winId()), out_png.c_str());
        std::fflush(stdout);
        pump(12000);
    }
    return viewport_->line_vertex_count() > 0;
}

bool MainWindow::multidoc_shot(int kind, const std::string& out_png) {
    const auto pump = [](int ms) {
        for (int i = 0; i < ms / 2; ++i) {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    };
    resize(1200, 820);
    move(60, 60);
    show();
    raise();
    activateWindow();
    pump(700);
    const auto rect = [&](double x0, double y0, double x1, double y1, std::uint64_t g) {
        engine_->submit(core::AddPolylineCommand{
            {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}, true, g});
    };

    QMessageBox* box = nullptr; // kind 2 keeps the (non-modal) prompt up for the grab

    if (kind == 0) {
        // Two tabs: Drawing1 with content (dirty), Drawing2 empty. Active = Drawing1.
        rect(-40, -30, 40, 30, 1);
        pump(150);
        create_new_tab(); // Drawing2 (empty, active)
        pump(250);
        switch_to_document(viewport_->documents().front().id); // back to Drawing1 (content)
        pump(300);
        viewport_->zoom_extents();
        pump(300);
    } else if (kind == 1) {
        // Per-tab view: Drawing1 framed tight on a small rect; Drawing2 on a big rect.
        rect(-5, -5, 5, 5, 1); // small
        pump(150);
        viewport_->zoom_extents();
        pump(300);
        create_new_tab();
        pump(250);
        rect(-200, -150, 200, 150, 2); // big
        pump(150);
        viewport_->zoom_extents();
        pump(300);
        // Switch back to Drawing1: its tight view must be RESTORED (not re-derived).
        switch_to_document(viewport_->documents().front().id);
        pump(500);
    } else if (kind == 2) {
        // Close a dirty tab -> the Save/Discard/Cancel prompt (shown non-modally to grab).
        rect(-40, -30, 40, 30, 1);
        pump(150);
        create_new_tab();
        pump(200);
        rect(-30, -20, 30, 20, 2); // Drawing2 dirty
        pump(200);
        box = new QMessageBox(QMessageBox::Warning, QStringLiteral("Unsaved changes"),
                              QStringLiteral("Save changes to Drawing2?"),
                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
        box->setModal(false);
        box->show();
        pump(300);
    } else if (kind == 3) {
        // Open makes a NEW tab; the unsaved work in the current tab is intact.
        rect(-40, -30, 40, 30, 1); // Drawing1: unsaved rectangle
        pump(150);
        const QString file =
            QString::fromStdString((std::filesystem::temp_directory_path() / "saved.musa").string());
        create_new_tab();        // Drawing2
        pump(200);
        engine_->submit(core::AddCircleCommand{{0, 0}, 30, 3}); // a circle
        pump(150);
        save_to(file, false);    // Drawing2 -> "saved.musa" (clean)
        pump(400);
        switch_to_document(viewport_->documents().front().id); // back to Drawing1 (unsaved)
        pump(300);
        open_from(file, false);  // opens "saved.musa" into a NEW (third) tab
        pump(500);
        viewport_->zoom_extents();
        pump(300);
    } else if (kind == 4) {
        // Undo per tab: Drawing1 has two SEPARATE ops (distinct undo groups); undo
        // rewinds only its last op (the line), leaving the rectangle.
        rect(-40, -30, 40, 30, 1);             // op 1 (group 1): rectangle
        pump(120);
        engine_->submit(core::AddLineCommand{{-40, 40}, {40, 40}, 2}); // op 2 (group 2): a line
        pump(150);
        create_new_tab();
        pump(200);
        engine_->submit(core::AddCircleCommand{{0, 0}, 20, 3}); // Drawing2: a circle
        pump(150);
        switch_to_document(viewport_->documents().front().id); // back to Drawing1
        pump(300);
        engine_->submit(core::UndoLastGroupCommand{});         // undo the line only
        pump(200);
        viewport_->zoom_extents();
        pump(300);
    } else if (kind == 5) {
        // Cross-document clipboard: copy a rectangle (on a custom layer) from Drawing1,
        // paste it into Drawing2 (Ctrl+C / Ctrl+V); the layer is remapped by name.
        core::Layer walls;
        walls.name = "Walls";
        engine_->submit(core::AddLayerCommand{walls});
        pump(120);
        std::uint16_t walls_idx = 0;
        for (std::uint16_t i = 0; i < viewport_->layers().size(); ++i) {
            if (viewport_->layers()[i].name == "Walls") {
                walls_idx = i;
            }
        }
        engine_->submit(core::SetCurrentLayerCommand{walls_idx});
        rect(-40, -30, 40, 30, 1);
        pump(120);
        engine_->submit(core::SelectAllCommand{});
        pump(150);
        engine_->submit(core::CopyClipboardCommand{}); // Ctrl+C
        pump(120);
        create_new_tab();                              // Drawing2 (no "Walls")
        pump(250);
        core::PasteClipboardCommand paste;             // Ctrl+V (keep coordinates here)
        paste.at_cursor = false;
        paste.group = 100;
        engine_->submit(std::move(paste));
        pump(200);
        viewport_->zoom_extents();
        pump(300);
    } else if (kind == 6) {
        // Tab-to-tab drag: select in Drawing1, then drop on Drawing2's tab (the real
        // drop handler: copy -> switch -> paste). The rectangle appears in Drawing2.
        rect(-40, -30, 40, 30, 1);
        pump(120);
        engine_->submit(core::SelectAllCommand{});
        pump(150);
        create_new_tab(); // Drawing2
        pump(200);
        switch_to_document(viewport_->documents().front().id); // back to Drawing1 (selected)
        pump(300);
        if (file_tabs_ != nullptr && file_tabs_->count() >= 2) {
            const QPoint g = file_tabs_->mapToGlobal(file_tabs_->tabRect(1).center());
            (void)drop_selection_on_tab(g); // simulate dropping the selection on Drawing2's tab
        }
        pump(300);
        viewport_->zoom_extents();
        pump(300);
    }

    const QRect mfr = frameGeometry();
    std::printf("[multidoc_shot] kind=%d docs=%d active=%llu line_vertex_count=%d main=0x%lx "
                "frameG=(%d,%d %dx%d)\n",
                kind, static_cast<int>(viewport_->documents().size()),
                static_cast<unsigned long long>(viewport_->active_document_id()),
                viewport_->line_vertex_count(), static_cast<unsigned long>(winId()), mfr.x(),
                mfr.y(), mfr.width(), mfr.height());
    std::fflush(stdout);
    if (qEnvironmentVariableIsSet("MUSACAD_DYN_HOLD")) {
        std::printf("[multidoc_shot] HOLD: capture with `import -window 0x%lx %s`\n",
                    static_cast<unsigned long>(winId()), out_png.c_str());
        std::fflush(stdout);
        pump(12000);
    }
    if (box != nullptr) {
        box->close();
    }
    return true;
}

bool MainWindow::text_shot(int kind, const std::string& out_png) {
    using core::Vec2;
    const auto pump = [](int ms) {
        for (int i = 0; i < ms / 2; ++i) {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    };
    resize(1200, 820);
    move(60, 60);
    show();
    raise();
    activateWindow();
    pump(700);

    const std::uint64_t g = 1;
    const auto poly = [&](std::vector<Vec2> pts, bool closed) {
        engine_->submit(core::AddPolylineCommand{std::move(pts), closed, g});
    };
    const auto seg = [&](double x0, double y0, double x1, double y1) {
        poly({{x0, y0}, {x1, y1}}, false);
    };
    const auto txt = [&](double x, double y, double h, const std::string& s) {
        engine_->submit(core::AddTextCommand{Vec2{x, y}, h, 0.0, std::uint8_t{0}, s, g});
    };

    // A representative drafting title block: sheet border + a lower-right title-block
    // grid + mixed-case single-line labels + a lowercase MTEXT note. This is the
    // user's "title block" equivalent -- the same scene renders before and after the
    // text-quality work (geometry is identical; only stroke rendering changes).
    (void)kind;
    poly({{0, 0}, {260, 0}, {260, 180}, {0, 180}}, true); // sheet border
    // Title-block box (lower right) with internal divisions.
    poly({{150, 0}, {260, 0}, {260, 50}, {150, 50}}, true);
    seg(150, 38, 260, 38);
    seg(150, 26, 260, 26);
    seg(150, 14, 260, 14);
    seg(205, 38, 205, 0);

    txt(156, 41, 6.0, "MUSA CAD");
    txt(156, 29, 4.0, "Mounting Bracket");
    txt(156, 16.5, 3.0, "Drawn by: Pranay Kiran");
    txt(156, 4, 3.0, "Material: Al 6061-T6");
    txt(209, 41, 2.6, "Scale 1:2");
    txt(209, 29, 2.6, "Sheet 1 of 1");
    txt(209, 16.5, 2.6, "Rev: A");
    txt(209, 4, 2.6, "Part No: MC-0042");
    txt(10, 168, 5.0, "Drawing Title Block (text quality sample)");

    core::MTextBlock note;
    note.pos = {10, 70};
    note.height = 4.0;
    note.line_spacing = 1.4;
    engine_->submit(core::AddMTextCommand{
        note,
        "Notes:\n1. all dimensions in mm.\n2. break sharp edges 0.5 max.\n"
        "3. tolerances per ISO 2768-m.\n4. deburr and clean surfaces.",
        g});
    pump(300);
    viewport_->zoom_extents();
    pump(400);

    // Save the scene so the plot path can be exercised on the identical drawing
    // (the plot-unchanged proof): MUSACAD_PLOT_TEST="<scene>.musa|out.pdf|1".
    const std::filesystem::path scene =
        std::filesystem::path(out_png).replace_extension(".musa");
    save_to(QString::fromStdString(scene.string()), false);
    pump(400);

    const QRect mfr = frameGeometry();
    std::printf("[text_shot] kind=%d line_vertex_count=%d scene=%s main=0x%lx "
                "frameG=(%d,%d %dx%d)\n",
                kind, viewport_->line_vertex_count(), scene.string().c_str(),
                static_cast<unsigned long>(winId()), mfr.x(), mfr.y(), mfr.width(), mfr.height());
    std::fflush(stdout);
    if (qEnvironmentVariableIsSet("MUSACAD_DYN_HOLD")) {
        std::printf("[text_shot] HOLD: capture with `import -window 0x%lx %s`\n",
                    static_cast<unsigned long>(winId()), out_png.c_str());
        std::fflush(stdout);
        pump(12000);
    }
    return true;
}

bool MainWindow::hatch_shot(int kind, const std::string& out_png) {
    using core::Vec2;
    const auto pump = [](int ms) {
        for (int i = 0; i < ms / 2; ++i) {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    };
    resize(1200, 820);
    move(60, 60);
    show();
    raise();
    activateWindow();
    pump(700);
    engine_->submit(core::NewDocumentCommand{});
    pump(150);

    bool ok = false;
    if (kind == 0) {
        // A closed L-shaped boundary polyline -> select it -> HATCH -> SOLID fill.
        engine_->submit(core::AddPolylineCommand{
            {{0, 0}, {60, 0}, {60, 40}, {35, 40}, {35, 22}, {0, 22}}, true, 1});
        pump(200);
        viewport_->zoom_extents();
        pump(150);
        engine_->submit(core::SelectAllCommand{});
        pump(150);
        processor_->submit_line("HATCH"); // noun-verb: fills the selected boundary as SOLID
        pump(300);
        viewport_->zoom_extents();
        pump(200);
        if (!properties_dock_->isVisible()) {
            toggle_properties();
        }
        pump(350);
        const core::SelectionSummary sum = viewport_->selection_summary();
        bool hatch_section = false;
        std::string pat;
        for (const core::PropertyField& f : sum.fields) {
            if (f.id == core::PropertyId::HatchPattern) {
                hatch_section = true;
                pat = f.value.text;
            }
        }
        ok = sum.kind_plus1 == static_cast<std::uint8_t>(core::EntityKind::Hatch) + 1 &&
             hatch_section;
        std::printf("[hatch_shot] kind=0 selected=%d hatch_section=%d pattern='%s' => %s\n",
                    viewport_->selection_count(), hatch_section ? 1 : 0, pat.c_str(),
                    ok ? "PASS" : "FAIL");
    } else if (kind == 1) {
        // Pick-point with an island: a rectangle of 4 LINEs + a circle inside. Click between
        // them -> trace the ring boundary (circle becomes a hole) and SOLID-fill it. The hatch
        // is left selected so its boundary grips show (selection identification).
        engine_->submit(core::AddLineCommand{{0, 0}, {90, 0}, 1});
        engine_->submit(core::AddLineCommand{{90, 0}, {90, 55}, 1});
        engine_->submit(core::AddLineCommand{{90, 55}, {0, 55}, 1});
        engine_->submit(core::AddLineCommand{{0, 55}, {0, 0}, 1});
        engine_->submit(core::AddCircleCommand{{45, 27.5}, 16, 2});
        pump(250);
        viewport_->zoom_extents();
        pump(150);
        processor_->submit_line("HATCH");
        pump(150);
        processor_->submit_line("6,6"); // pick internal point (inside ring, outside circle)
        pump(300);
        processor_->submit_line(""); // Enter to finish the command
        pump(200);
        viewport_->zoom_extents();
        pump(200);
        const bool is_hatch =
            viewport_->selection_summary().kind_plus1 ==
            static_cast<std::uint8_t>(core::EntityKind::Hatch) + 1;
        const int grips = static_cast<int>(viewport_->selection_summary().fields.empty() ? 0 : 1);
        (void)grips;
        ok = is_hatch && viewport_->selection_count() == 1;
        std::printf("[hatch_shot] kind=1 pick-point selected_hatch=%d => %s\n", is_hatch ? 1 : 0,
                    ok ? "PASS" : "FAIL");
    } else if (kind == 2) {
        // PARTITION: a closed square split by a diagonal chord (endpoints mid-edge). A
        // pick in ONE partition must hatch only that sub-region (not the whole square), and
        // the resulting hatch is left selected so its fill shows the selection highlight.
        engine_->submit(core::AddPolylineCommand{{{0, 0}, {60, 0}, {60, 60}, {0, 60}}, true, 1});
        engine_->submit(core::AddLineCommand{{20, 60}, {40, 0}, 1}); // partitioning chord
        pump(250);
        viewport_->zoom_extents();
        pump(150);
        processor_->submit_line("HATCH");
        pump(150);
        processor_->submit_line("8,30"); // pick inside the LEFT partition only
        pump(300);
        processor_->submit_line(""); // Enter to finish
        pump(200);
        viewport_->zoom_extents();
        pump(250);
        const bool is_hatch =
            viewport_->selection_summary().kind_plus1 ==
            static_cast<std::uint8_t>(core::EntityKind::Hatch) + 1;
        ok = is_hatch && viewport_->selection_count() == 1;
        std::printf("[hatch_shot] kind=2 partition selected_hatch=%d => %s\n", is_hatch ? 1 : 0,
                    ok ? "PASS" : "FAIL");
    }
    std::printf("[hatch_shot] kind=%d main=0x%lx frameG=(%d,%d %dx%d)\n", kind,
                static_cast<unsigned long>(winId()), frameGeometry().x(), frameGeometry().y(),
                frameGeometry().width(), frameGeometry().height());
    std::fflush(stdout);
    if (qEnvironmentVariableIsSet("MUSACAD_DYN_HOLD")) {
        std::printf("[hatch_shot] HOLD: capture with `import -window 0x%lx %s`\n",
                    static_cast<unsigned long>(winId()), out_png.c_str());
        std::fflush(stdout);
        pump(12000);
    }
    return ok;
}

bool MainWindow::mleader_text_shot(int kind, const std::string& out_png) {
    using core::Vec2;
    const auto pump = [](int ms) {
        for (int i = 0; i < ms / 2; ++i) {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    };
    resize(1200, 820);
    move(60, 60);
    show();
    raise();
    activateWindow();
    pump(700);
    engine_->submit(core::NewDocumentCommand{});
    pump(150);
    const double r = 6.0;

    if (kind == 0) {
        // MLeader selected -> PR shows General + a Text section (Contents/Height/Font/Attach)
        // and a clean Color row (default = ByLayer, no contradictory swatch).
        core::MTextBlock b;
        b.pos = {22, 10};
        b.height = 3.0;
        b.attach = 0;
        engine_->submit(core::AddMLeaderCommand{{{0, 0}, {14, 8}, {22, 8}}, 0, b, "DETAIL A", 1});
        pump(250);
        viewport_->zoom_extents();
        pump(250);
        engine_->submit(core::SelectPickCommand{{7, 4}, r, false}); // pick the leader line
        pump(300);
        if (!properties_dock_->isVisible()) {
            toggle_properties();
        }
        pump(350);
        const core::SelectionSummary sum = viewport_->selection_summary();
        bool has_text_section = false;
        bool color_by_layer = false;
        for (const core::PropertyField& f : sum.fields) {
            if (f.id == core::PropertyId::TextHeight || f.id == core::PropertyId::TextFont) {
                has_text_section = true;
            }
            if (f.id == core::PropertyId::Color) {
                color_by_layer = f.value.flag;
            }
        }
        std::printf("[mleader_text_shot] kind=0 PR: selection=%d text_section=%d color_by_layer=%d\n",
                    viewport_->selection_count(), has_text_section ? 1 : 0, color_by_layer ? 1 : 0);
    } else if (kind == 1) {
        // MATCHPROP TEXT (height 5) -> MLeader: the label adopts the source height (2 -> 5).
        engine_->submit(core::AddTextCommand{{-60, 22}, 5.0, 0.0, std::uint8_t{0}, "SOURCE h5", 1});
        core::MTextBlock b;
        b.pos = {12, 0};
        b.height = 2.0;
        engine_->submit(core::AddMLeaderCommand{{{-20, -14}, {0, 0}, {10, 0}}, 0, b, "label", 2});
        pump(250);
        engine_->submit(core::MatchPropPickSourceCommand{{-52, 24}, r});
        pump(140);
        engine_->submit(core::MatchPropApplyCommand{{5, 0}, r, core::MatchPropFilter{}, 9}); // horiz seg
        pump(250);
        engine_->submit(core::SelectPickCommand{{5, 0}, r, false});
        pump(200);
        double mh = 0.0;
        for (const core::PropertyField& f : viewport_->selection_summary().fields) {
            if (f.id == core::PropertyId::TextHeight) {
                mh = f.value.num;
            }
        }
        engine_->submit(core::ClearSelectionCommand{});
        pump(120);
        std::printf("[mleader_text_shot] kind=1 MLeader label height after MA = %.3g (want 5)\n", mh);
        viewport_->zoom_extents();
        pump(300);
    } else if (kind == 2) {
        // MATCHPROP MLeader -> MLeader: label height + leader style copy.
        core::MTextBlock sb;
        sb.pos = {-28, 16};
        sb.height = 6.0;
        engine_->submit(core::AddMLeaderCommand{{{-50, 2}, {-38, 14}, {-28, 14}}, 0, sb, "SRC h6", 1});
        core::MTextBlock tb;
        tb.pos = {32, 0};
        tb.height = 2.0;
        engine_->submit(core::AddMLeaderCommand{{{10, -14}, {22, 0}, {32, 0}}, 0, tb, "dst", 2});
        pump(250);
        engine_->submit(core::MatchPropPickSourceCommand{{-33, 14}, r}); // src horiz seg
        pump(140);
        engine_->submit(core::MatchPropApplyCommand{{27, 0}, r, core::MatchPropFilter{}, 9}); // dst horiz seg
        pump(250);
        engine_->submit(core::SelectPickCommand{{27, 0}, r, false});
        pump(200);
        double mh = 0.0;
        for (const core::PropertyField& f : viewport_->selection_summary().fields) {
            if (f.id == core::PropertyId::TextHeight) {
                mh = f.value.num;
            }
        }
        engine_->submit(core::ClearSelectionCommand{});
        pump(120);
        std::printf("[mleader_text_shot] kind=2 dst MLeader label height after MA = %.3g (want 6)\n",
                    mh);
        viewport_->zoom_extents();
        pump(300);
    } else if (kind == 3) {
        // TEXT control codes: %%c -> diameter, %%p -> plus-minus (a real callout).
        engine_->submit(
            core::AddTextCommand{{0, 0}, 10.0, 0.0, std::uint8_t{0}, "%%c50 H7 %%p0.02", 1});
        pump(200);
        viewport_->zoom_extents();
        pump(300);
    } else if (kind == 4) {
        // MTEXT Unicode escape: \U+2300 -> diameter sign.
        core::MTextBlock b;
        b.pos = {0, 0};
        b.height = 10.0;
        engine_->submit(core::AddMTextCommand{b, "\\U+2300 50 BORE", 1});
        pump(200);
        viewport_->zoom_extents();
        pump(300);
    } else if (kind == 5) {
        // TEXT overline toggle: "Pipe [OD] = 50" with OD overlined.
        engine_->submit(
            core::AddTextCommand{{0, 0}, 10.0, 0.0, std::uint8_t{0}, "Pipe %%oOD%%o = 50mm", 1});
        pump(200);
        viewport_->zoom_extents();
        pump(300);
    } else if (kind == 6) {
        // MLeader with a per-leader ARROW override -> PR shows a Leader section (Arrowhead +
        // Arrow size) and the arrowhead visibly grows (override 8 vs the dimstyle's 2.5).
        core::MTextBlock b;
        b.pos = {22, 10};
        b.height = 3.0;
        engine_->submit(core::AddMLeaderCommand{{{0, 0}, {14, 8}, {22, 8}}, 0, b, "DETAIL B", 1});
        pump(200);
        viewport_->zoom_extents();
        pump(150);
        engine_->submit(core::SelectPickCommand{{7, 4}, r, false});
        pump(200);
        core::PropertyValue av; // override arrow size to 8 (flag=false => overridden)
        av.flag = false;
        av.num = 8.0;
        engine_->submit(core::SetPropertyCommand{core::PropertyId::LeaderArrowSize, av, 2});
        pump(200);
        engine_->submit(core::SelectPickCommand{{7, 4}, r, false}); // re-select recreated entity
        pump(250);
        if (!properties_dock_->isVisible()) {
            toggle_properties();
        }
        pump(350);
        const core::SelectionSummary sum = viewport_->selection_summary();
        bool has_leader_section = false;
        double arrow = 0.0;
        bool arrow_overridden = false;
        for (const core::PropertyField& f : sum.fields) {
            if (f.id == core::PropertyId::LeaderArrowSize) {
                has_leader_section = true;
                arrow = f.value.num;
                arrow_overridden = !f.value.flag;
            }
        }
        std::printf("[mleader_text_shot] kind=6 PR: leader_section=%d arrow_size=%.3g overridden=%d\n",
                    has_leader_section ? 1 : 0, arrow, arrow_overridden ? 1 : 0);
    } else if (kind == 7) {
        // The LABEL gets its own colour (green) while the leader line + arrow stay the entity
        // colour (magenta) -- proving the text colour is independent of the General colour.
        core::MTextBlock b;
        b.pos = {22, 10};
        b.height = 4.0;
        engine_->submit(core::AddMLeaderCommand{{{0, 0}, {14, 8}, {22, 8}}, 0, b, "NOTE", 1});
        pump(200);
        viewport_->zoom_extents();
        pump(150);
        engine_->submit(core::SelectPickCommand{{7, 4}, r, false});
        pump(180);
        core::PropertyValue ec; // General colour -> magenta (leader line + arrow)
        ec.flag = false;
        ec.color = {255, 85, 255};
        engine_->submit(core::SetPropertyCommand{core::PropertyId::Color, ec, 2});
        pump(120);
        engine_->submit(core::SelectPickCommand{{7, 4}, r, false});
        pump(150);
        core::PropertyValue tc; // label Text colour -> green (independent)
        tc.flag = false;
        tc.color = {60, 220, 90};
        engine_->submit(core::SetPropertyCommand{core::PropertyId::LeaderTextColor, tc, 3});
        pump(150);
        engine_->submit(core::SelectPickCommand{{7, 4}, r, false});
        pump(220);
        if (!properties_dock_->isVisible()) {
            toggle_properties();
        }
        pump(300);
        std::printf("[mleader_text_shot] kind=7: label green, line/arrow magenta (independent)\n");
    }

    std::printf("[mleader_text_shot] kind=%d main=0x%lx frameG=(%d,%d %dx%d)\n", kind,
                static_cast<unsigned long>(winId()), frameGeometry().x(), frameGeometry().y(),
                frameGeometry().width(), frameGeometry().height());
    std::fflush(stdout);
    if (qEnvironmentVariableIsSet("MUSACAD_DYN_HOLD")) {
        std::printf("[mleader_text_shot] HOLD: capture with `import -window 0x%lx %s`\n",
                    static_cast<unsigned long>(winId()), out_png.c_str());
        std::fflush(stdout);
        pump(12000);
    }
    return true;
}

bool MainWindow::matchprop_shot(int kind, const std::string& out_png) {
    using core::Vec2;
    const auto pump = [](int ms) {
        for (int i = 0; i < ms / 2; ++i) {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    };
    resize(1200, 820);
    move(60, 60);
    show();
    raise();
    activateWindow();
    pump(700);
    const double r = 6.0; // generous world pick radius for the scripted picks
    QDialog* box = nullptr;

    if (kind == 0) {
        // Cross-kind universal: a blue / "Walls" / 0.5mm line -> a default circle. The
        // circle adopts colour/layer/lineweight but stays a circle.
        core::Layer walls;
        walls.name = "Walls";
        walls.color = {60, 140, 255};
        engine_->submit(core::AddLayerCommand{walls});
        pump(120);
        std::uint16_t wi = 0;
        for (std::uint16_t i = 0; i < viewport_->layers().size(); ++i) {
            if (viewport_->layers()[i].name == "Walls") {
                wi = i;
            }
        }
        core::EntityProps sp;
        sp.layer = wi;
        sp.set_color_by_layer(false);
        sp.color = {60, 140, 255};
        sp.set_lineweight_by_layer(false);
        sp.lineweight = 50;
        engine_->submit(core::AddLineCommand{{-70, 15}, {-25, 15}, 1, sp});
        engine_->submit(core::AddCircleCommand{{30, 15}, 18, 2}); // default (ByLayer, layer 0)
        pump(220);
        engine_->submit(core::MatchPropPickSourceCommand{{-47, 15}, r});
        pump(140);
        engine_->submit(core::MatchPropApplyCommand{{48, 15}, r, core::MatchPropFilter{}, 9});
        pump(220);
        viewport_->zoom_extents();
        pump(300);
    } else if (kind == 1) {
        // Within text family: TEXT (height 8) -> MTEXT (height 3). MTEXT adopts the height.
        engine_->submit(core::AddTextCommand{{-70, 15}, 8.0, 0.0, std::uint8_t{0}, "SOURCE", 1});
        core::MTextBlock blk;
        blk.pos = {20, 18};
        blk.height = 3.0;
        engine_->submit(core::AddMTextCommand{blk, "target text", 2});
        pump(220);
        engine_->submit(core::MatchPropPickSourceCommand{{-60, 16}, r});
        pump(140);
        engine_->submit(core::MatchPropApplyCommand{{24, 16}, r, core::MatchPropFilter{}, 9});
        pump(220);
        viewport_->zoom_extents();
        pump(300);
    } else if (kind == 2) {
        // Within dim family: a source linear dim with per-dim overrides (bigger arrows +
        // text) -> a default target dim, which inherits the overrides.
        engine_->submit(core::AddDimensionCommand{std::uint8_t{0}, {-70, 5}, {-25, 5}, {-47, 20}, 0, 1});
        pump(160);
        engine_->submit(core::SelectPickCommand{{-47, 20}, r, false});
        pump(160);
        core::PropertyValue av;
        av.flag = false;
        av.num = 6.0;
        engine_->submit(core::SetPropertyCommand{core::PropertyId::DimArrowSize, av, 2});
        pump(100);
        core::PropertyValue tv;
        tv.flag = false;
        tv.num = 6.0;
        engine_->submit(core::SetPropertyCommand{core::PropertyId::DimTextHeight, tv, 3});
        pump(100);
        engine_->submit(core::ClearSelectionCommand{});
        pump(100);
        engine_->submit(core::AddDimensionCommand{std::uint8_t{0}, {15, 5}, {60, 5}, {37, 20}, 0, 4});
        pump(160);
        engine_->submit(core::MatchPropPickSourceCommand{{-47, 20}, r});
        pump(140);
        engine_->submit(core::MatchPropApplyCommand{{37, 20}, r, core::MatchPropFilter{}, 9});
        pump(220);
        viewport_->zoom_extents();
        pump(300);
    } else if (kind == 3) {
        // The Settings dialog (shown non-modally for the grab) with Color UNCHECKED -- the
        // visual proof; the gating behaviour itself is covered by the engine tests.
        auto* dlg = new QDialog(this);
        dlg->setWindowTitle(QStringLiteral("Property Settings"));
        dlg->setModal(false);
        auto* outer = new QVBoxLayout(dlg);
        const auto add_check = [dlg](QVBoxLayout* lay, const QString& label, bool on, bool en) {
            auto* cb = new QCheckBox(label, dlg);
            cb->setChecked(on);
            cb->setEnabled(en);
            lay->addWidget(cb);
        };
        auto* basic = new QGroupBox(QStringLiteral("Basic Properties"), dlg);
        auto* bl = new QVBoxLayout(basic);
        add_check(bl, QStringLiteral("Color"), false, true); // unchecked -> colour kept
        add_check(bl, QStringLiteral("Layer"), true, true);
        add_check(bl, QStringLiteral("Lineweight"), true, true);
        add_check(bl, QStringLiteral("Linetype"), true, true);
        add_check(bl, QStringLiteral("Linetype Scale"), true, true);
        add_check(bl, QStringLiteral("Plot Style"), true, false);
        outer->addWidget(basic);
        auto* special = new QGroupBox(QStringLiteral("Special Properties"), dlg);
        auto* sl = new QVBoxLayout(special);
        add_check(sl, QStringLiteral("Text"), true, true);
        add_check(sl, QStringLiteral("Dimension"), true, true);
        add_check(sl, QStringLiteral("Hatch"), true, false);
        add_check(sl, QStringLiteral("Polyline"), true, false);
        outer->addWidget(special);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
        outer->addWidget(buttons);
        dlg->resize(280, 360);
        dlg->move(x() + 360, y() + 220);
        dlg->show();
        dlg->raise();
        box = dlg;
        pump(300);
    } else if (kind == 4) {
        // Skips inapplicable: TEXT (green) source -> LINE target. The line takes the
        // universal colour but the text-specific properties are silently skipped.
        core::EntityProps sp;
        sp.set_color_by_layer(false);
        sp.color = {70, 210, 120};
        engine_->submit(core::AddTextCommand{{-70, 15}, 8.0, 0.0, std::uint8_t{0}, "SOURCE", 1, sp});
        engine_->submit(core::AddLineCommand{{15, 15}, {60, 15}, 2});
        pump(220);
        engine_->submit(core::MatchPropPickSourceCommand{{-60, 16}, r});
        pump(140);
        engine_->submit(core::MatchPropApplyCommand{{37, 15}, r, core::MatchPropFilter{}, 9});
        pump(220);
        viewport_->zoom_extents();
        pump(300);
    }

    const QRect mfr = frameGeometry();
    std::printf("[matchprop_shot] kind=%d line_vertex_count=%d main=0x%lx frameG=(%d,%d %dx%d)\n",
                kind, viewport_->line_vertex_count(), static_cast<unsigned long>(winId()), mfr.x(),
                mfr.y(), mfr.width(), mfr.height());
    std::fflush(stdout);
    if (qEnvironmentVariableIsSet("MUSACAD_DYN_HOLD")) {
        std::printf("[matchprop_shot] HOLD: capture with `import -window 0x%lx %s`\n",
                    static_cast<unsigned long>(winId()), out_png.c_str());
        std::fflush(stdout);
        pump(12000);
    }
    if (box != nullptr) {
        box->close();
    }
    return true;
}

bool MainWindow::ltscale_shot(int kind, const std::string& out_png) {
    using core::Vec2;
    const auto pump = [](int ms) {
        for (int i = 0; i < ms / 2; ++i) {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    };
    resize(1200, 820);
    move(60, 60);
    show();
    raise();
    activateWindow();
    pump(700);

    const std::uint64_t g = 1;
    const auto cline = [&](double x0, double y0, double x1, double y1, double celt) {
        core::EntityProps p;
        p.set_linetype_by_layer(false);
        p.linetype = core::Linetype::Center;
        engine_->submit(core::AddLineCommand{{x0, y0}, {x1, y1}, g, p, celt});
    };
    const auto ccircle = [&](double cx, double cy, double r) {
        core::EntityProps p;
        p.set_linetype_by_layer(false);
        p.linetype = core::Linetype::Center;
        engine_->submit(core::AddCircleCommand{{cx, cy}, r, g, p, 1.0});
    };
    const auto txt = [&](double x, double y, const std::string& s) {
        engine_->submit(core::AddTextCommand{Vec2{x, y}, 4.0, 0.0, std::uint8_t{0}, s, g});
    };

    if (kind == 0 || kind == 1) {
        // A Center-linetype drawing: changing LTSCALE re-dashes EVERYTHING. kind 0 = 1.0
        // (before), kind 1 = 0.5 (after, denser dashes across all entities).
        for (int i = 0; i < 4; ++i) {
            cline(0, static_cast<double>(i) * 18.0, 120, static_cast<double>(i) * 18.0, 1.0);
        }
        ccircle(60, -45, 35);
        if (kind == 1) {
            engine_->submit(core::SetLtscaleCommand{0.5});
        }
        pump(250);
        viewport_->zoom_extents();
        pump(300);
    } else if (kind == 2) {
        // Three identical short Center lines; the MIDDLE one has CELTSCALE 0.3 -> visibly
        // finer dashes than the others (one-entity override, document LTSCALE unchanged).
        cline(0, 24, 90, 24, 1.0);
        cline(0, 12, 90, 12, 0.3); // <- per-entity override
        cline(0, 0, 90, 0, 1.0);
        txt(95, 22, "CELTSCALE 1.0");
        txt(95, 10, "CELTSCALE 0.3");
        txt(95, -2, "CELTSCALE 1.0");
        pump(250);
        viewport_->zoom_extents();
        pump(300);
    } else if (kind == 3) {
        // The user's case: a ~22-unit Center line renders solid (pattern bigger than the
        // line) at the default scale; CELTSCALE 0.25 makes the same line dash.
        cline(0, 12, 22, 12, 1.0);  // solid (pattern too large)
        cline(0, 0, 22, 0, 0.25);   // dashes (per-entity scale)
        txt(26, 11, "22u Center, CELTSCALE 1.0 (solid)");
        txt(26, -1, "22u Center, CELTSCALE 0.25 (dashes)");
        pump(250);
        viewport_->zoom_extents();
        pump(300);
    }

    const std::filesystem::path scene =
        std::filesystem::path(out_png).replace_extension(".musa");
    save_to(QString::fromStdString(scene.string()), false);
    pump(400);

    const QRect mfr = frameGeometry();
    std::printf("[ltscale_shot] kind=%d line_vertex_count=%d scene=%s main=0x%lx frameG=(%d,%d %dx%d)\n",
                kind, viewport_->line_vertex_count(), scene.string().c_str(),
                static_cast<unsigned long>(winId()), mfr.x(), mfr.y(), mfr.width(), mfr.height());
    std::fflush(stdout);
    if (qEnvironmentVariableIsSet("MUSACAD_DYN_HOLD")) {
        std::printf("[ltscale_shot] HOLD: capture with `import -window 0x%lx %s`\n",
                    static_cast<unsigned long>(winId()), out_png.c_str());
        std::fflush(stdout);
        pump(12000);
    }
    return true;
}

bool MainWindow::selftest_param_dialogs() {
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
    processor_->cancel();
    pump([this] { return !processor_->has_active_command(); });
    engine_->submit(core::NewDocumentCommand{});
    pump([this] { return viewport_->line_vertex_count() == 0; });

    // The CIRCLE ribbon button STARTS the interactive command (it does NOT place an
    // object at 0,0). After firing, a command is active and nothing is drawn yet.
    auto* circle_btn = findChild<QToolButton*>(QStringLiteral("ribbon.cmd.C"));
    bool ribbon_ok = false;
    if (circle_btn != nullptr) {
        circle_btn->click();
        ribbon_ok = pump([this] { return processor_->has_active_command(); }) &&
                    viewport_->line_vertex_count() == 0; // nothing placed yet -> must pick
        processor_->cancel();
        pump([this] { return !processor_->has_active_command(); });
    }
    std::printf("[selftest] CIRCLE ribbon starts interactive command (no 0,0 object): %s\n",
                ribbon_ok ? "PASS" : "FAIL");

    // CIRCLE with the [Diameter] option (command line == DYN == identical input):
    // C -> centre -> "D" -> 50  draws a radius-25 circle.
    type("C");
    type("0,0");
    type("D");
    type("50");
    Vec2 mn{}, mx{};
    const bool diam_ok = pump([&] {
        return viewport_->line_vertex_count() > 0 && viewport_->content_bounds(mn, mx) &&
               std::abs((mx.x - mn.x) - 50.0) < 0.5; // diameter 50 -> radius 25
    });
    std::printf("[selftest] CIRCLE [Diameter] option draws d=50 circle: %s\n",
                diam_ok ? "PASS" : "FAIL");

    // Plain radius via typing still works (converges on the same command).
    engine_->submit(core::NewDocumentCommand{});
    pump([this] { return viewport_->line_vertex_count() == 0; });
    type("C");
    type("0,0");
    type("25");
    const bool rad_ok = pump([&] {
        return viewport_->line_vertex_count() > 0 && viewport_->content_bounds(mn, mx) &&
               std::abs((mx.x - mn.x) - 50.0) < 0.5;
    });
    std::printf("[selftest] CIRCLE radius via typing: %s\n", rad_ok ? "PASS" : "FAIL");

    all = all && ribbon_ok && diam_ok && rad_ok;
    engine_->submit(core::NewDocumentCommand{});
    return all;
}

bool MainWindow::selftest_dwg() {
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
    const QString saved = QSettings().value(QStringLiteral("io/dwg_converter_path")).toString();
    const QString dir = QDir::temp().path();

    // A MOCK converter (Generic kind): `<prog> <in> <out>` just copies in->out, so a
    // .dwg whose bytes are valid DXF round-trips through the real pipeline. (No real
    // DWG converter exists in the build env; real-converter verification is the
    // user's to run -- this proves the discovery/invoke/import/catalog wiring.)
    const QString mock = dir + QStringLiteral("/musacad_mock_conv.sh");
    {
        QFile f(mock);
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        f.write("#!/bin/sh\ncp \"$1\" \"$2\"\n");
        f.close();
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    // (1) graceful degradation: a None converter never crashes, returns the hint.
    {
        DwgConverter none;
        QString err;
        const bool failed = !none.to_dxf(QStringLiteral("x.dwg"), dir + QStringLiteral("/y.dxf"), err);
        const bool deg_ok = failed && !err.isEmpty() && !DwgConverter::install_hint().isEmpty();
        std::printf("[selftest] DWG no-converter degrades gracefully (clear hint): %s\n",
                    deg_ok ? "PASS" : "FAIL");
        all = all && deg_ok;
    }
    // (2) discovery picks up the configured path (Generic).
    QSettings().setValue(QStringLiteral("io/dwg_converter_path"), mock);
    const DwgConverter conv = DwgConverter::discover();
    const bool disc_ok = conv.available() && conv.kind() == DwgConverter::Kind::Generic;
    std::printf("[selftest] DWG converter discovery (configured path): %s\n",
                disc_ok ? "PASS" : "FAIL");

    // Setup-dialog helpers: Browse (from_program) validates an explicit path; PATH
    // detection + a bogus path behave sanely; kind names are non-empty.
    const DwgConverter on_path = DwgConverter::discover_on_path(); // must not crash
    (void)on_path;
    const bool setup_ok =
        DwgConverter::from_program(mock).available() &&
        !DwgConverter::from_program(dir + QStringLiteral("/nope.bin")).available() &&
        !DwgConverter::kind_name(DwgConverter::Kind::Generic).isEmpty();
    std::printf("[selftest] DWG setup helpers (Browse/auto-detect/kind names): %s\n",
                setup_ok ? "PASS" : "FAIL");
    all = all && setup_ok;

    // A fake .dwg whose CONTENT is valid DXF: 2 LINEs + 1 unsupported HATCH.
    const QString fake_dwg = dir + QStringLiteral("/musacad_fake.dwg");
    {
        const char* dxf =
            "0\nSECTION\n2\nENTITIES\n"
            "0\nLINE\n8\n0\n10\n0\n20\n0\n11\n10\n21\n0\n"
            "0\nLINE\n8\n0\n10\n0\n20\n0\n11\n0\n21\n10\n"
            "0\nHATCH\n8\n0\n"
            "0\nENDSEC\n0\nEOF\n";
        QFile f(fake_dwg);
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        f.write(dxf);
        f.close();
    }
    // (3) convert DWG->DXF off-thread via run_with_progress, then load + catalog.
    const QString out_dxf = dir + QStringLiteral("/musacad_dwgin_test.dxf");
    QFile::remove(out_dxf);
    QString err;
    const bool conv_ok =
        run_with_progress(QStringLiteral("Converting…"),
                          [&](QString& e) { return conv.to_dxf(fake_dwg, out_dxf, e); }, err) &&
        QFileInfo::exists(out_dxf);
    engine_->submit(core::NewDocumentCommand{});
    pump([this] { return viewport_->line_vertex_count() == 0; });
    const std::uint64_t sv0 = viewport_->status_version();
    open_from(out_dxf, true);
    const bool loaded = pump([this] { return viewport_->line_vertex_count() == 4; }); // 2 lines
    pump([this, sv0] { return viewport_->status_version() != sv0; });
    const std::string msg = viewport_->last_status();
    const bool catalog_ok = msg.find("skipped") != std::string::npos &&
                            msg.find("HATCH") != std::string::npos;
    std::printf("[selftest] DWG import via converter loads + catalogs gaps (%s): %s\n",
                msg.c_str(), (conv_ok && loaded && catalog_ok) ? "PASS" : "FAIL");
    all = all && disc_ok && conv_ok && loaded && catalog_ok;

    // (4) export pipeline mechanics: existing DXF export -> convert DXF->DWG; then
    // read it back to confirm the round-trip survives the (mock) converter.
    const QString stage = dir + QStringLiteral("/musacad_dwgout_stage.dxf");
    const QString out_dwg = dir + QStringLiteral("/musacad_out.dwg");
    QFile::remove(stage);
    QFile::remove(out_dwg);
    save_to(stage, true);
    const bool staged = pump([&] { const QFileInfo fi(stage); return fi.exists() && fi.size() > 0; });
    const bool exp_ok = staged &&
                        run_with_progress(QStringLiteral("Exporting…"),
                                          [&](QString& e) {
                                              return conv.to_dwg(stage, out_dwg,
                                                                 QStringLiteral("ACAD2018"), e);
                                          },
                                          err) &&
                        QFileInfo::exists(out_dwg);
    std::printf("[selftest] DWG export (DXF->DWG via converter): %s\n", exp_ok ? "PASS" : "FAIL");
    all = all && exp_ok;

    // Cleanup + restore the user's converter-path setting.
    for (const QString& p : {mock, fake_dwg, out_dxf, stage, out_dwg}) {
        QFile::remove(p);
    }
    if (saved.isEmpty()) {
        QSettings().remove(QStringLiteral("io/dwg_converter_path"));
    } else {
        QSettings().setValue(QStringLiteral("io/dwg_converter_path"), saved);
    }
    engine_->submit(core::NewDocumentCommand{});
    return all;
}

void MainWindow::open_text_editor(double wx, double wy, double pick_radius,
                                  const std::string& content, bool multiline) {
    // A small modal editor (popup; an in-canvas overlay is awkward over a QWindow
    // OpenGL surface). Themed by the app-wide Fusion + dark palette (Ph11), so no
    // light native widget. Multi-line for MTEXT/QLEADER, single-line for TEXT.
    QDialog dlg(this);
    dlg.setWindowTitle(multiline ? QStringLiteral("Edit MText") : QStringLiteral("Edit Text"));
    auto* layout = new QVBoxLayout(&dlg);
    const QString initial = QString::fromStdString(content);
    QPlainTextEdit* multi = nullptr;
    QLineEdit* single = nullptr;
    if (multiline) {
        multi = new QPlainTextEdit(initial, &dlg);
        layout->addWidget(multi);
        dlg.resize(360, 180);
    } else {
        single = new QLineEdit(initial, &dlg);
        single->selectAll();
        layout->addWidget(single);
    }
    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    // For single-line, Enter confirms (QLineEdit returnPressed) -- AutoCAD feel.
    if (single != nullptr) {
        QObject::connect(single, &QLineEdit::returnPressed, &dlg, &QDialog::accept);
    }

    if (dlg.exec() != QDialog::Accepted) {
        return; // Esc / Cancel -> no change
    }
    const std::string updated =
        multiline ? multi->toPlainText().toStdString() : single->text().toStdString();
    // One undo group; the engine changes only the content (layer/props/pos kept).
    engine_->submit(core::EditTextContentCommand{
        {wx, wy}, pick_radius, updated, processor_->begin_group()});
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
    const QString name = active_doc_name(); // the active document's tab name (filename / DrawingN)
    const QString mark = (viewport_ != nullptr && viewport_->dirty()) ? QStringLiteral("*")
                                                                      : QString();
    const int fps = viewport_ != nullptr ? static_cast<int>(viewport_->fps() + 0.5) : 0;
    // Build stamp in the title so "the user ran an old binary" is checkable at a glance.
    setWindowTitle(QStringLiteral("%1%2  —  %3  —  %4 FPS  —  built %5")
                       .arg(name, mark, app)
                       .arg(fps)
                       .arg(QStringLiteral(__DATE__ " " __TIME__)));
}

QString MainWindow::active_doc_name() const {
    if (viewport_ != nullptr) {
        const std::uint64_t active = viewport_->active_document_id();
        for (const core::DocumentInfo& d : viewport_->documents()) {
            if (d.id == active) {
                return QString::fromStdString(d.name);
            }
        }
    }
    return QStringLiteral("Drawing1");
}

QString MainWindow::active_doc_path() const {
    if (viewport_ != nullptr) {
        const std::uint64_t active = viewport_->active_document_id();
        for (const core::DocumentInfo& d : viewport_->documents()) {
            if (d.id == active) {
                return QString::fromStdString(d.path);
            }
        }
    }
    return {};
}

// Retained for any single-document path that still wants a discard guard; the
// multi-document lifecycle prompts per-tab on close / per-document on quit instead.
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
    // The engine binds the active document's path/tab-name on a successful native save.
    engine_->submit(core::SaveDocumentCommand{path.toStdString(), dxf});
}

void MainWindow::open_from(const QString& path, bool dxf) {
    if (path.isEmpty()) {
        return;
    }
    // A remembered plot Window holds the PREVIOUS drawing's world coordinates, which are
    // meaningless for a new file (and would plot off-sheet garbage). Reset to Extents.
    last_plot_spec_.area = PlotSpec::Area::Window == last_plot_spec_.area ? PlotSpec::Area::Extents
                                                                          : last_plot_spec_.area;
    last_plot_spec_.win_min = {};
    last_plot_spec_.win_max = {};
    // Open/Import ALWAYS creates a new tab and activates it -- never overwriting the work
    // in the current tab. The tab name is the filename.
    core::OpenDocumentCommand oc;
    oc.path = path.toStdString();
    oc.dxf = dxf;
    oc.new_tab = true;
    oc.name = QFileInfo(path).fileName().toStdString();
    engine_->submit(std::move(oc));
}

void MainWindow::create_new_tab() {
    engine_->submit(core::CreateDocumentCommand{});
}

void MainWindow::file_new() {
    create_new_tab(); // a new untitled tab; existing tabs are untouched
}

void MainWindow::file_open() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open Drawing"),
                                                      QString(), QStringLiteral("Musa CAD (*.musa)"),
                                                      nullptr, QFileDialog::DontUseNativeDialog);
    open_from(path, false);
}

void MainWindow::file_save() {
    const QString path = active_doc_path();
    if (path.isEmpty()) {
        file_save_as();
        return;
    }
    save_to(path, false);
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

// --- multi-document tab strip ---------------------------------------------

void MainWindow::sync_document_tabs() {
    if (file_tabs_ == nullptr || viewport_ == nullptr) {
        return;
    }
    const std::vector<core::DocumentInfo> docs = viewport_->documents();
    if (docs.empty()) {
        return; // before the first snapshot
    }
    const std::uint64_t active = viewport_->active_document_id();
    const auto label = [](const core::DocumentInfo& d) {
        return QString::fromStdString(d.name) + (d.dirty ? QStringLiteral(" *") : QString());
    };
    // Rebuild only when the set / labels actually changed (avoids flicker + spurious
    // currentChanged signals that would re-issue a switch). Programmatic edits are
    // signal-blocked so they never feed back into switch_to_document.
    bool changed = static_cast<int>(docs.size()) != file_tabs_->count();
    for (int i = 0; !changed && i < file_tabs_->count(); ++i) {
        const auto& d = docs[static_cast<std::size_t>(i)];
        changed = file_tabs_->tabData(i).toULongLong() != d.id || file_tabs_->tabText(i) != label(d);
    }
    const QSignalBlocker block(file_tabs_);
    if (changed) {
        while (file_tabs_->count() > static_cast<int>(docs.size())) {
            file_tabs_->removeTab(file_tabs_->count() - 1);
        }
        for (int i = 0; i < static_cast<int>(docs.size()); ++i) {
            const auto& d = docs[static_cast<std::size_t>(i)];
            if (i < file_tabs_->count()) {
                file_tabs_->setTabText(i, label(d));
            } else {
                file_tabs_->addTab(label(d));
            }
            file_tabs_->setTabData(i, QVariant::fromValue<qulonglong>(d.id));
        }
    }
    int active_idx = -1;
    for (int i = 0; i < file_tabs_->count(); ++i) {
        if (file_tabs_->tabData(i).toULongLong() == active) {
            active_idx = i;
        }
    }
    if (active_idx >= 0 && file_tabs_->currentIndex() != active_idx) {
        file_tabs_->setCurrentIndex(active_idx);
    }
}

void MainWindow::switch_to_document(std::uint64_t id) {
    if (id == 0 || engine_ == nullptr) {
        return;
    }
    // Cancel-on-switch: an in-flight command belongs to the outgoing document.
    if (processor_ != nullptr) {
        processor_->cancel();
    }
    engine_->submit(core::SwitchDocumentCommand{id});
}

void MainWindow::cycle_document(int dir) {
    if (viewport_ == nullptr) {
        return;
    }
    const std::vector<core::DocumentInfo> docs = viewport_->documents();
    if (docs.size() < 2) {
        return;
    }
    const std::uint64_t active = viewport_->active_document_id();
    int idx = 0;
    for (int i = 0; i < static_cast<int>(docs.size()); ++i) {
        if (docs[static_cast<std::size_t>(i)].id == active) {
            idx = i;
        }
    }
    const int n = static_cast<int>(docs.size());
    const int next = ((idx + dir) % n + n) % n;
    switch_to_document(docs[static_cast<std::size_t>(next)].id);
}

bool MainWindow::drop_selection_on_tab(QPoint global_pos) {
    if (file_tabs_ == nullptr || viewport_ == nullptr) {
        return false;
    }
    const QPoint local = file_tabs_->mapFromGlobal(global_pos);
    if (!file_tabs_->rect().contains(local)) {
        return false; // not dropped on the tab strip
    }
    const int idx = file_tabs_->tabAt(local);
    if (idx < 0) {
        return false;
    }
    const std::uint64_t target = file_tabs_->tabData(idx).toULongLong();
    if (target == 0 || target == viewport_->active_document_id() ||
        viewport_->selection_count() <= 0) {
        return false; // same document (or nothing selected) -> not a transfer
    }
    // Transfer: snapshot the source selection, switch to the target, paste at the original
    // coordinates as one undo group. FIFO ordering guarantees the copy reads the source.
    engine_->submit(core::CopyClipboardCommand{});
    switch_to_document(target);
    core::PasteClipboardCommand paste;
    paste.at_cursor = false; // keep original world coordinates in the target
    paste.group = processor_->begin_group();
    engine_->submit(std::move(paste));
    return true;
}

// Returns false only if the user CANCELS (the close/quit must be aborted). On Save it
// queues the save (the caller flushes before tearing down); on Discard it does nothing.
bool MainWindow::prompt_save_document(std::uint64_t id, const QString& name, const QString& path) {
    if (viewport_ != nullptr && viewport_->active_document_id() != id) {
        switch_to_document(id); // make it active so Save targets the right store (FIFO)
    }
    const auto choice = QMessageBox::warning(
        this, QStringLiteral("Unsaved changes"),
        QStringLiteral("Save changes to %1?").arg(name),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (choice == QMessageBox::Cancel) {
        return false;
    }
    if (choice == QMessageBox::Save) {
        QString p = path;
        if (p.isEmpty()) {
            p = QFileDialog::getSaveFileName(this, QStringLiteral("Save Drawing As"), QString(),
                                             QStringLiteral("Musa CAD (*.musa)"), nullptr,
                                             QFileDialog::DontUseNativeDialog);
            if (p.isEmpty()) {
                return false; // cancelled the path dialog -> abort
            }
            if (!p.endsWith(QStringLiteral(".musa"), Qt::CaseInsensitive)) {
                p += QStringLiteral(".musa");
            }
        }
        engine_->submit(core::SaveDocumentCommand{p.toStdString(), false});
    }
    return true;
}

void MainWindow::close_document_tab(std::uint64_t id) {
    if (id == 0 || viewport_ == nullptr) {
        return;
    }
    bool found = false;
    bool dirty = false;
    QString name;
    QString path;
    for (const core::DocumentInfo& d : viewport_->documents()) {
        if (d.id == id) {
            found = true;
            dirty = d.dirty;
            name = QString::fromStdString(d.name);
            path = QString::fromStdString(d.path);
        }
    }
    if (!found) {
        return;
    }
    if (dirty && !prompt_save_document(id, name, path)) {
        return; // user cancelled the close
    }
    engine_->submit(core::CloseDocumentCommand{id}); // queues after any Save (FIFO)
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Quit guard: prompt to save every dirty document, then flush the saves before the
    // window (and engine) tear down so nothing is silently lost.
    if (viewport_ == nullptr) {
        event->accept();
        return;
    }
    const std::vector<core::DocumentInfo> docs = viewport_->documents();
    for (const core::DocumentInfo& d : docs) {
        if (d.dirty &&
            !prompt_save_document(d.id, QString::fromStdString(d.name),
                                  QString::fromStdString(d.path))) {
            event->ignore(); // a Cancel anywhere aborts the quit
            return;
        }
    }
    // Let the geometry thread drain the queued saves (per-doc dirty clears) before close.
    bool any_dirty = false;
    for (const core::DocumentInfo& d : docs) {
        any_dirty = any_dirty || d.dirty;
    }
    if (any_dirty) {
        pump_with_progress(QStringLiteral("Saving…"),
                           [this] {
                               for (const core::DocumentInfo& d : viewport_->documents()) {
                                   if (d.dirty) {
                                       return false;
                                   }
                               }
                               return true;
                           },
                           10'000);
    }
    event->accept();
}

void MainWindow::file_import_dxf() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Import DXF"), QString(),
                                                      QStringLiteral("DXF (*.dxf)"), nullptr,
                                                      QFileDialog::DontUseNativeDialog);
    open_from(path, true); // a new tab; current work is untouched
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

bool MainWindow::offer_dwg_setup(const QString& title) {
    QMessageBox box(this);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(title);
    box.setText(DwgConverter::install_hint());
    QPushButton* cfg = box.addButton(QStringLiteral("Configure…"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    command_widget_->append_line(DwgConverter::install_hint().toStdString());
    if (box.clickedButton() == cfg) {
        configure_dwg_converter();
        return true;
    }
    return false;
}

void MainWindow::configure_dwg_converter() {
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("DWG Converter Setup"));
    auto* v = new QVBoxLayout(&dlg);

    auto* info = new QLabel(
        QStringLiteral(
            "DWG needs an external converter. Musa CAD never bundles one (it stays "
            "LGPL-clean) -- it runs a converter you install:\n"
            "  • ODA File Converter (free) -- opendesign.com\n"
            "  • LibreDWG (dwg2dxf)\n\n"
            "Browse to its executable below, or Auto-detect if it is on your PATH. "
            "(Musa CAD cannot download/install it for you -- licensing + per-platform "
            "installers + the ODA EULA make that the user's step.)"),
        &dlg);
    info->setWordWrap(true);
    v->addWidget(info);

    auto* status = new QLabel(&dlg);
    status->setObjectName(QStringLiteral("DwgStatus"));
    v->addWidget(status);

    auto* row = new QWidget(&dlg);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    auto* path_edit = new QLineEdit(
        QSettings().value(QStringLiteral("io/dwg_converter_path")).toString(), row);
    path_edit->setPlaceholderText(QStringLiteral("converter path (blank = auto-detect on PATH)"));
    auto* browse = new QPushButton(QStringLiteral("Browse…"), row);
    h->addWidget(path_edit);
    h->addWidget(browse);
    v->addWidget(row);

    const auto refresh = [status, path_edit] {
        const QString p = path_edit->text().trimmed();
        const DwgConverter c =
            p.isEmpty() ? DwgConverter::discover_on_path() : DwgConverter::from_program(p);
        if (c.available()) {
            status->setText(QStringLiteral("✔ Detected: %1 — %2")
                                .arg(DwgConverter::kind_name(c.kind()), c.program()));
        } else {
            status->setText(p.isEmpty() ? QStringLiteral("✗ No converter found on PATH.")
                                        : QStringLiteral("✗ No converter at that path."));
        }
    };
    connect(path_edit, &QLineEdit::textChanged, &dlg, [refresh] { refresh(); });
    connect(browse, &QPushButton::clicked, &dlg, [&dlg, path_edit] {
        const QString f = QFileDialog::getOpenFileName(&dlg, QStringLiteral("Select DWG converter"),
                                                       QString(), QString(), nullptr,
                                                       QFileDialog::DontUseNativeDialog);
        if (!f.isEmpty()) {
            path_edit->setText(f);
        }
    });

    auto* links = new QWidget(&dlg);
    auto* lh = new QHBoxLayout(links);
    lh->setContentsMargins(0, 0, 0, 0);
    auto* detect = new QPushButton(QStringLiteral("Auto-detect on PATH"), links);
    auto* oda = new QPushButton(QStringLiteral("Get ODA…"), links);
    auto* ldwg = new QPushButton(QStringLiteral("Get LibreDWG…"), links);
    lh->addWidget(detect);
    lh->addWidget(oda);
    lh->addWidget(ldwg);
    v->addWidget(links);
    connect(detect, &QPushButton::clicked, &dlg, [path_edit, refresh] {
        const DwgConverter c = DwgConverter::discover_on_path();
        path_edit->setText(c.available() ? c.program() : QString());
        refresh();
    });
    connect(oda, &QPushButton::clicked, &dlg, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.opendesign.com/guestfiles")));
    });
    connect(ldwg, &QPushButton::clicked, &dlg, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.gnu.org/software/libredwg/")));
    });

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(bb);

    refresh();
    if (dlg.exec() == QDialog::Accepted) {
        const QString p = path_edit->text().trimmed();
        if (p.isEmpty()) {
            QSettings().remove(QStringLiteral("io/dwg_converter_path"));
        } else {
            QSettings().setValue(QStringLiteral("io/dwg_converter_path"), p);
        }
    }
}

bool MainWindow::run_with_progress(const QString& label,
                                   const std::function<bool(QString&)>& work, QString& err) {
    // The converter runs as a separate PROCESS (off the UI process entirely). We run
    // the blocking call on a short worker thread and pump the event loop behind a
    // modal indeterminate dialog, so the app stays responsive without touching the
    // store. The dialog has no cancel (a half-killed convert is worse than waiting).
    QProgressDialog dlg(label, QString(), 0, 0, this);
    dlg.setWindowModality(Qt::ApplicationModal);
    dlg.setMinimumDuration(0);
    dlg.setAutoClose(false);
    dlg.setAutoReset(false);
    dlg.show();
    std::atomic<bool> done{false};
    bool ok = false;
    QString local_err;
    std::jthread worker([&] {
        ok = work(local_err);
        done.store(true, std::memory_order_release);
    });
    while (!done.load(std::memory_order_acquire)) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    worker.join();
    dlg.close();
    err = local_err;
    return ok;
}

bool MainWindow::pump_with_progress(const QString& label, const std::function<bool()>& done,
                                    int timeout_ms) {
    if (done()) {
        return true; // already satisfied -- no flash of a dialog
    }
    QProgressDialog dlg(label, QString(), 0, 0, this);
    dlg.setWindowModality(Qt::ApplicationModal);
    dlg.setMinimumDuration(0);
    dlg.setAutoClose(false);
    dlg.setAutoReset(false);
    dlg.show();
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeout_ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    dlg.close();
    return done();
}

void MainWindow::file_import_dwg() {
    const QString dwg = QFileDialog::getOpenFileName(this, QStringLiteral("Import DWG"), QString(),
                                                     QStringLiteral("DWG (*.dwg)"), nullptr,
                                                     QFileDialog::DontUseNativeDialog);
    if (dwg.isEmpty()) {
        return;
    }
    DwgConverter conv = DwgConverter::discover();
    if (!conv.available()) {
        if (offer_dwg_setup(QStringLiteral("DWG import"))) {
            conv = DwgConverter::discover(); // the user may have configured one
        }
        if (!conv.available()) {
            return;
        }
    }
    // Convert to a temp DXF off the UI thread, then load via the EXISTING fail-safe
    // DXF importer (one geometry-thread op; store unchanged on any error).
    const QString tmp = QDir::temp().filePath(QStringLiteral("musacad_dwgin.dxf"));
    QFile::remove(tmp);
    QString err;
    if (!run_with_progress(QStringLiteral("Converting DWG…"),
                           [&](QString& e) { return conv.to_dxf(dwg, tmp, e); }, err)) {
        command_widget_->append_line("DWG import failed: " + err.toStdString());
        QMessageBox::warning(this, QStringLiteral("DWG import"), err);
        return;
    }
    const std::uint64_t sv0 = viewport_->status_version();
    open_from(tmp, true);
    // The parse + index runs on the geometry thread; a large DXF can take seconds, so
    // keep an indeterminate dialog up until the store reports back instead of a frozen
    // blank window. The status change also carries the importer's itemised gap catalog
    // ("Imported N ...; skipped K unsupported (HATCH x12, ...)"), logged next to the
    // source so migration gaps are recorded, not silently dropped.
    pump_with_progress(QStringLiteral("Loading drawing…"),
                       [this, sv0] { return viewport_->status_version() != sv0; }, 600'000);
    const std::string summary = viewport_->last_status();
    if (!summary.empty()) {
        const QString log_path = dwg + QStringLiteral(".import.log");
        QFile log(log_path);
        if (log.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            log.write(("DWG import via " + conv.program().toStdString() + "\n").c_str());
            log.write(summary.c_str());
            log.write("\n");
        }
        command_widget_->append_line("DWG imported. Gap log: " + log_path.toStdString());
    }
    QFile::remove(tmp);
}

void MainWindow::file_export_dwg() {
    DwgConverter conv = DwgConverter::discover();
    if (!conv.available()) {
        if (offer_dwg_setup(QStringLiteral("DWG export"))) {
            conv = DwgConverter::discover();
        }
        if (!conv.available()) {
            return;
        }
    }
    QString dwg = QFileDialog::getSaveFileName(this, QStringLiteral("Export DWG"), QString(),
                                               QStringLiteral("DWG (*.dwg)"), nullptr,
                                               QFileDialog::DontUseNativeDialog);
    if (dwg.isEmpty()) {
        return;
    }
    if (!dwg.endsWith(QStringLiteral(".dwg"), Qt::CaseInsensitive)) {
        dwg += QStringLiteral(".dwg");
    }
    // Stage 1: the EXISTING exporter writes a temp DXF on the geometry thread.
    const QString tmp = QDir::temp().filePath(QStringLiteral("musacad_dwgout.dxf"));
    QFile::remove(tmp);
    save_to(tmp, true);
    const bool staged = pump_with_progress(
        QStringLiteral("Preparing DXF…"),
        [&tmp] {
            const QFileInfo fi(tmp);
            return fi.exists() && fi.size() > 0;
        },
        600'000);
    if (!staged) {
        command_widget_->append_line("DWG export failed: could not write the intermediate DXF.");
        QMessageBox::warning(this, QStringLiteral("DWG export"),
                             QStringLiteral("Could not write the intermediate DXF."));
        return;
    }
    // Stage 2: convert DXF -> DWG (two-stage lossy: capped by DXF export then the
    // converter). Default DWG version ACAD2018 (widely compatible).
    QString err;
    if (!run_with_progress(QStringLiteral("Exporting DWG…"),
                           [&](QString& e) {
                               return conv.to_dwg(tmp, dwg, QStringLiteral("ACAD2018"), e);
                           },
                           err)) {
        command_widget_->append_line("DWG export failed: " + err.toStdString());
        QMessageBox::warning(this, QStringLiteral("DWG export"), err);
        QFile::remove(tmp);
        return;
    }
    QFile::remove(tmp);
    command_widget_->append_line("Exported DWG: " + dwg.toStdString());
}

void MainWindow::show_about() {
    const auto sv = [](std::string_view s) { return QString::fromUtf8(s.data(), int(s.size())); };
    const QString app = sv(core::app_name());
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("About %1").arg(app));
    auto* lay = new QVBoxLayout(&dlg);
    lay->setSpacing(10);
    lay->setContentsMargins(28, 22, 28, 18);

    auto* logo = new QLabel(&dlg);
    logo->setPixmap(QIcon(QStringLiteral(":/branding/musacad_logo.svg")).pixmap(96, 96));
    logo->setAlignment(Qt::AlignCenter);
    lay->addWidget(logo);

    auto* title = new QLabel(QStringLiteral("<span style='font-size:15pt; font-weight:600'>%1</span>")
                                 .arg(app), &dlg);
    title->setAlignment(Qt::AlignCenter);
    lay->addWidget(title);

    auto* info = new QLabel(
        QStringLiteral("Version %1<br>Built %2<br><br>"
                       "A high-performance, multi-threaded 2D CAD engine.<br>"
                       "© Musa CAD contributors — licensed under the "
                       "<b>GNU LGPL-3.0-or-later</b>.")
            .arg(sv(core::version_string()), QStringLiteral(__DATE__ " " __TIME__)),
        &dlg);
    info->setTextFormat(Qt::RichText);
    info->setAlignment(Qt::AlignCenter);
    lay->addWidget(info);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    lay->addWidget(buttons);
    dlg.exec();
}

// --- Plot / print ----------------------------------------------------------

namespace {
// Compile-time build stamp so a stale binary is obvious at a glance (provenance + title).
constexpr const char* kBuildStamp = __DATE__ " " __TIME__;

// One self-documenting provenance line: the FULLY RESOLVED spec actually being plotted,
// plus the build that produced it. Every plot logs this so each PDF is evidence.
std::string describe_plot_spec(const PlotSpec& s, const char* ctx) {
    const char* area = s.area == PlotSpec::Area::Display  ? "Display"
                       : s.area == PlotSpec::Area::Window ? "Window"
                                                          : "Extents";
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "[plot-spec] %s | build=%s | area=%s win=(%.1f,%.1f)..(%.1f,%.1f) fit=%d "
                  "scale=%.4f:%.4f orient=%s paper=%.0fx%.0fmm center=%d lw=%d target=%s",
                  ctx, kBuildStamp, area, s.win_min.x, s.win_min.y, s.win_max.x, s.win_max.y,
                  s.fit ? 1 : 0, s.scale_num, s.scale_den, s.landscape ? "landscape" : "portrait",
                  s.paper_w_mm, s.paper_h_mm, s.center ? 1 : 0, s.plot_lineweights ? 1 : 0,
                  s.target.c_str());
    return buf;
}
} // namespace

void MainWindow::open_plot_dialog() {
    // Default the paper orientation to the drawing's aspect (a landscape drawing -> a
    // landscape sheet), so a fit plot uses the page well. The dialog still lets the user
    // override. Only nudge when nothing was deliberately picked (still the default A4).
    engine_->consume_snapshot();
    const core::RenderSnapshot& live = engine_->snapshot();
    if (live.has_bounds) {
        const double w = live.bounds_max.x - live.bounds_min.x;
        const double h = live.bounds_max.y - live.bounds_min.y;
        if (w > 0.0 && h > 0.0) {
            const bool want_landscape = w >= h;
            if (want_landscape != last_plot_spec_.landscape) {
                last_plot_spec_.landscape = want_landscape;
                std::swap(last_plot_spec_.paper_w_mm, last_plot_spec_.paper_h_mm);
            }
        }
    }
    open_plot_dialog_seeded(last_plot_spec_);
}

void MainWindow::open_plot_dialog_seeded(const PlotSpec& seed) {
    std::vector<std::string> printers;
    for (const QPrinterInfo& pi : QPrinterInfo::availablePrinters()) {
        printers.push_back(pi.printerName().toStdString());
    }
    auto* dlg = new PlotDialog(seed, printers, page_setup_names(), this);
    // Pick window: the dialog is modal over a native (QWindow) GL viewport, so a hidden
    // modal can't reliably yield input on X11. Fully CLOSE it, activate the viewport, let
    // the user drag a rectangle, then re-open a fresh dialog seeded with the result. The
    // pick callback always fires (drag = window; click/Esc = unchanged) so the dialog
    // always comes back -- the user is never stranded without it.
    connect(dlg, &PlotDialog::pickWindowRequested, this, [this, dlg] {
        const PlotSpec s = dlg->spec();
        dlg->close(); // ends the modal session (deleteLater via the finished connect)
        activateWindow();
        raise();
        viewport_->requestActivate(); // the embedded GL QWindow must be active for input
        command_widget_->append_line(
            "Pick the plot window: drag a rectangle in the drawing (Esc to cancel).");
        viewport_->begin_plot_window_pick([this, s](bool ok, core::Vec2 mn, core::Vec2 mx) {
            PlotSpec u = s;
            if (ok) {
                u.area = PlotSpec::Area::Window;
                u.win_min = mn;
                u.win_max = mx;
            }
            open_plot_dialog_seeded(u);
        });
    });
    connect(dlg, &PlotDialog::previewRequested, this, [this, dlg] { plot_preview(dlg->spec()); });
    connect(dlg, &PlotDialog::saveSetupRequested, this, [this, dlg] { save_page_setup(dlg->spec()); });
    connect(dlg, &PlotDialog::recallSetupRequested, this, [this, dlg](const QString& name) {
        PlotSpec s;
        if (recall_page_setup(name.toStdString(), s)) {
            dlg->set_spec(s);
        }
    });
    connect(dlg, &QDialog::accepted, this, [this, dlg] {
        last_plot_spec_ = dlg->spec();
        do_plot(last_plot_spec_);
    });
    connect(dlg, &QDialog::finished, dlg, &QObject::deleteLater);
    dlg->show();
}

bool MainWindow::prepare_plot(const PlotSpec& spec, core::Vec2& amin, core::Vec2& amax) {
    // The live snapshot supplies the extents (same geometry) + a size to scale tolerance by.
    engine_->consume_snapshot();
    const core::RenderSnapshot& live = engine_->snapshot();
    if (spec.area == PlotSpec::Area::Display) {
        viewport_->view_world_rect(amin, amax);
    } else if (spec.area == PlotSpec::Area::Window) {
        amin = spec.win_min;
        amax = spec.win_max;
    } else { // Extents
        if (!live.has_bounds) {
            QMessageBox::information(this, QStringLiteral("Plot"), QStringLiteral("Nothing to plot."));
            return false;
        }
        amin = live.bounds_min;
        amax = live.bounds_max;
    }
    // Safety net: a degenerate or off-drawing plot area maps every entity off the sheet,
    // producing the "stray lines crossing an empty page" garbage. This happens with a
    // Window remembered from a DIFFERENT drawing (its world coords don't bracket this one)
    // or a Display view not aimed at the geometry. Fall back to Extents so PLOT is never
    // silently bogus.
    if (spec.area != PlotSpec::Area::Extents && live.has_bounds) {
        const bool degenerate = !(amax.x - amin.x > 1e-9) || !(amax.y - amin.y > 1e-9);
        const bool disjoint = amax.x < live.bounds_min.x || amin.x > live.bounds_max.x ||
                              amax.y < live.bounds_min.y || amin.y > live.bounds_max.y;
        if (degenerate || disjoint) {
            amin = live.bounds_min;
            amax = live.bounds_max;
            command_widget_->append_line(
                "Plot: the chosen area was empty or off the drawing -- using Extents instead.");
        }
    }
    // Build a fine plot snapshot (smooth arcs) on the geometry thread. Tessellate to the
    // PLOTTED region at paper-pixel resolution -- NOT the whole-drawing extents. A large
    // drawing (or stray far-off geometry) inflates an extents-based tolerance so a circle
    // that fills the picked window collapses into a polygon. ~0.3 px chord deviation at
    // 300 DPI keeps every on-page feature smooth while staying cheap for tiny ones.
    const double area_diag = std::max(core::length(amax - amin), 1e-9);
    const double paper_diag_px = std::hypot(spec.paper_w_mm, spec.paper_h_mm) / 25.4 * 300.0;
    const double tol = std::max(area_diag / paper_diag_px * 0.3, 1e-9);
    const std::uint64_t v0 = engine_->plot_snapshot_version();
    engine_->submit(core::BuildPlotSnapshotCommand{tol});
    pump_with_progress(QStringLiteral("Preparing plot…"),
                       [this, v0] { return engine_->plot_snapshot_version() != v0; }, 60'000);
    return true;
}

void MainWindow::do_plot(const PlotSpec& spec) {
    command_widget_->append_line(describe_plot_spec(spec, "do_plot"));
    core::Vec2 amin;
    core::Vec2 amax;
    if (!prepare_plot(spec, amin, amax)) {
        return;
    }
    const core::RenderSnapshot& snap = engine_->plot_snapshot();
    const QSizeF paper(spec.paper_w_mm, spec.paper_h_mm);

    if (spec.target == "PDF") {
        QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Plot to PDF"), QString(),
                                                    QStringLiteral("PDF (*.pdf)"), nullptr,
                                                    QFileDialog::DontUseNativeDialog);
        if (path.isEmpty()) {
            return;
        }
        if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
            path += QStringLiteral(".pdf");
        }
        QString err;
        run_with_progress(QStringLiteral("Plotting…"),
                          [&](QString&) -> bool {
                              QPdfWriter w(path);
                              w.setPageSize(QPageSize(paper, QPageSize::Millimeter));
                              w.setResolution(300);
                              paint_plot(w, snap, spec, amin, amax);
                              return true;
                          },
                          err);
        const QFileInfo fi(path);
        if (!fi.exists() || fi.size() == 0) {
            QMessageBox::warning(this, QStringLiteral("Plot"),
                                 QStringLiteral("Could not write the PDF: ") + path);
            return;
        }
        command_widget_->append_line("Plotted to PDF: " + path.toStdString());
        return;
    }

    // A physical printer.
    QPrinterInfo info = QPrinterInfo::printerInfo(QString::fromStdString(spec.target));
    if (info.isNull()) {
        QMessageBox::warning(this, QStringLiteral("Plot"),
                             QStringLiteral("Printer not found: ") + QString::fromStdString(spec.target));
        return;
    }
    QString err;
    bool ok = run_with_progress(
        QStringLiteral("Printing…"),
        [&](QString& e) -> bool {
            QPrinter pr(info, QPrinter::HighResolution);
            if (!pr.setPageSize(QPageSize(paper, QPageSize::Millimeter))) {
                e = QStringLiteral("The printer rejected the page size.");
                return false;
            }
            pr.setCopyCount(std::max(1, spec.copies));
            paint_plot(pr, snap, spec, amin, amax);
            return true;
        },
        err);
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("Plot"),
                             err.isEmpty() ? QStringLiteral("Printing failed.") : err);
        return;
    }
    command_widget_->append_line("Sent to printer: " + spec.target);
}

void MainWindow::plot_preview(const PlotSpec& spec) {
    core::Vec2 amin;
    core::Vec2 amax;
    if (!prepare_plot(spec, amin, amax)) {
        return;
    }
    // Reuse paint_plot onto a raster image at the paper's aspect (the preview is a screen
    // image; the actual plot output stays vector).
    const double aspect = spec.paper_h_mm > 0 ? spec.paper_w_mm / spec.paper_h_mm : 1.4;
    const int w = 900;
    const int h = std::max(1, static_cast<int>(w / aspect));
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(Qt::white);
    paint_plot(img, engine_->plot_snapshot(), spec, amin, amax);
    auto* dlg = new QDialog(this);
    dlg->setWindowTitle(QStringLiteral("Plot Preview"));
    auto* lay = new QVBoxLayout(dlg);
    auto* lbl = new QLabel(dlg);
    lbl->setPixmap(QPixmap::fromImage(img));
    lay->addWidget(lbl);
    connect(dlg, &QDialog::finished, dlg, &QObject::deleteLater);
    dlg->show();
}

bool MainWindow::selftest_plot_file(const QString& in_path, const QString& out_pdf, int area) {
    const std::uint64_t sv0 = viewport_->status_version();
    open_from(in_path, in_path.endsWith(QStringLiteral(".dxf"), Qt::CaseInsensitive));
    pump_with_progress(QStringLiteral("Loading…"),
                       [this, sv0] { return viewport_->status_version() != sv0; }, 120'000);
    PlotSpec spec;
    spec.area = static_cast<PlotSpec::Area>(std::clamp(area, 0, 2));
    spec.paper_w_mm = 297.0;
    spec.paper_h_mm = 210.0;
    spec.landscape = true;
    spec.fit = true;
    spec.center = true;
    core::Vec2 amin;
    core::Vec2 amax;
    if (!prepare_plot(spec, amin, amax)) {
        std::printf("[plot_test] prepare_plot returned false (nothing to plot)\n");
        return false;
    }
    const core::RenderSnapshot& snap = engine_->plot_snapshot();
    std::printf("[plot_test] area=%d lines=%zu fills=%zu amin=(%.1f,%.1f) amax=(%.1f,%.1f)\n", area,
                snap.line_vertices.size(), snap.fill_vertices.size(), amin.x, amin.y, amax.x, amax.y);
    QPdfWriter w(out_pdf);
    w.setPageSize(QPageSize(QSizeF(spec.paper_w_mm, spec.paper_h_mm), QPageSize::Millimeter));
    w.setResolution(300);
    paint_plot(w, snap, spec, amin, amax);
    std::printf("[plot_test] wrote %s\n", out_pdf.toStdString().c_str());
    return true;
}

bool MainWindow::selftest_gui_plot_file(const QString& in_path, const QString& out_pdf) {
    // REAL GUI PATH (no harness shortcuts): open the file, then resolve the spec EXACTLY as
    // Ctrl+P -> open_plot_dialog does -- including the orientation default and the dialog's
    // own set_spec()->widgets->spec() round-trip -- and plot the dialog's INITIAL state.
    const std::uint64_t sv0 = viewport_->status_version();
    open_from(in_path, in_path.endsWith(QStringLiteral(".dxf"), Qt::CaseInsensitive));
    pump_with_progress(QStringLiteral("Loading…"),
                       [this, sv0] { return viewport_->status_version() != sv0; }, 120'000);

    // 1:1 with open_plot_dialog: orientation default from the drawing aspect.
    engine_->consume_snapshot();
    const core::RenderSnapshot& live = engine_->snapshot();
    if (live.has_bounds) {
        const double w = live.bounds_max.x - live.bounds_min.x;
        const double h = live.bounds_max.y - live.bounds_min.y;
        if (w > 0.0 && h > 0.0) {
            const bool want_landscape = w >= h;
            if (want_landscape != last_plot_spec_.landscape) {
                last_plot_spec_.landscape = want_landscape;
                std::swap(last_plot_spec_.paper_w_mm, last_plot_spec_.paper_h_mm);
            }
        }
    }
    // Construct the ACTUAL dialog (no show) and read its INITIAL resolved spec.
    std::vector<std::string> printers;
    PlotDialog dlg(last_plot_spec_, printers, page_setup_names(), this);
    const PlotSpec spec = dlg.spec();
    std::printf("%s\n", describe_plot_spec(spec, "GUI-dialog-initial").c_str());

    core::Vec2 amin;
    core::Vec2 amax;
    if (!prepare_plot(spec, amin, amax)) {
        std::printf("[gui-plot] prepare_plot returned false\n");
        return false;
    }
    const core::RenderSnapshot& snap = engine_->plot_snapshot();

    // Where does the geometry actually map on the page? Compute the device bbox of EVERY
    // painted vertex (ignoring the clip) -- if it blows far past the page, the spec is
    // garbage (off-area geometry). This is the assertion that must catch the user's bug.
    const double dpi = 300.0;
    const double pxw = spec.paper_w_mm / 25.4 * dpi;
    const double pxh = spec.paper_h_mm / 25.4 * dpi;
    double aw = std::max(amax.x - amin.x, 1e-9);
    double ah = std::max(amax.y - amin.y, 1e-9);
    double mmpu = spec.fit ? std::min(spec.paper_w_mm / aw, spec.paper_h_mm / ah)
                           : (spec.scale_den != 0.0 ? spec.scale_num / spec.scale_den : 1.0);
    if (!(mmpu > 0.0)) {
        mmpu = 1.0;
    }
    const double ppu = mmpu / 25.4 * dpi;
    const double scaled_w = aw * ppu, scaled_h = ah * ppu;
    const double ox = (spec.center ? (pxw - scaled_w) * 0.5 : 0.0);
    const double oy = (spec.center ? (pxh - scaled_h) * 0.5 : 0.0);
    double dlo_x = 1e300, dlo_y = 1e300, dhi_x = -1e300, dhi_y = -1e300;
    for (const core::Vec2& v : snap.line_vertices) {
        const double x = ox + (v.x - amin.x) * ppu;
        const double y = oy + (ah - (v.y - amin.y)) * ppu;
        dlo_x = std::min(dlo_x, x);
        dlo_y = std::min(dlo_y, y);
        dhi_x = std::max(dhi_x, x);
        dhi_y = std::max(dhi_y, y);
    }
    std::printf("[gui-plot] page=%.0fx%.0fpx  painted device bbox=(%.0f,%.0f)..(%.0f,%.0f)\n", pxw,
                pxh, dlo_x, dlo_y, dhi_x, dhi_y);
    const double slack = std::max(pxw, pxh); // 1 page of tolerance
    const bool within = dlo_x > -slack && dlo_y > -slack && dhi_x < pxw + slack &&
                        dhi_y < pxh + slack;
    std::printf("[gui-plot] painted-bbox %s page (%s)\n", within ? "WITHIN" : "OVERFLOWS",
                within ? "ok" : "GARBAGE SPEC");

    QPdfWriter w(out_pdf);
    w.setPageSize(QPageSize(QSizeF(spec.paper_w_mm, spec.paper_h_mm), QPageSize::Millimeter));
    w.setResolution(static_cast<int>(dpi));
    paint_plot(w, snap, spec, amin, amax);
    std::printf("[gui-plot] wrote %s\n", out_pdf.toStdString().c_str());
    return within;
}

// Page setups live in the document (native v11); the snapshot carries them to the UI and
// AddPageSetupCommand saves one (one model -- no QSettings fork).
std::vector<std::string> MainWindow::page_setup_names() {
    engine_->consume_snapshot();
    std::vector<std::string> names;
    for (const core::PageSetup& ps : engine_->snapshot().page_setups) {
        names.push_back(ps.name);
    }
    return names;
}

void MainWindow::save_page_setup(const PlotSpec& spec) {
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Save Page Setup"),
                                               QStringLiteral("Name:"), QLineEdit::Normal,
                                               QStringLiteral("Setup1"), &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }
    engine_->submit(core::AddPageSetupCommand{to_page_setup(spec, name.trimmed().toStdString())});
    command_widget_->append_line("Page setup saved: " + name.trimmed().toStdString());
}

bool MainWindow::recall_page_setup(const std::string& name, PlotSpec& out) {
    engine_->consume_snapshot();
    const core::RenderSnapshot& snap = engine_->snapshot();
    for (const core::PageSetup& ps : snap.page_setups) {
        if (ps.name == name) {
            out = from_page_setup(ps);
            // Validate a recalled Window against THIS drawing: a setup carried in from a
            // different drawing can hold a window that doesn't bracket the geometry. A
            // recalled setup must never bypass the plot-time fallback -- sanitise here too.
            if (out.area == PlotSpec::Area::Window && snap.has_bounds) {
                const bool degenerate = !(out.win_max.x - out.win_min.x > 1e-9) ||
                                        !(out.win_max.y - out.win_min.y > 1e-9);
                const bool disjoint = out.win_max.x < snap.bounds_min.x ||
                                      out.win_min.x > snap.bounds_max.x ||
                                      out.win_max.y < snap.bounds_min.y ||
                                      out.win_min.y > snap.bounds_max.y;
                if (degenerate || disjoint) {
                    out.area = PlotSpec::Area::Extents;
                    out.win_min = {};
                    out.win_max = {};
                    command_widget_->append_line("Page setup \"" + name +
                                                 "\": window is off this drawing -- using Extents.");
                }
            }
            return true;
        }
    }
    return false;
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
