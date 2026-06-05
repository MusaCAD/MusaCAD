#include "musacad/ui/theme.hpp"

namespace musacad::ui {

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
QToolButton#AppButton {
    background: #cc3333;
    color: white;
    font-weight: bold;
    border: none;
    padding: 4px 10px;
}
QToolButton#AppButton:hover { background: #dd4444; }
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
