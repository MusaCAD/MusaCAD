// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// The ribbon's contents: the tabs, panels and tools, laid out the way AutoCAD's Drafting &
// Annotation workspace lays them out -- Home (Draw, Modify, Annotation, Layers, Block,
// Properties, Groups, Utilities, Clipboard), Insert, Annotate, Parametric, View, Manage and
// Output -- with the same tool groupings, drop-downs and slide-outs, over Musa CAD's own
// commands. Every button is its typed command (icon and tooltip from the command registry),
// a method entry such as "Circle > 2-Point" is a macro that feeds the command its option,
// and a tool Musa CAD does not have yet is a disabled placeholder in AutoCAD's place, so
// the layout reads the same while the gaps are tracked as issues. The application menu on
// the Musa mark carries the file operations, as AutoCAD's does.
#include "musacad/ui/main_window.hpp"

#include <QAction>
#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLayout>
#include <QIcon>
#include <QKeySequence>
#include <QMenu>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>

#include <string>
#include <vector>

#include "musacad/command/command_processor.hpp"
#include "musacad/command/command_registry.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/properties.hpp"
#include "musacad/core/properties_palette.hpp"
#include "musacad/ui/command_icons.hpp"
#include "musacad/ui/command_line_widget.hpp"
#include "musacad/ui/properties_panel.hpp"
#include "musacad/ui/ribbon_bar.hpp"
#include "musacad/ui/viewport_window.hpp"

