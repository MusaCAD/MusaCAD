// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#include "musacad/ui/theme.hpp"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QStyleFactory>
#include <QtGlobal>
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QStyleHints>
#endif

namespace musacad::ui {

namespace {
/// A dark palette matching the QSS, used for every surface QSS doesn't reach
/// (native-style dialogs, message boxes, menus, the file picker).
QPalette dark_palette() {
    const QColor window(0x2b, 0x2b, 0x2b);
    const QColor base(0x23, 0x23, 0x23);
    const QColor alt(0x32, 0x32, 0x32);
    const QColor text(0xd6, 0xd6, 0xd6);
    const QColor button(0x3c, 0x3c, 0x3c);
    const QColor highlight(0x4a, 0x90, 0xd9);
    const QColor disabled(0x70, 0x70, 0x70);

    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, alt);
    p.setColor(QPalette::ToolTipBase, alt);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, button);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, QColor(0xff, 0xff, 0xff));
    p.setColor(QPalette::Link, highlight);
    p.setColor(QPalette::Highlight, highlight);
    p.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
    p.setColor(QPalette::PlaceholderText, disabled);
    p.setColor(QPalette::Disabled, QPalette::Text, disabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    return p;
}
} // namespace

void apply_dark_theme(QApplication& app) {
    // Fusion honors the palette consistently across platforms (unlike native
    // styles, which may ignore it and render light dialogs).
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QApplication::setPalette(dark_palette());
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    // Announce the dark preference to the OS / portal (affects native pickers and,
    // on some compositors, window decorations).
    app.styleHints()->setColorScheme(Qt::ColorScheme::Dark);
#endif
    app.setStyleSheet(dark_theme_qss());
}

QString dark_theme_qss() {
    // Single source of styling. Colors approximate the AutoCAD 2023 dark theme.
    return QStringLiteral(R"QSS(
* {
    color: #d6d6d6;
    font-size: 11px;
}
QMainWindow, QWidget#MusaFrame {
    background: #2b2b2b;
}

/* ----- Quick Access Toolbar strip ----- */
QWidget#QatStrip {
    background: #3c3c3c;
    border-bottom: 1px solid #1f1f1f;
}
/* QAT application button: a clean, flat "Musa CAD" wordmark (matches the QPushButton
   by objectName), highlighting on hover. */
#AppButton {
    background: transparent;
    color: #e6e6e6;
    font-weight: 600;
    border: none;
    border-radius: 3px;
    padding: 2px 10px;
}
#AppButton:hover { background: #3a3a3a; }
#AppButton:pressed { background: #454545; }
QWidget#QatStrip QToolButton {
    background: transparent;
    border: none;
    padding: 3px;
}
QWidget#QatStrip QToolButton:hover { background: #505050; border-radius: 3px; }
QWidget#QatStrip QToolButton:disabled { color: #777; }

/* ----- Ribbon ----- */
QWidget#Ribbon { background: #333333; }
QTabBar#RibbonTabs::tab {
    background: #333333;
    padding: 5px 16px;
    border: none;
    color: #c0c0c0;
}
QTabBar#RibbonTabs::tab:selected {
    background: #3f3f3f;
    color: #ffffff;
    border-bottom: 2px solid #4a90d9;
}
QTabBar#RibbonTabs::tab:hover { color: #ffffff; }
QWidget#RibbonPage { background: #3f3f3f; }

QFrame#RibbonPanel {
    background: #3f3f3f;
    border-right: 1px solid #2b2b2b;
}
QLabel#RibbonPanelTitle {
    color: #9a9a9a;
    font-size: 10px;
    qproperty-alignment: AlignCenter;
}
QFrame#RibbonPanel QToolButton {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 3px;
    padding: 3px;
}
QFrame#RibbonPanel QToolButton:hover:enabled {
    background: #505868;
    border: 1px solid #4a90d9;
}
QFrame#RibbonPanel QToolButton:disabled { color: #6f6f6f; }

/* ----- File / layout tab rows ----- */
QTabBar#FileTabs::tab, QTabBar#LayoutTabs::tab {
    background: #2b2b2b;
    color: #c0c0c0;
    padding: 3px 14px;
    border: 1px solid #1f1f1f;
}
QTabBar#FileTabs::tab:selected, QTabBar#LayoutTabs::tab:selected {
    background: #3f3f3f;
    color: #ffffff;
}

/* ----- Command line palette ----- */
QPlainTextEdit#CommandScrollback {
    background: #1e1e1e;
    border: 1px solid #1f1f1f;
    color: #cfcfcf;
}
QLineEdit#CommandInput {
    background: #1e1e1e;
    border: 1px solid #4a4a4a;
    padding: 3px;
    color: #ffffff;
}
QLabel#CommandPrompt { color: #9acd6a; }
QListWidget#CommandSuggest {
    background: #1b1b1b;
    border: 1px solid #4a90d9;
    color: #e0e0e0;
}
QListWidget#CommandSuggest::item:selected {
    background: #4a90d9;
    color: #ffffff;
}

/* ----- Dynamic Input: on-geometry field tooltips ----- */
QFrame#DynFieldTip {
    background: #1e1e1e;
    border: 1px solid #4a90d9;
    border-radius: 2px;
}
QLabel#DynFieldLabel { color: #9a9a9a; }
QLineEdit#DynFieldValue {
    background: #161616;
    border: 1px solid #3a3a3a;
    padding: 1px 3px;
    color: #ffffff;
}
QLineEdit#DynFieldValue:focus { border: 1px solid #7cc0ff; }

/* ----- Status bar ----- */
QStatusBar { background: #232323; }
QStatusBar QToolButton {
    background: transparent;
    border: 1px solid transparent;
    padding: 2px 8px;
    color: #b0b0b0;
}
QStatusBar QToolButton:hover { background: #3a3a3a; border-radius: 3px; }
QStatusBar QToolButton:checked {
    background: #2f4a66;
    color: #7cc0ff;
    border: 1px solid #4a90d9;
    border-radius: 3px;
}
QStatusBar QLabel#CoordReadout {
    color: #d6d6d6;
    padding: 0 8px;
}
)QSS");
}

} // namespace musacad::ui
