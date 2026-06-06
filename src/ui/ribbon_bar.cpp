#include "musacad/ui/ribbon_bar.hpp"

#include <QAction>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace musacad::ui {

// ---------------------------------------------------------------------------
// RibbonPanel
// ---------------------------------------------------------------------------
RibbonPanel::RibbonPanel(const QString& title, QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("RibbonPanel"));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 2);
    outer->setSpacing(2);

    auto* content_widget = new QWidget(this);
    content_ = new QHBoxLayout(content_widget);
    content_->setContentsMargins(0, 0, 0, 0);
    content_->setSpacing(2);
    outer->addWidget(content_widget, 1);

    auto* title_label = new QLabel(title, this);
    title_label->setObjectName(QStringLiteral("RibbonPanelTitle"));
    outer->addWidget(title_label);
}

QToolButton* RibbonPanel::make_button(const QIcon& icon, const QString& label, bool enabled) {
    auto* btn = new QToolButton(this);
    btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    btn->setIcon(icon);
    btn->setIconSize(QSize(24, 24));
    btn->setText(label);
    btn->setAutoRaise(true);
    btn->setEnabled(enabled);
    btn->setMinimumWidth(52);
    if (!enabled) {
        btn->setToolTip(label + QStringLiteral(" (coming soon)"));
    }
    content_->addWidget(btn);
    return btn;
}

QToolButton* RibbonPanel::add_button(const QIcon& icon, const QString& label) {
    return make_button(icon, label, true);
}

QToolButton* RibbonPanel::add_placeholder(const QIcon& icon, const QString& label) {
    return make_button(icon, label, false);
}

void RibbonPanel::add_widget(QWidget* widget) { content_->addWidget(widget); }

// ---------------------------------------------------------------------------
// RibbonBar
// ---------------------------------------------------------------------------
RibbonBar::RibbonBar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("Ribbon"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Quick Access Toolbar strip.
    auto* qat = new QWidget(this);
    qat->setObjectName(QStringLiteral("QatStrip"));
    qat->setAttribute(Qt::WA_StyledBackground, true);
    qat_layout_ = new QHBoxLayout(qat);
    qat_layout_->setContentsMargins(2, 2, 2, 2);
    qat_layout_->setSpacing(2);

    app_button_ = new QPushButton(QStringLiteral("M"), qat);
    app_button_->setObjectName(QStringLiteral("AppButton"));
    app_button_->setToolTip(QStringLiteral("Application menu"));
    qat_layout_->addWidget(app_button_);
    qat_layout_->addSpacing(8);
    root->addWidget(qat);

    // Tabs + pages.
    tabs_ = new QTabBar(this);
    tabs_->setObjectName(QStringLiteral("RibbonTabs"));
    tabs_->setExpanding(false);
    root->addWidget(tabs_);

    pages_ = new QStackedWidget(this);
    pages_->setObjectName(QStringLiteral("RibbonPages"));
    root->addWidget(pages_);

    connect(tabs_, &QTabBar::currentChanged, pages_, &QStackedWidget::setCurrentIndex);
}

void RibbonBar::add_qat_action(QAction* action) {
    auto* btn = new QToolButton(this);
    btn->setDefaultAction(action);
    btn->setAutoRaise(true);
    // Insert before the trailing stretch (added lazily) -- simplest is to append.
    qat_layout_->addWidget(btn);
}

int RibbonBar::add_tab(const QString& title) {
    auto* page = new QWidget(pages_);
    page->setObjectName(QStringLiteral("RibbonPage"));
    page->setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);
    layout->addStretch(1); // panels are inserted before this stretch (left-aligned)
    page_layouts_.push_back(layout);
    pages_->addWidget(page);
    return tabs_->addTab(title);
}

RibbonPanel* RibbonBar::add_panel(int tab_index, const QString& title) {
    auto* panel = new RibbonPanel(title, this);
    QHBoxLayout* layout = page_layouts_.at(static_cast<std::size_t>(tab_index));
    layout->insertWidget(layout->count() - 1, panel); // before the trailing stretch
    return panel;
}

} // namespace musacad::ui