namespace musacad::ui {

namespace {

/// The standard colour list of the Properties panel's colour control (AutoCAD's ACI 1..7).
struct NamedColor {
    const char* name;
    core::Rgb rgb;
};
constexpr NamedColor kColors[] = {
    {"Red", {255, 0, 0}},      {"Yellow", {255, 255, 0}}, {"Green", {0, 255, 0}},
    {"Cyan", {0, 255, 255}},   {"Blue", {0, 0, 255}},     {"Magenta", {255, 0, 255}},
    {"White", {255, 255, 255}},
};
/// The lineweights offered (hundredths of a millimetre), as in the Layer Properties dialog.
constexpr int kLineweights[] = {0, 5, 9, 13, 15, 18, 20, 25, 30, 35, 40, 50, 53, 60, 70, 80, 90, 100, 106, 120, 140, 158, 200, 211};

QIcon swatch_icon(core::Rgb c) {
    QPixmap px(16, 16);
    px.fill(QColor(c.r, c.g, c.b));
    return QIcon(px);
}

} // namespace

void MainWindow::build_ribbon() {
    ribbon_ = new RibbonBar(this);

    // ------------------------------------------------------------------ the application menu
    // The Musa mark is the application button (AutoCAD's "A"); its menu holds the file
    // operations that AutoCAD keeps there too: New, Open, Save, Save As, Import, Export,
    // Plot, the DWG converter setup, About and Exit.
    if (QPushButton* app_btn = ribbon_->app_button()) {
        app_btn->setIcon(QIcon(QStringLiteral(":/branding/musacad_logo.svg")));
        app_btn->setIconSize(QSize(22, 22));
        app_btn->setToolTip(QStringLiteral("Musa CAD -- the application menu: New, Open, Save, "
                                           "Import, Export, Plot"));
        const auto file_action = [this](QMenu* m, const QString& icon, const QString& text,
                                        void (MainWindow::*slot)(), const QKeySequence& shortcut = {}) {
            QAction* a = m->addAction(ribbon_icon(icon), text);
            if (!shortcut.isEmpty()) {
                a->setShortcut(shortcut);
                a->setShortcutVisibleInContextMenu(true);
            }
            connect(a, &QAction::triggered, this, slot);
            return a;
        };
        connect(app_btn, &QPushButton::clicked, this, [this, app_btn, file_action] {
            QMenu menu(this);
            file_action(&menu, QStringLiteral("assets/ribbon/new.svg"), QStringLiteral("New"), &MainWindow::file_new,
                        QKeySequence::New);
            file_action(&menu, QStringLiteral("assets/ribbon/open.svg"), QStringLiteral("Open…"), &MainWindow::file_open,
                        QKeySequence::Open);
            file_action(&menu, QStringLiteral("assets/ribbon/save.svg"), QStringLiteral("Save"), &MainWindow::file_save,
                        QKeySequence::Save);
            file_action(&menu, QStringLiteral("assets/ribbon/saveas.svg"), QStringLiteral("Save As…"),
                        &MainWindow::file_save_as, QKeySequence(QStringLiteral("Ctrl+Shift+S")));
            menu.addSeparator();
            QMenu* imp = menu.addMenu(ribbon_icon(QStringLiteral("assets/ribbon/import.svg")), QStringLiteral("Import"));
            file_action(imp, QStringLiteral("assets/ribbon/import.svg"), QStringLiteral("DXF…"), &MainWindow::file_import_dxf);
            file_action(imp, QStringLiteral("assets/ribbon/import.svg"), QStringLiteral("DWG…"), &MainWindow::file_import_dwg);
            QMenu* exp = menu.addMenu(ribbon_icon(QStringLiteral("assets/ribbon/export.svg")), QStringLiteral("Export"));
            file_action(exp, QStringLiteral("assets/ribbon/export.svg"), QStringLiteral("DXF…"), &MainWindow::file_export_dxf);
            file_action(exp, QStringLiteral("assets/ribbon/export.svg"), QStringLiteral("DWG…"), &MainWindow::file_export_dwg);
            file_action(exp, QStringLiteral("assets/ribbon/plot.svg"), QStringLiteral("PDF (Plot)…"), &MainWindow::open_plot_dialog);
            file_action(&menu, QStringLiteral("assets/ribbon/plot.svg"), QStringLiteral("Plot…"), &MainWindow::open_plot_dialog,
                        QKeySequence::Print);
            menu.addSeparator();
            file_action(&menu, QStringLiteral("assets/ribbon/settings.svg"), QStringLiteral("DWG Setup…"),
                        &MainWindow::configure_dwg_converter);
            file_action(&menu, QStringLiteral("assets/ribbon/settings.svg"), QStringLiteral("About Musa CAD…"),
                        &MainWindow::show_about);
            menu.addSeparator();
            connect(menu.addAction(QStringLiteral("Exit")), &QAction::triggered, this, &MainWindow::close);
            menu.exec(app_btn->mapToGlobal(QPoint(0, app_btn->height())));
        });
    }

    // ------------------------------------------------------------------ the Quick Access Toolbar
    const auto qat = [&](const QString& icon, const QString& tip, const QKeySequence& shortcut,
                         void (MainWindow::*slot)()) {
        auto* a = new QAction(ribbon_icon(icon), tip, this);
        if (!shortcut.isEmpty()) {
            a->setShortcut(shortcut);
            a->setShortcutContext(Qt::ApplicationShortcut);
            addAction(a); // the shortcut works app-wide
        }
        connect(a, &QAction::triggered, this, slot);
        ribbon_->add_qat_action(a);
        return a;
    };
    qat(QStringLiteral("assets/ribbon/new.svg"), QStringLiteral("New"), QKeySequence::New, &MainWindow::file_new);
    qat(QStringLiteral("assets/ribbon/open.svg"), QStringLiteral("Open"), QKeySequence::Open, &MainWindow::file_open);
    qat(QStringLiteral("assets/ribbon/save.svg"), QStringLiteral("Save"), QKeySequence::Save, &MainWindow::file_save);
    qat(QStringLiteral("assets/ribbon/saveas.svg"), QStringLiteral("Save As"), QKeySequence(QStringLiteral("Ctrl+Shift+S")),
        &MainWindow::file_save_as);
    auto* qat_undo = new QAction(ribbon_icon(QStringLiteral("assets/ribbon/undo.svg")), QStringLiteral("Undo"), this);
    qat_undo->setShortcut(QKeySequence::Undo);
    qat_undo->setShortcutContext(Qt::ApplicationShortcut);
    connect(qat_undo, &QAction::triggered, this, [this] { processor_->undo(); });
    ribbon_->add_qat_action(qat_undo);
    auto* qat_redo = new QAction(ribbon_icon(QStringLiteral("assets/ribbon/redo.svg")), QStringLiteral("Redo"), this);
    qat_redo->setShortcut(QKeySequence::Redo);
    qat_redo->setShortcutContext(Qt::ApplicationShortcut);
    connect(qat_redo, &QAction::triggered, this, [this] { processor_->redo(); });
    ribbon_->add_qat_action(qat_redo);
    qat(QStringLiteral("assets/ribbon/plot.svg"), QStringLiteral("Plot"), QKeySequence::Print, &MainWindow::open_plot_dialog);

    // Multi-document and clipboard shortcuts (app-wide).
    const auto add_app_shortcut = [this](const QKeySequence& seq, auto slot) {
        auto* act = new QAction(this);
        act->setShortcut(seq);
        act->setShortcutContext(Qt::ApplicationShortcut);
        connect(act, &QAction::triggered, this, slot);
        addAction(act);
    };
    const auto clip_copy = [this] {
        if (viewport_ != nullptr && viewport_->selection_count() > 0) {
            engine_->submit(core::CopyClipboardCommand{});
        }
    };
    const auto clip_cut = [this] {
        if (viewport_ != nullptr && viewport_->selection_count() > 0) {
            engine_->submit(core::CutClipboardCommand{processor_->begin_group()});
        }
    };
    const auto clip_paste = [this] {
        engine_->submit(core::PasteClipboardCommand{last_cursor_world_, processor_->begin_group()});
    };
    add_app_shortcut(QKeySequence::Copy, clip_copy);
    add_app_shortcut(QKeySequence::Cut, clip_cut);
    add_app_shortcut(QKeySequence::Paste, clip_paste);
    add_app_shortcut(QKeySequence(QStringLiteral("Ctrl+Tab")), [this] { cycle_document(1); });
    add_app_shortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Tab")), [this] { cycle_document(-1); });
    add_app_shortcut(QKeySequence(QStringLiteral("Ctrl+W")), [this] {
        if (viewport_ != nullptr) {
            close_document_tab(viewport_->active_document_id());
        }
    });

    // ------------------------------------------------------------------ helpers
    // A rich tooltip from a registry entry: <b>NAME (ALIAS)</b><br>description.
    const auto cmd_tooltip = [](const command::CommandInfo& info) -> QString {
        QString head = QString::fromStdString(info.name);
        const QString alias = QString::fromStdString(info.primary_alias);
        if (!alias.isEmpty() && alias.compare(head, Qt::CaseInsensitive) != 0) {
            head += QStringLiteral(" (") + alias + QLatin1Char(')');
        }
        QString tip = QStringLiteral("<b>") + head.toHtmlEscaped() + QStringLiteral("</b>");
        if (!info.description.empty()) {
            tip += QStringLiteral("<br>") + QString::fromStdString(info.description).toHtmlEscaped();
        }
        return tip;
    };
    // The icon of a command: the registry's, else a named asset.
    const auto icon_of = [&](const char* alias, const char* fallback = "") -> QIcon {
        const command::CommandInfo* info = processor_->registry().find(alias);
        QString path = info != nullptr ? QString::fromStdString(info->icon) : QString();
        if (path.isEmpty() && fallback[0] != '\0') {
            path = QStringLiteral("assets/ribbon/") + QString::fromUtf8(fallback) + QStringLiteral(".svg");
        }
        return ribbon_icon(path);
    };
    const auto asset = [](const char* name) {
        return ribbon_icon(QStringLiteral("assets/ribbon/") + QString::fromUtf8(name) + QStringLiteral(".svg"));
    };
    // Binds a button to a typed command: object name, tooltip and the click.
    const auto bind = [&](QToolButton* b, const char* alias) {
        b->setObjectName(QStringLiteral("ribbon.cmd.%1").arg(QString::fromUtf8(alias)));
        if (const command::CommandInfo* info = processor_->registry().find(alias); info != nullptr) {
            b->setToolTip(cmd_tooltip(*info));
        }
        connect(b, &QToolButton::clicked, this, [this, alias] {
            command_widget_->focus_input();
            processor_->start_command(alias);
        });
        return b;
    };
    // A large button (icon over its label) running a command.
    const auto large = [&](RibbonPanel* panel, const QString& label, const char* alias, const char* fallback = "",
                           RibbonTier tier = RibbonTier::Primary) {
        return bind(panel->add_button(icon_of(alias, fallback), label, tier), alias);
    };
    // A small button (icon beside its label) in a column, running a command.
    const auto small = [&](RibbonPanel* panel, QWidget* col, const QString& label, const char* alias,
                           const char* fallback = "") {
        return bind(panel->add_small(col, icon_of(alias, fallback), label), alias);
    };
    // A menu entry running a command.
    const auto action = [&](const char* alias, const QString& text, const char* fallback = "") -> QAction* {
        auto* a = new QAction(icon_of(alias, fallback), text, this);
        if (const command::CommandInfo* info = processor_->registry().find(alias); info != nullptr) {
            a->setToolTip(cmd_tooltip(*info));
        }
        connect(a, &QAction::triggered, this, [this, alias] {
            command_widget_->focus_input();
            processor_->start_command(alias);
        });
        return a;
    };
    // A menu entry running a command with a macro (AutoCAD's ribbon macros: the option
    // tokens are fed in, "\\" waiting for the user's next input).
    const auto macro = [&](const QString& text, const char* alias, std::vector<std::string> tokens,
                           const char* icon_name = "") -> QAction* {
        auto* a = new QAction(icon_name[0] != '\0' ? asset(icon_name) : icon_of(alias), text, this);
        a->setObjectName(QStringLiteral("ribbon.macro.") + text.toLower().replace(QLatin1Char(' '), QLatin1Char('_')));
        if (const command::CommandInfo* info = processor_->registry().find(alias); info != nullptr) {
            a->setToolTip(QStringLiteral("<b>%1</b><br>%2").arg(text.toHtmlEscaped(),
                                                                 QString::fromStdString(info->description).toHtmlEscaped()));
        }
        connect(a, &QAction::triggered, this, [this, alias, tokens] {
            command_widget_->focus_input();
            processor_->start_macro(alias, tokens);
        });
        return a;
    };
    // A large split button: the main area runs `primary`, the arrow opens `menu`.
    const auto large_split = [&](RibbonPanel* panel, const QString& label, const char* primary, QMenu* menu,
                                 const char* fallback = "", RibbonTier tier = RibbonTier::Primary) {
        return bind(panel->add_dropdown(icon_of(primary, fallback), label, menu, /*split=*/true, tier), primary);
    };
    const auto small_split = [&](RibbonPanel* panel, QWidget* col, const QString& label, const char* primary,
                                 QMenu* menu, const char* fallback = "") {
        return bind(panel->add_small_dropdown(col, icon_of(primary, fallback), label, menu, /*split=*/true), primary);
    };
    // A menu of command entries.
    const auto menu_of = [&](std::initializer_list<QAction*> actions) {
        auto* m = new QMenu(this);
        for (QAction* a : actions) {
            m->addAction(a);
        }
        return m;
    };
    // A disabled small placeholder for a tool Musa CAD does not have yet (tracked as an issue).
    const auto todo_small = [&](RibbonPanel* panel, QWidget* col, const QString& label, const char* icon_name) {
        return panel->add_small(col, asset(icon_name), label, /*enabled=*/false);
    };

    // ================================================================== Home
    const int home = ribbon_->add_tab(QStringLiteral("Home"));

    // --- Draw: Line, Polyline, Circle, Arc large; Rectangle / Ellipse / Hatch stacked;
    // the slide-out holds the rest of the draw commands.
    RibbonPanel* draw = ribbon_->add_panel(home, QStringLiteral("Draw"), 100);
    draw->set_representative_icon(asset("line"));
    large(draw, QStringLiteral("Line"), "L");
    large(draw, QStringLiteral("Polyline"), "PL");
    large_split(draw, QStringLiteral("Circle"), "C",
                menu_of({macro(QStringLiteral("Center, Radius"), "C", {}),
                         macro(QStringLiteral("Center, Diameter"), "C", {"\\", "D"}),
                         macro(QStringLiteral("2-Point"), "C", {"2P"}),
                         macro(QStringLiteral("3-Point"), "C", {"3P"}),
                         macro(QStringLiteral("Tan, Tan, Radius"), "C", {"T"}),
                         macro(QStringLiteral("Tan, Tan, Tan"), "C", {"TTT"})}));
    large_split(draw, QStringLiteral("Arc"), "A",
                menu_of({macro(QStringLiteral("3-Point"), "A", {}),
                         macro(QStringLiteral("Start, Center, End"), "A", {"\\", "C"}),
                         macro(QStringLiteral("Start, Center, Angle"), "A", {"\\", "C", "\\", "A"}),
                         macro(QStringLiteral("Start, Center, Length"), "A", {"\\", "C", "\\", "L"}),
                         macro(QStringLiteral("Start, End, Angle"), "A", {"\\", "E", "\\", "A"}),
                         macro(QStringLiteral("Start, End, Direction"), "A", {"\\", "E", "\\", "D"}),
                         macro(QStringLiteral("Start, End, Radius"), "A", {"\\", "E", "\\", "R"}),
                         macro(QStringLiteral("Center, Start, End"), "A", {"C"}),
                         macro(QStringLiteral("Center, Start, Angle"), "A", {"C", "\\", "\\", "A"}),
                         macro(QStringLiteral("Center, Start, Length"), "A", {"C", "\\", "\\", "L"}),
                         macro(QStringLiteral("Continue"), "A", {""})}));
    {
        QWidget* col = draw->add_column(/*icon_only=*/true);
        small_split(draw, col, QStringLiteral("Rectangle"), "REC",
                    menu_of({action("REC", QStringLiteral("Rectangle")), action("POL", QStringLiteral("Polygon"))}));
        small_split(draw, col, QStringLiteral("Ellipse"), "EL",
                    menu_of({macro(QStringLiteral("Center"), "EL", {"C"}),
                             macro(QStringLiteral("Axis, End"), "EL", {}),
                             macro(QStringLiteral("Elliptical Arc"), "EL", {"A"})}));
        small_split(draw, col, QStringLiteral("Hatch"), "H",
                    menu_of({action("H", QStringLiteral("Hatch")),
                             macro(QStringLiteral("Gradient"), "H", {"G"}, "gradient")}));
    }
    {
        QWidget* out = draw->expander();
        QWidget* c1 = draw->add_column_to(out);
        small(draw, c1, QStringLiteral("Revision Cloud"), "REVCLOUD");
        small(draw, c1, QStringLiteral("Wipeout"), "WIPEOUT", "wipeout");
        small(draw, c1, QStringLiteral("Donut"), "DO");
        QWidget* c2 = draw->add_column_to(out);
        bind(draw->add_small_dropdown(c2, icon_of("SPL"), QStringLiteral("Spline"),
                                      menu_of({macro(QStringLiteral("Spline Fit"), "SPL", {"M", "F"}),
                                               macro(QStringLiteral("Spline CV"), "SPL", {"M", "CV"})}),
                                      true),
             "SPL");
        bind(draw->add_small_dropdown(c2, icon_of("PO"), QStringLiteral("Point"),
                                      menu_of({action("PO", QStringLiteral("Multiple Points")),
                                               action("DIV", QStringLiteral("Divide")),
                                               action("ME", QStringLiteral("Measure"))}),
                                      true),
             "PO");
        small(draw, c2, QStringLiteral("Polygon"), "POL");
        QWidget* c3 = draw->add_column_to(out);
        small(draw, c3, QStringLiteral("Construction Line"), "XL");
        small(draw, c3, QStringLiteral("Ray"), "RAY");
        todo_small(draw, c3, QStringLiteral("Region"), "region");
    }

    // --- Modify: AutoCAD's three columns of three, then the icon-only Erase / Explode /
    // Offset column; the slide-out holds Lengthen, Edit Polyline, Break, Join, Align...
    RibbonPanel* modify = ribbon_->add_panel(home, QStringLiteral("Modify"), 95);
    modify->set_representative_icon(asset("move"));
    QToolButton* move_btn = nullptr;
    QToolButton* copy_btn = nullptr;
    QToolButton* rotate_btn = nullptr;
    QToolButton* mirror_btn = nullptr;
    QToolButton* scale_btn = nullptr;
    QToolButton* array_btn = nullptr;
    {
        QWidget* c1 = modify->add_column();
        move_btn = small(modify, c1, QStringLiteral("Move"), "M");
        copy_btn = small(modify, c1, QStringLiteral("Copy"), "CO");
        small(modify, c1, QStringLiteral("Stretch"), "S");
        QWidget* c2 = modify->add_column();
        rotate_btn = small(modify, c2, QStringLiteral("Rotate"), "RO");
        mirror_btn = small(modify, c2, QStringLiteral("Mirror"), "MI");
        scale_btn = small(modify, c2, QStringLiteral("Scale"), "SC");
        QWidget* c3 = modify->add_column(/*icon_only=*/true);
        small_split(modify, c3, QStringLiteral("Trim"), "TR",
                    menu_of({action("TR", QStringLiteral("Trim")), action("EX", QStringLiteral("Extend"))}));
        small_split(modify, c3, QStringLiteral("Fillet"), "F",
                    menu_of({action("F", QStringLiteral("Fillet")), action("CHA", QStringLiteral("Chamfer"))}));
        // ARRAY opens the parametric dialog (typing "AR" still runs the command line); the
        // arrow offers the three kinds.
        auto* array_menu = menu_of({action("ARRAYRECT", QStringLiteral("Rectangular Array")),
                                    action("ARRAYPATH", QStringLiteral("Path Array")),
                                    action("ARRAYPOLAR", QStringLiteral("Polar Array"))});
        array_btn = modify->add_small_dropdown(c3, icon_of("AR"), QStringLiteral("Array"), array_menu, true);
        array_btn->setObjectName(QStringLiteral("ribbon.cmd.array"));
        if (const command::CommandInfo* info = processor_->registry().find("AR"); info != nullptr) {
            array_btn->setToolTip(cmd_tooltip(*info));
        }
        connect(array_btn, &QToolButton::clicked, this, [this] { open_array_dialog(); });
        QWidget* c4 = modify->add_column(/*icon_only=*/true);
        small(modify, c4, QStringLiteral("Erase"), "ERASE");
        small(modify, c4, QStringLiteral("Explode"), "X");
        small(modify, c4, QStringLiteral("Offset"), "O");
        // Rotate / Scale by a typed value with a live ghost, from their arrows (issue #32).
        rotate_btn->setMenu(menu_of({}));
        rotate_btn->menu()->addAction(QStringLiteral("Rotate by value…"), this, [this] { open_rotate_dialog(); });
        rotate_btn->setPopupMode(QToolButton::MenuButtonPopup);
        scale_btn->setMenu(menu_of({}));
        scale_btn->menu()->addAction(QStringLiteral("Scale by value…"), this, [this] { open_scale_dialog(); });
        scale_btn->setPopupMode(QToolButton::MenuButtonPopup);
    }
    {
        QWidget* out = modify->expander();
        QWidget* c1 = modify->add_column_to(out);
        small(modify, c1, QStringLiteral("Lengthen"), "LEN");
        small(modify, c1, QStringLiteral("Edit Polyline"), "PE", "polyline");
        small(modify, c1, QStringLiteral("Break"), "BR");
        QWidget* c2 = modify->add_column_to(out);
        small(modify, c2, QStringLiteral("Break at Point"), "BREAKATPOINT", "break");
        small(modify, c2, QStringLiteral("Join"), "J");
        small(modify, c2, QStringLiteral("Align"), "AL");
        QWidget* c3 = modify->add_column_to(out);
        small(modify, c3, QStringLiteral("Match Properties"), "MA");
        todo_small(modify, c3, QStringLiteral("Blend Curves"), "blend");
        todo_small(modify, c3, QStringLiteral("Delete Duplicate Objects"), "overkill");
    }
    // Move / Copy / Mirror / Rotate / Scale / Array ask "Select objects:" when nothing is
    // selected (issue #46), so they stay enabled, as AutoCAD's do.
    (void)move_btn;
    (void)copy_btn;
    (void)mirror_btn;
    (void)rotate_btn;
    (void)scale_btn;
    (void)array_btn;
    selection_required_buttons_.clear();

    // --- Annotation: Text and Dimension large; Linear / Leader / Table stacked; the
    // styles behind the launcher and in the slide-out.
    RibbonPanel* annot = ribbon_->add_panel(home, QStringLiteral("Annotation"), 60);
    annot->set_representative_icon(asset("text"));
    large_split(annot, QStringLiteral("Text"), "MT",
                menu_of({action("MT", QStringLiteral("Multiline Text")), action("DT", QStringLiteral("Single Line"))}));
    large(annot, QStringLiteral("Dimension"), "DIM");
    {
        QWidget* col = annot->add_column(/*icon_only=*/true);
        small_split(annot, col, QStringLiteral("Linear"), "DLI",
                    menu_of({action("DLI", QStringLiteral("Linear")), action("DAL", QStringLiteral("Aligned")),
                             action("DAN", QStringLiteral("Angular")), action("DAR", QStringLiteral("Arc Length")),
                             action("DRA", QStringLiteral("Radius")), action("DDI", QStringLiteral("Diameter")),
                             action("DJO", QStringLiteral("Jogged")), action("DOR", QStringLiteral("Ordinate"))}));
        small_split(annot, col, QStringLiteral("Leader"), "LE",
                    menu_of({action("LE", QStringLiteral("Quick Leader")), action("LEADER", QStringLiteral("Leader"))}));
        small(annot, col, QStringLiteral("Table"), "TB");
    }
    {
        QWidget* out = annot->expander();
        QWidget* c1 = annot->add_column_to(out);
        QToolButton* dimstyle_btn = annot->add_small(c1, asset("dim-style"), QStringLiteral("Dimension Style"));
        dimstyle_btn->setObjectName(QStringLiteral("ribbon.dimstyle"));
        dimstyle_btn->setToolTip(QStringLiteral("Create and edit dimension styles."));
        connect(dimstyle_btn, &QToolButton::clicked, this, [this] { open_dimstyle_dialog(); });
        small(annot, c1, QStringLiteral("Text Style"), "ST", "text-style");
        small(annot, c1, QStringLiteral("Edit Text"), "ED", "text");
        QWidget* c2 = annot->add_column_to(out);
        small(annot, c2, QStringLiteral("Continue"), "DCO");
        small(annot, c2, QStringLiteral("Baseline"), "DBA");
        small(annot, c2, QStringLiteral("Tolerance"), "TOL");
    }
    annot->set_dialog_launcher(QStringLiteral("Dimension Style"), [this] { open_dimstyle_dialog(); });

    // --- Layers: Layer Properties large, the current-layer control, and the layer tools
    // (the tools AutoCAD keeps here -- Off, Isolate, Freeze, Make Current -- are #69).
    RibbonPanel* layers = ribbon_->add_panel(home, QStringLiteral("Layers"), 55);
    layers->set_representative_icon(asset("layers"));
    QToolButton* layer_btn = layers->add_button(asset("layer-props"), QStringLiteral("Layer\nProperties"));
    layer_btn->setObjectName(QStringLiteral("ribbon.layer_manager"));
    layer_btn->setToolTip(QStringLiteral("Open the Layer Properties manager."));
    connect(layer_btn, &QToolButton::clicked, this, [this] { open_layer_dialog(); });
    {
        QWidget* col = layers->add_column(/*icon_only=*/true);
        layer_combo_ = new QComboBox(col);
        layer_combo_->setObjectName(QStringLiteral("CurrentLayerCombo"));
        layer_combo_->setMinimumWidth(150);
        layer_combo_->setToolTip(QStringLiteral("Current layer"));
        connect(layer_combo_, &QComboBox::activated, this, [this](int index) {
            if (index >= 0) {
                engine_->submit(core::SetCurrentLayerCommand{static_cast<std::uint16_t>(index)});
            }
        });
        col->layout()->addWidget(layer_combo_);
        auto* row = new QWidget(col);
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(1);
        col->layout()->addWidget(row);
        QWidget* c2 = layers->add_column_to(row, /*icon_only=*/true);
        todo_small(layers, c2, QStringLiteral("Off"), "layer-off");
        todo_small(layers, c2, QStringLiteral("Isolate"), "layer-iso");
        QWidget* c3 = layers->add_column_to(row, /*icon_only=*/true);
        todo_small(layers, c3, QStringLiteral("Freeze"), "layer-freeze");
        todo_small(layers, c3, QStringLiteral("Make Current"), "layer-current");
    }

    // --- Block: Insert large; Create / Edit / Define Attributes stacked.
    RibbonPanel* block = ribbon_->add_panel(home, QStringLiteral("Block"), 45);
    block->set_representative_icon(asset("insert"));
    large(block, QStringLiteral("Insert"), "I");
    {
        QWidget* col = block->add_column(/*icon_only=*/true);
        small_split(block, col, QStringLiteral("Create"), "B",
                    menu_of({action("B", QStringLiteral("Create Block")), action("W", QStringLiteral("Write Block"))}));
        small(block, col, QStringLiteral("Edit"), "REFEDIT");
        small_split(block, col, QStringLiteral("Edit Attributes"), "EATTEDIT",
                    menu_of({action("EATTEDIT", QStringLiteral("Single")), action("ATTEDIT", QStringLiteral("Command line")),
                             action("BATTMAN", QStringLiteral("Block Attribute Manager"))}));
    }

    // --- Properties: Match Properties large; the colour / linetype / lineweight controls
    // (the selection's, or the current entity properties when nothing is selected).
    RibbonPanel* props = ribbon_->add_panel(home, QStringLiteral("Properties"), 40);
    props->set_representative_icon(asset("color-swatch"));
    large(props, QStringLiteral("Match\nProperties"), "MA", "matchprop");
    {
        QWidget* col = props->add_column(/*icon_only=*/true);
        color_combo_ = new QComboBox(col);
        color_combo_->setObjectName(QStringLiteral("ribbon.color"));
        color_combo_->setToolTip(QStringLiteral("Object colour: the selection's, or what new objects get (ByLayer or an override)."));
        color_combo_->addItem(asset("color-swatch"), QStringLiteral("ByLayer"));
        for (const NamedColor& c : kColors) {
            color_combo_->addItem(swatch_icon(c.rgb), QString::fromUtf8(c.name));
        }
        color_combo_->addItem(QStringLiteral("Select Colour…"));
        color_combo_->setMinimumWidth(130);
        col->layout()->addWidget(color_combo_);
        linetype_combo_ = new QComboBox(col);
        linetype_combo_->setObjectName(QStringLiteral("ribbon.linetype"));
        linetype_combo_->setToolTip(QStringLiteral("Linetype: the selection's, or what new objects get."));
        linetype_combo_->addItems({QStringLiteral("ByLayer"), QStringLiteral("Continuous"), QStringLiteral("Dashed"),
                                   QStringLiteral("Center"), QStringLiteral("Hidden")});
        col->layout()->addWidget(linetype_combo_);
        lineweight_combo_ = new QComboBox(col);
        lineweight_combo_->setObjectName(QStringLiteral("ribbon.lineweight"));
        lineweight_combo_->setToolTip(QStringLiteral("Lineweight: the selection's, or what new objects get."));
        lineweight_combo_->addItem(QStringLiteral("ByLayer"));
        for (const int lw : kLineweights) {
            lineweight_combo_->addItem(QStringLiteral("%1 mm").arg(lw / 100.0, 0, 'f', 2), lw);
        }
        col->layout()->addWidget(lineweight_combo_);
        connect(color_combo_, &QComboBox::activated, this, [this](int index) { apply_property_combo(core::PropertyId::Color, index); });
        connect(linetype_combo_, &QComboBox::activated, this,
                [this](int index) { apply_property_combo(core::PropertyId::Linetype, index); });
        connect(lineweight_combo_, &QComboBox::activated, this,
                [this](int index) { apply_property_combo(core::PropertyId::Lineweight, index); });
    }
    {
        QWidget* out = props->expander();
        QWidget* c1 = props->add_column_to(out);
        small(props, c1, QStringLiteral("Linetype Scale"), "LTS");
        small(props, c1, QStringLiteral("List"), "LI");
        QToolButton* lwt_btn = props->add_small(c1, asset("lwt"), QStringLiteral("Lineweight Display"));
        lwt_btn->setObjectName(QStringLiteral("ribbon.lwt"));
        lwt_btn->setToolTip(QStringLiteral("Toggle display of lineweights (LWDISPLAY)."));
        lwt_btn->setCheckable(true);
        lwt_btn->setChecked(true); // LWDISPLAY defaults on
        connect(lwt_btn, &QToolButton::toggled, this,
                [this](bool on) { engine_->submit(core::SetLineweightDisplayCommand{on}); });
    }
    props->set_dialog_launcher(QStringLiteral("Properties palette"), [this] {
        command_widget_->focus_input();
        processor_->start_command("PR");
    });

    // --- Groups: Group large; Ungroup / Group Selection stacked.
    RibbonPanel* groups = ribbon_->add_panel(home, QStringLiteral("Groups"), 30);
    groups->set_representative_icon(asset("group"));
    large(groups, QStringLiteral("Group"), "G");
    {
        QWidget* col = groups->add_column(/*icon_only=*/true);
        small(groups, col, QStringLiteral("Ungroup"), "UNGROUP");
        small(groups, col, QStringLiteral("Group Edit"), "GROUPEDIT", "group");
        QToolButton* pick = groups->add_small(col, asset("group-select"), QStringLiteral("Group Selection On/Off"));
        pick->setObjectName(QStringLiteral("ribbon.pickstyle"));
        pick->setToolTip(QStringLiteral("Whether picking one member selects its whole group (PICKSTYLE)."));
        pick->setCheckable(true);
        pick->setChecked(true);
        connect(pick, &QToolButton::toggled, this, [this](bool on) {
            processor_->start_macro("PICKSTYLE", {on ? "1" : "0"});
        });
    }

    // --- Utilities: Measure large; Quick Select / Quick Calculator / ID Point stacked.
    RibbonPanel* util = ribbon_->add_panel(home, QStringLiteral("Utilities"), 25);
    util->set_representative_icon(asset("measure"));
    large_split(util, QStringLiteral("Measure"), "DI",
                menu_of({action("DI", QStringLiteral("Distance")), action("AA", QStringLiteral("Area")),
                         action("ID", QStringLiteral("ID Point")), action("LI", QStringLiteral("List"))}),
                "distance");
    {
        QWidget* col = util->add_column(/*icon_only=*/true);
        small(util, col, QStringLiteral("Quick Select"), "QSELECT", "quick-select");
        todo_small(util, col, QStringLiteral("Quick Calculator"), "calculator");
        small(util, col, QStringLiteral("ID Point"), "ID", "id-point");
    }

    // --- Clipboard: Paste large; Cut / Copy Clip / Copy with Base Point stacked.
    RibbonPanel* clip = ribbon_->add_panel(home, QStringLiteral("Clipboard"), 20);
    clip->set_representative_icon(asset("paste"));
    {
        QToolButton* paste = clip->add_button(asset("paste"), QStringLiteral("Paste"));
        paste->setObjectName(QStringLiteral("ribbon.paste"));
        paste->setToolTip(QStringLiteral("<b>PASTE (Ctrl+V)</b><br>Paste the clipboard's objects at the cursor."));
        connect(paste, &QToolButton::clicked, this, clip_paste);
        QWidget* col = clip->add_column(/*icon_only=*/true);
        QToolButton* cut = clip->add_small(col, asset("cut"), QStringLiteral("Cut"));
        cut->setObjectName(QStringLiteral("ribbon.cut"));
        cut->setToolTip(QStringLiteral("<b>CUT (Ctrl+X)</b><br>Move the selection to the clipboard."));
        connect(cut, &QToolButton::clicked, this, clip_cut);
        QToolButton* copyc = clip->add_small(col, asset("copyclip"), QStringLiteral("Copy Clip"));
        copyc->setObjectName(QStringLiteral("ribbon.copyclip"));
        copyc->setToolTip(QStringLiteral("<b>COPYCLIP (Ctrl+C)</b><br>Copy the selection to the clipboard."));
        connect(copyc, &QToolButton::clicked, this, clip_copy);
        todo_small(clip, col, QStringLiteral("Copy with Base Point"), "copybase");
        selection_required_buttons_.push_back(cut);
        selection_required_buttons_.push_back(copyc);
        cut->setEnabled(false);
        copyc->setEnabled(false);
    }

    // ================================================================== Insert
    const int insert = ribbon_->add_tab(QStringLiteral("Insert"));
    RibbonPanel* iblock = ribbon_->add_panel(insert, QStringLiteral("Block"), 60);
    iblock->set_representative_icon(asset("insert"));
    large(iblock, QStringLiteral("Insert"), "I");
    {
        QWidget* col = iblock->add_column();
        small_split(iblock, col, QStringLiteral("Create"), "B",
                    menu_of({action("B", QStringLiteral("Create Block")), action("W", QStringLiteral("Write Block"))}));
        small(iblock, col, QStringLiteral("Edit"), "REFEDIT");
        small(iblock, col, QStringLiteral("Explode"), "X");
    }
    RibbonPanel* blockdef = ribbon_->add_panel(insert, QStringLiteral("Block Definition"), 55);
    blockdef->set_representative_icon(asset("attdef"));
    large(blockdef, QStringLiteral("Define\nAttributes"), "ATT");
    {
        QWidget* col = blockdef->add_column();
        small_split(blockdef, col, QStringLiteral("Edit Attribute"), "EATTEDIT",
                    menu_of({action("EATTEDIT", QStringLiteral("Single")), action("ATTEDIT", QStringLiteral("Command line")),
                             action("ATTDISP", QStringLiteral("Attribute Display"))}));
        small(blockdef, col, QStringLiteral("Manage Attributes"), "BATTMAN");
        small(blockdef, col, QStringLiteral("Edit Reference"), "REFEDIT");
    }
    RibbonPanel* reference = ribbon_->add_panel(insert, QStringLiteral("Reference"), 50);
    reference->set_representative_icon(asset("xref"));
    large_split(reference, QStringLiteral("Attach"), "XR",
                menu_of({macro(QStringLiteral("Attach Xref"), "XR", {"A"}), action("IAT", QStringLiteral("Attach Image"), "image")}));
    {
        QWidget* col = reference->add_column();
        small(reference, col, QStringLiteral("Clip"), "ICL", "imageclip");
        bind(reference->add_small_dropdown(col, asset("imageframe"), QStringLiteral("Frames"),
                                           menu_of({macro(QStringLiteral("Hide frames"), "IMAGEFRAME", {"0"}, "imageframe"),
                                                    macro(QStringLiteral("Show and plot frames"), "IMAGEFRAME", {"1"}, "imageframe"),
                                                    macro(QStringLiteral("Show frames, do not plot"), "IMAGEFRAME", {"2"}, "imageframe")}),
                                           false),
             "IMAGEFRAME");
        bind(reference->add_small_dropdown(col, asset("xref"), QStringLiteral("External References"),
                                           menu_of({macro(QStringLiteral("List"), "XR", {"?"}), macro(QStringLiteral("Reload"), "XR", {"R"}),
                                                    macro(QStringLiteral("Detach"), "XR", {"D"})}),
                                           false),
             "XR");
    }
    RibbonPanel* import_p = ribbon_->add_panel(insert, QStringLiteral("Import"), 30);
    import_p->set_representative_icon(asset("import"));
    {
        auto* m = new QMenu(this);
        connect(m->addAction(ribbon_icon(QStringLiteral("assets/ribbon/import.svg")), QStringLiteral("DXF…")), &QAction::triggered,
                this, &MainWindow::file_import_dxf);
        connect(m->addAction(ribbon_icon(QStringLiteral("assets/ribbon/import.svg")), QStringLiteral("DWG…")), &QAction::triggered,
                this, &MainWindow::file_import_dwg);
        QToolButton* b = import_p->add_dropdown(asset("import"), QStringLiteral("Import"), m, /*split=*/false, RibbonTier::Primary);
        b->setObjectName(QStringLiteral("ribbon.import"));
        b->setToolTip(QStringLiteral("Import geometry from a DXF or DWG file."));
    }
    RibbonPanel* data = ribbon_->add_panel(insert, QStringLiteral("Data"), 20);
    data->set_representative_icon(asset("field"));
    large(data, QStringLiteral("Field"), "FIELD", "field");
    {
        QWidget* col = data->add_column();
        small(data, col, QStringLiteral("Table"), "TB");
        todo_small(data, col, QStringLiteral("Update Fields"), "field");
        todo_small(data, col, QStringLiteral("Data Link"), "field");
    }

    // ================================================================== Annotate
    const int annotate = ribbon_->add_tab(QStringLiteral("Annotate"));
    RibbonPanel* text = ribbon_->add_panel(annotate, QStringLiteral("Text"), 60);
    text->set_representative_icon(asset("text"));
    large_split(text, QStringLiteral("Multiline\nText"), "MT",
                menu_of({action("MT", QStringLiteral("Multiline Text")), action("DT", QStringLiteral("Single Line"))}));
    {
        QWidget* col = text->add_column();
        small(text, col, QStringLiteral("Text Style"), "ST", "text-style");
        small(text, col, QStringLiteral("Edit Text"), "ED", "text");
        todo_small(text, col, QStringLiteral("Find Text"), "find");
    }
    RibbonPanel* dims = ribbon_->add_panel(annotate, QStringLiteral("Dimensions"), 55);
    dims->set_representative_icon(asset("dim"));
    large(dims, QStringLiteral("Dimension"), "DIM");
    {
        QWidget* col = dims->add_column();
        small_split(dims, col, QStringLiteral("Linear"), "DLI",
                    menu_of({action("DLI", QStringLiteral("Linear")), action("DAL", QStringLiteral("Aligned")),
                             action("DAN", QStringLiteral("Angular")), action("DAR", QStringLiteral("Arc Length")),
                             action("DRA", QStringLiteral("Radius")), action("DDI", QStringLiteral("Diameter")),
                             action("DJO", QStringLiteral("Jogged")), action("DOR", QStringLiteral("Ordinate"))}));
        small_split(dims, col, QStringLiteral("Continue"), "DCO",
                    menu_of({action("DCO", QStringLiteral("Continue")), action("DBA", QStringLiteral("Baseline"))}));
        QToolButton* ds = dims->add_small(col, asset("dim-style"), QStringLiteral("Dimension Style"));
        ds->setObjectName(QStringLiteral("ribbon.dimstyle2"));
        ds->setToolTip(QStringLiteral("Create and edit dimension styles."));
        connect(ds, &QToolButton::clicked, this, [this] { open_dimstyle_dialog(); });
    }
    {
        QWidget* out = dims->expander();
        QWidget* c1 = dims->add_column_to(out);
        small(dims, c1, QStringLiteral("Tolerance"), "TOL");
        small(dims, c1, QStringLiteral("Datum"), "DIMDATUM", "datum");
        todo_small(dims, c1, QStringLiteral("Center Mark"), "centermark");
    }
    dims->set_dialog_launcher(QStringLiteral("Dimension Style"), [this] { open_dimstyle_dialog(); });
    RibbonPanel* leaders = ribbon_->add_panel(annotate, QStringLiteral("Leaders"), 40);
    leaders->set_representative_icon(asset("leader"));
    large_split(leaders, QStringLiteral("Leader"), "LE",
                menu_of({action("LE", QStringLiteral("Quick Leader")), action("LEADER", QStringLiteral("Leader"))}));
    RibbonPanel* tables = ribbon_->add_panel(annotate, QStringLiteral("Tables"), 30);
    tables->set_representative_icon(asset("table"));
    large(tables, QStringLiteral("Table"), "TB");
    RibbonPanel* markup = ribbon_->add_panel(annotate, QStringLiteral("Markup"), 20);
    markup->set_representative_icon(asset("revcloud"));
    large(markup, QStringLiteral("Revision\nCloud"), "REVCLOUD");
    large(markup, QStringLiteral("Wipeout"), "WIPEOUT", "wipeout", RibbonTier::Secondary);

    // ================================================================== Parametric
    // AutoCAD's constraint panels, in place; the constraints themselves are #70.
    const int parametric = ribbon_->add_tab(QStringLiteral("Parametric"));
    RibbonPanel* geometric = ribbon_->add_panel(parametric, QStringLiteral("Geometric"), 50);
    geometric->set_representative_icon(asset("c-coincident"));
    {
        const char* names[12][2] = {{"Coincident", "c-coincident"}, {"Collinear", "c-collinear"}, {"Concentric", "c-concentric"},
                                    {"Fix", "c-fix"},               {"Parallel", "c-parallel"},   {"Perpendicular", "c-perpendicular"},
                                    {"Horizontal", "c-horizontal"}, {"Vertical", "c-vertical"},   {"Tangent", "c-tangent"},
                                    {"Smooth", "c-smooth"},         {"Symmetric", "c-symmetric"}, {"Equal", "c-equal"}};
        QWidget* col = nullptr;
        for (int i = 0; i < 12; ++i) {
            if (i % 3 == 0) {
                col = geometric->add_column();
            }
            todo_small(geometric, col, QString::fromUtf8(names[i][0]), names[i][1]);
        }
    }
    RibbonPanel* dimensional = ribbon_->add_panel(parametric, QStringLiteral("Dimensional"), 40);
    dimensional->set_representative_icon(asset("dim-linear"));
    {
        QWidget* c1 = dimensional->add_column();
        todo_small(dimensional, c1, QStringLiteral("Linear"), "dim-linear");
        todo_small(dimensional, c1, QStringLiteral("Aligned"), "dim-aligned");
        todo_small(dimensional, c1, QStringLiteral("Radius"), "dim-radius");
        QWidget* c2 = dimensional->add_column();
        todo_small(dimensional, c2, QStringLiteral("Diameter"), "dim-diameter");
        todo_small(dimensional, c2, QStringLiteral("Angular"), "dim-angular");
    }
    RibbonPanel* pmanage = ribbon_->add_panel(parametric, QStringLiteral("Manage"), 30);
    pmanage->set_representative_icon(asset("parameters"));
    pmanage->add_placeholder(asset("delconstraint"), QStringLiteral("Delete\nConstraints"), RibbonTier::Primary);
    pmanage->add_placeholder(asset("parameters"), QStringLiteral("Parameters\nManager"), RibbonTier::Primary);

    // ================================================================== View
    const int view = ribbon_->add_tab(QStringLiteral("View"));
    RibbonPanel* nav = ribbon_->add_panel(view, QStringLiteral("Navigate 2D"), 60);
    nav->set_representative_icon(asset("zoom"));
    {
        auto* zoom_menu = new QMenu(this);
        QAction* ze = zoom_menu->addAction(asset("zoom-extents"), QStringLiteral("Zoom Extents"));
        connect(ze, &QAction::triggered, this, [this] { viewport_->zoom_extents(); });
        zoom_menu->addAction(macro(QStringLiteral("Zoom All"), "Z", {"A"}));
        zoom_menu->addAction(macro(QStringLiteral("Zoom In"), "Z", {"2"}));
        zoom_menu->addAction(macro(QStringLiteral("Zoom Out"), "Z", {"0.5"}));
        zoom_menu->addAction(action("Z", QStringLiteral("Zoom Scale")));
        QToolButton* zoom = nav->add_dropdown(asset("zoom"), QStringLiteral("Zoom"), zoom_menu, /*split=*/true, RibbonTier::Primary);
        zoom->setObjectName(QStringLiteral("ribbon.cmd.Z"));
        zoom->setToolTip(QStringLiteral("<b>ZOOM (Z)</b><br>Zoom in or out to change the view magnification."));
        connect(zoom, &QToolButton::clicked, this, [this] { viewport_->zoom_extents(); });
        QWidget* col = nav->add_column();
        QToolButton* zext = nav->add_small(col, asset("zoom-extents"), QStringLiteral("Zoom Extents"));
        zext->setObjectName(QStringLiteral("ribbon.zoom_extents"));
        zext->setToolTip(QStringLiteral("Zoom to show the full drawing extents."));
        connect(zext, &QToolButton::clicked, this, [this] { viewport_->zoom_extents(); });
        todo_small(nav, col, QStringLiteral("Pan"), "pan");
    }
    RibbonPanel* named = ribbon_->add_panel(view, QStringLiteral("Named Views"), 50);
    named->set_representative_icon(asset("view"));
    large_split(named, QStringLiteral("Named\nViews"), "V",
                menu_of({macro(QStringLiteral("Save View"), "V", {"S"}), macro(QStringLiteral("Restore View"), "V", {"R"}),
                         macro(QStringLiteral("Delete View"), "V", {"D"}), macro(QStringLiteral("List Views"), "V", {"?"})}));
    {
        QWidget* col = named->add_column();
        bind(named->add_small(col, asset("view"), QStringLiteral("New View")), "V");
        todo_small(named, col, QStringLiteral("View Manager"), "view");
    }
    RibbonPanel* mvp = ribbon_->add_panel(view, QStringLiteral("Model Viewports"), 45);
    mvp->set_representative_icon(asset("vports"));
    {
        auto* vp_menu = new QMenu(this);
        const auto vp_action = [&](const char* kind, const QString& label) {
            QAction* a = vp_menu->addAction(label);
            a->setObjectName(QStringLiteral("vports.%1").arg(QString::fromUtf8(kind)));
            connect(a, &QAction::triggered, this, [this, kind] { apply_vport_configuration(kind); });
        };
        vp_action("single", QStringLiteral("Single"));
        vp_action("2v", QStringLiteral("Two: Vertical"));
        vp_action("2h", QStringLiteral("Two: Horizontal"));
        vp_action("3r", QStringLiteral("Three: Right"));
        vp_action("3l", QStringLiteral("Three: Left"));
        vp_action("3a", QStringLiteral("Three: Above"));
        vp_action("3b", QStringLiteral("Three: Below"));
        vp_action("3v", QStringLiteral("Three: Vertical"));
        vp_action("3h", QStringLiteral("Three: Horizontal"));
        vp_action("4", QStringLiteral("Four: Equal"));
        QToolButton* vp_btn = mvp->add_dropdown(asset("vports"), QStringLiteral("Viewport\nConfiguration"), vp_menu,
                                                /*split=*/false, RibbonTier::Primary);
        vp_btn->setObjectName(QStringLiteral("ribbon.vports"));
        vp_btn->setToolTip(QStringLiteral("Split the model window into tiled viewports, each with its own "
                                          "view (VPORTS). Click a viewport to make it current."));
        QWidget* col = mvp->add_column();
        small(mvp, col, QStringLiteral("Named"), "VPORTS", "vports");
        bind(mvp->add_small(col, asset("vports"), QStringLiteral("Join")), "VPORTS");
    }
    RibbonPanel* palettes = ribbon_->add_panel(view, QStringLiteral("Palettes"), 40);
    palettes->set_representative_icon(asset("properties"));
    {
        QToolButton* pp = palettes->add_button(asset("properties"), QStringLiteral("Properties"));
        pp->setObjectName(QStringLiteral("ribbon.cmd.PR"));
        pp->setToolTip(QStringLiteral("<b>PROPERTIES (PR)</b><br>Show or hide the Properties palette."));
        connect(pp, &QToolButton::clicked, this, [this] {
            command_widget_->focus_input();
            processor_->start_command("PR");
        });
        QToolButton* lp = palettes->add_button(asset("layer-props"), QStringLiteral("Layer\nProperties"));
        lp->setObjectName(QStringLiteral("ribbon.layer_manager2"));
        lp->setToolTip(QStringLiteral("Open the Layer Properties manager."));
        connect(lp, &QToolButton::clicked, this, [this] { open_layer_dialog(); });
        palettes->add_placeholder(asset("settings"), QStringLiteral("Tool\nPalettes"), RibbonTier::Secondary);
    }

    // ================================================================== Manage
    const int manage = ribbon_->add_tab(QStringLiteral("Manage"));
    RibbonPanel* cleanup = ribbon_->add_panel(manage, QStringLiteral("Cleanup"), 50);
    cleanup->set_representative_icon(asset("purge"));
    large(cleanup, QStringLiteral("Purge"), "PU");
    large(cleanup, QStringLiteral("Audit"), "AUDIT", "audit");
    RibbonPanel* customize = ribbon_->add_panel(manage, QStringLiteral("Customization"), 40);
    customize->set_representative_icon(asset("settings"));
    customize->add_placeholder(asset("settings"), QStringLiteral("User\nInterface"), RibbonTier::Primary);
    customize->add_placeholder(asset("settings"), QStringLiteral("Tool\nPalettes"), RibbonTier::Secondary);

    // ================================================================== Output
    const int output = ribbon_->add_tab(QStringLiteral("Output"));
    RibbonPanel* plot = ribbon_->add_panel(output, QStringLiteral("Plot"), 50);
    plot->set_representative_icon(asset("plot"));
    {
        QToolButton* p = plot->add_button(asset("plot"), QStringLiteral("Plot"));
        p->setObjectName(QStringLiteral("ribbon.plot"));
        p->setToolTip(QStringLiteral("<b>PLOT (Ctrl+P)</b><br>Plot or print the drawing to paper or PDF."));
        connect(p, &QToolButton::clicked, this, &MainWindow::open_plot_dialog);
        QWidget* col = plot->add_column();
        todo_small(plot, col, QStringLiteral("Batch Plot"), "plot");
        todo_small(plot, col, QStringLiteral("Preview"), "preview");
        todo_small(plot, col, QStringLiteral("Page Setup Manager"), "pagesetup");
    }
    RibbonPanel* export_p = ribbon_->add_panel(output, QStringLiteral("Export to DWG/DXF/PDF"), 40);
    export_p->set_representative_icon(asset("export"));
    {
        auto* m = new QMenu(this);
        connect(m->addAction(ribbon_icon(QStringLiteral("assets/ribbon/export.svg")), QStringLiteral("DXF…")), &QAction::triggered,
                this, &MainWindow::file_export_dxf);
        connect(m->addAction(ribbon_icon(QStringLiteral("assets/ribbon/export.svg")), QStringLiteral("DWG…")), &QAction::triggered,
                this, &MainWindow::file_export_dwg);
        connect(m->addAction(ribbon_icon(QStringLiteral("assets/ribbon/plot.svg")), QStringLiteral("PDF (Plot)…")), &QAction::triggered,
                this, &MainWindow::open_plot_dialog);
        QToolButton* b = export_p->add_dropdown(asset("export"), QStringLiteral("Export"), m, /*split=*/false, RibbonTier::Primary);
        b->setObjectName(QStringLiteral("ribbon.export"));
        b->setToolTip(QStringLiteral("Export the drawing to a DXF or DWG file, or plot it to PDF."));
        QWidget* col = export_p->add_column();
        QToolButton* setup = export_p->add_small(col, asset("settings"), QStringLiteral("DWG Setup"));
        setup->setObjectName(QStringLiteral("ribbon.dwg_setup"));
        setup->setToolTip(QStringLiteral("Configure or download the external DWG converter."));
        connect(setup, &QToolButton::clicked, this, &MainWindow::configure_dwg_converter);
    }

    // Contextual tabs (hatch / text / block editor), shown reactively by the selection poll.
    build_contextual_tabs();

    setMenuWidget(ribbon_);
}

// The Properties panel's colour / linetype / lineweight controls: with a selection they
// edit it (the palette's write path); with none they set what new objects get (the
// current entity properties, AutoCAD's CECOLOR / CELTYPE / CELWEIGHT).
void MainWindow::apply_property_combo(core::PropertyId id, int index) {
    using core::PropertyValue;
    const int sel = viewport_ != nullptr ? viewport_->selection_count() : 0;
    core::EntityProps cur = viewport_ != nullptr ? viewport_->current_props() : core::EntityProps{};
    PropertyValue v;
    if (id == core::PropertyId::Color) {
        if (index == 0) {
            v.flag = true;
            cur.set_color_by_layer(true);
        } else if (index == color_combo_->count() - 1) {
            const QColor picked = QColorDialog::getColor(QColor(cur.color.r, cur.color.g, cur.color.b), this,
                                                         QStringLiteral("Select Colour"));
            if (!picked.isValid()) {
                refresh_property_combos(true);
                return;
            }
            v.flag = false;
            v.color = {static_cast<std::uint8_t>(picked.red()), static_cast<std::uint8_t>(picked.green()),
                       static_cast<std::uint8_t>(picked.blue())};
            cur.set_color_by_layer(false);
            cur.color = v.color;
        } else {
            v.flag = false;
            v.color = kColors[index - 1].rgb;
            cur.set_color_by_layer(false);
            cur.color = v.color;
        }
    } else if (id == core::PropertyId::Linetype) {
        v.flag = index == 0;
        v.choice = index == 0 ? 0 : index - 1;
        cur.set_linetype_by_layer(index == 0);
        if (index > 0) {
            cur.linetype = static_cast<core::Linetype>(index - 1);
        }
    } else {
        v.flag = index == 0;
        v.num = index == 0 ? 0.0 : lineweight_combo_->itemData(index).toDouble();
        cur.set_lineweight_by_layer(index == 0);
        if (index > 0) {
            cur.lineweight = static_cast<std::uint8_t>(lineweight_combo_->itemData(index).toInt());
        }
    }
    if (sel > 0) {
        engine_->submit(core::SetPropertyCommand{id, v, processor_->begin_group()});
    } else {
        engine_->submit(core::SetCurrentPropsCommand{cur});
    }
}

// Mirror the selection's (or the current) properties into the three controls; called from
// the selection poll. `force` repaints even when nothing changed (after a cancelled pick).
void MainWindow::refresh_property_combos(bool force) {
    if (color_combo_ == nullptr || viewport_ == nullptr) {
        return;
    }
    const int sel = viewport_->selection_count();
    const core::EntityProps cur = viewport_->current_props();
    // What each control should show: -1 = varies across the selection.
    int color_idx = 0;
    int lt_idx = 0;
    int lw_idx = 0;
    const auto index_of_color = [&](core::Rgb c) {
        for (int i = 0; i < static_cast<int>(std::size(kColors)); ++i) {
            if (kColors[i].rgb == c) {
                return i + 1;
            }
        }
        return color_combo_->count() - 1; // a custom colour shows as "Select Colour…"
    };
    const auto index_of_lw = [&](int hundredths) {
        for (int i = 1; i < lineweight_combo_->count(); ++i) {
            if (lineweight_combo_->itemData(i).toInt() == hundredths) {
                return i;
            }
        }
        return 0;
    };
    if (sel > 0 && properties_panel_ != nullptr && properties_panel_->has_field(core::PropertyId::Color)) {
        const auto field = [&](core::PropertyId id, int& out, auto from_value) {
            if (!properties_panel_->has_field(id) || properties_panel_->field_varies(id)) {
                out = -1;
                return;
            }
            const core::PropertyValue v = properties_panel_->field_value(id);
            out = v.flag ? 0 : from_value(v);
        };
        field(core::PropertyId::Color, color_idx, [&](const core::PropertyValue& v) { return index_of_color(v.color); });
        field(core::PropertyId::Linetype, lt_idx, [&](const core::PropertyValue& v) { return v.choice + 1; });
        field(core::PropertyId::Lineweight, lw_idx, [&](const core::PropertyValue& v) { return index_of_lw(static_cast<int>(v.num)); });
    } else {
        color_idx = cur.color_by_layer() ? 0 : index_of_color(cur.color);
        lt_idx = cur.linetype_by_layer() ? 0 : static_cast<int>(cur.linetype) + 1;
        lw_idx = cur.lineweight_by_layer() ? 0 : index_of_lw(cur.lineweight);
    }
    const auto show = [&](QComboBox* box, int idx) {
        if (box->currentIndex() != idx || force) {
            const QSignalBlocker block(box);
            box->setCurrentIndex(idx);
        }
    };
    show(color_combo_, color_idx);
    show(linetype_combo_, lt_idx);
    show(lineweight_combo_, lw_idx);
}

} // namespace musacad::ui
