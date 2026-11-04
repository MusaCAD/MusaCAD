// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

// The interface half of `musacad --dump-wiki-data`: walks the window the application
// builds -- the ribbon, the Quick Access Toolbar, the actions with keyboard shortcuts --
// and records what each control starts. The documentation is generated from this, so a
// tool moved on the ribbon, or a shortcut added, shows up there without anyone editing it.

#include <QAction>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeySequence>
#include <QMenu>
#include <QTextDocumentFragment>
#include <QToolButton>

#include "musacad/ui/main_window.hpp"
#include "musacad/ui/ribbon_bar.hpp"
#include "musacad/ui/viewport_window.hpp"

namespace musacad::ui {

namespace {

/// A label as shown, on one line, without the mnemonic ampersand.
QString clean(QString s) {
    s.replace(QLatin1Char('\n'), QLatin1Char(' '));
    s.remove(QLatin1Char('&'));
    return s.simplified();
}

/// A tooltip as plain text (they are rich text: "<b>NAME (ALIAS)</b><br>what it does").
QString plain(const QString& html) {
    if (html.isEmpty()) {
        return {};
    }
    QString t = QTextDocumentFragment::fromHtml(html).toPlainText();
    t.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return t.simplified();
}

/// The command a control starts and the input its macro types, if it was tagged.
void put_command(QJsonObject& o, const QObject* source) {
    const QVariant cmd = source->property("musacadCommand");
    if (cmd.isValid()) {
        o.insert(QStringLiteral("command"), cmd.toString());
    }
    const QVariant macro = source->property("musacadMacro");
    if (macro.isValid()) {
        o.insert(QStringLiteral("macro"), QJsonArray::fromStringList(macro.toStringList()));
    }
}

QJsonArray menu_items(const QMenu* menu) {
    QJsonArray out;
    if (menu == nullptr) {
        return out;
    }
    for (const QAction* a : menu->actions()) {
        if (a->isSeparator()) {
            continue;
        }
        QJsonObject item{{QStringLiteral("label"), clean(a->text())}};
        put_command(item, a);
        if (const QMenu* sub = a->menu()) {
            item.insert(QStringLiteral("menu"), menu_items(sub));
        }
        out.append(item);
    }
    return out;
}

QJsonObject tool(const QToolButton* b, bool slideout) {
    QJsonObject o{{QStringLiteral("label"), clean(b->text())}};
    put_command(o, b);
    if (b->property("musacadPlaceholder").toBool()) {
        o.insert(QStringLiteral("placeholder"), true);
        if (const QString tip = plain(b->toolTip()); !tip.isEmpty()) {
            o.insert(QStringLiteral("tooltip"), tip);
        }
    }
    if (slideout) {
        o.insert(QStringLiteral("slideout"), true);
    }
    if (b->menu() != nullptr) {
        o.insert(QStringLiteral("menu"), menu_items(b->menu()));
        o.insert(QStringLiteral("split"), b->popupMode() == QToolButton::MenuButtonPopup);
    }
    return o;
}

} // namespace

command::ViewControl* MainWindow::view_control() const { return viewport_; }

QJsonObject MainWindow::wiki_ui_data() const {
    QJsonObject out;

    // The ribbon: tab -> panel -> tool, in the order the window lays them out.
    QJsonArray tabs;
    for (const RibbonBar::TabStructure& tab : ribbon_->structure()) {
        QJsonArray panels;
        for (const RibbonPanel* panel : tab.panels) {
            QJsonArray tools;
            for (const bool slideout : {false, true}) {
                for (const QToolButton* b : panel->tool_buttons(slideout)) {
                    if (b->text().isEmpty() && b->menu() == nullptr) {
                        continue; // a bare arrow or spacer, not a tool
                    }
                    tools.append(tool(b, slideout));
                }
            }
            panels.append(QJsonObject{{QStringLiteral("title"), clean(panel->title())}, {QStringLiteral("tools"), tools}});
        }
        QJsonObject t{{QStringLiteral("title"), clean(tab.title)}, {QStringLiteral("panels"), panels}};
        if (tab.contextual) {
            t.insert(QStringLiteral("contextual"), true);
        }
        tabs.append(t);
    }
    out.insert(QStringLiteral("ribbon"), tabs);

    // The Quick Access Toolbar: what it can show, and what it shows on a fresh install.
    QJsonArray qat;
    if (qat_menu_ != nullptr) {
        for (const QAction* a : qat_menu_->actions()) {
            if (a->isSeparator() || a->data().toString() == QStringLiteral("below")) {
                continue;
            }
            qat.append(QJsonObject{{QStringLiteral("label"), clean(a->text())},
                                   {QStringLiteral("shown"), qat_default_shown_.contains(a->text())}});
        }
    }
    out.insert(QStringLiteral("quickAccessToolbar"), qat);

    // Every keyboard shortcut the window answers to through an action (the mode keys, the
    // file keys, the clipboard, the drawing tabs, the palettes).
    QJsonArray shortcuts;
    for (const QAction* a : findChildren<QAction*>()) {
        const QList<QKeySequence> keys = a->shortcuts();
        if (keys.isEmpty()) {
            continue;
        }
        QJsonArray k;
        for (const QKeySequence& seq : keys) {
            k.append(seq.toString(QKeySequence::PortableText));
        }
        QJsonObject s{{QStringLiteral("keys"), k}, {QStringLiteral("action"), clean(a->text())}};
        if (const QString tip = plain(a->toolTip()); !tip.isEmpty() && tip != clean(a->text())) {
            s.insert(QStringLiteral("description"), tip);
        }
        put_command(s, a);
        shortcuts.append(s);
    }
    out.insert(QStringLiteral("shortcuts"), shortcuts);
    return out;
}

} // namespace musacad::ui
