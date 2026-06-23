// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <vector>

#include <QFrame>
#include <QWidget>

class QAction;
class QHBoxLayout;
class QIcon;
class QMenu;
class QPushButton;
class QStackedWidget;
class QTabBar;
class QToolButton;

namespace musacad::ui {

/// A labelled group of icon+label buttons inside a ribbon tab (e.g. "Draw").
class RibbonPanel : public QFrame {
    Q_OBJECT

public:
    explicit RibbonPanel(const QString& title, QWidget* parent = nullptr);

    /// Adds an enabled icon+label button. The caller connects its clicked()
    /// signal to an existing command/action.
    QToolButton* add_button(const QIcon& icon, const QString& label);

    /// Adds a disabled icon+label button for a not-yet-implemented feature.
    QToolButton* add_placeholder(const QIcon& icon, const QString& label);

    /// Adds a button with a dropdown `menu` of related commands (AutoCAD-style). When
    /// `split` is true it's a split button -- a main click runs the primary action (the
    /// caller wires clicked()) and the arrow opens the menu; otherwise the whole button
    /// opens the menu. `menu` is reparented to the button.
    QToolButton* add_dropdown(const QIcon& icon, const QString& label, QMenu* menu, bool split);

    /// Adds an arbitrary widget (e.g. the current-layer combo) to the panel.
    void add_widget(QWidget* widget);

private:
    QToolButton* make_button(const QIcon& icon, const QString& label, bool enabled);
    QHBoxLayout* content_;
};

/// AutoCAD-style ribbon: a Quick Access Toolbar strip on top, a row of tabs, and
/// a stacked set of pages -- each page a horizontal row of RibbonPanels.
class RibbonBar : public QWidget {
    Q_OBJECT

public:
    explicit RibbonBar(QWidget* parent = nullptr);

    QPushButton* app_button() const noexcept { return app_button_; }
    void add_qat_action(QAction* action);

    /// Adds a tab and returns its index.
    int add_tab(const QString& title);
    /// Adds a panel to the given tab and returns it.
    RibbonPanel* add_panel(int tab_index, const QString& title);

private:
    QPushButton* app_button_ = nullptr;
    QHBoxLayout* qat_layout_ = nullptr;
    QTabBar* tabs_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    std::vector<QHBoxLayout*> page_layouts_;
};

} // namespace musacad::ui
