// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/ui/ribbon_bar.hpp"

#include <algorithm>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QColor>
#include <QEvent>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace musacad::ui {

namespace {
constexpr int kFullIcon = 24;
constexpr int kCompactIcon = 18;
constexpr int kFullMinW = 52;

/// A QTabBar that paints a coloured accent stripe along the top edge of contextual tabs
/// (keyed by their stable page index). QSS `:selected` overrides setTabTextColor, so the
/// stripe is what keeps the accent visible whether the tab is active or not.
class RibbonTabBar : public QTabBar {
public:
    using QTabBar::QTabBar;
    void set_accent(int page_index, const QColor& c) {
        accent_[page_index] = c;
        update();
    }

protected:
    void paintEvent(QPaintEvent* e) override {
        QTabBar::paintEvent(e);
        QPainter p(this);
        for (int i = 0; i < count(); ++i) {
            const QVariant v = tabData(i);
            if (!v.isValid()) {
                continue;
            }
            const auto it = accent_.constFind(v.toInt());
            if (it == accent_.constEnd()) {
                continue;
            }
            const QRect r = tabRect(i);
            p.fillRect(QRect(r.left() + 1, r.top(), r.width() - 2, 3), it.value());
        }
    }

private:
    QHash<int, QColor> accent_;
};
} // namespace

// ---------------------------------------------------------------------------
// RibbonPanel
// ---------------------------------------------------------------------------
RibbonPanel::RibbonPanel(const QString& title, QWidget* parent) : QFrame(parent), title_(title) {
    setObjectName(QStringLiteral("RibbonPanel"));
    // Never shrink a panel below the natural width of its buttons -- the collapse algorithm
    // (RibbonBar::relayout_page) changes the panel STATE to free space instead, and only
    // when even all-collapsed doesn't fit does the page's scroll bar engage.
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);

    outer_ = new QVBoxLayout(this);
    outer_->setContentsMargins(4, 4, 4, 2);
    outer_->setSpacing(2);

    content_widget_ = new QWidget(this);
    content_ = new QHBoxLayout(content_widget_);
    content_->setContentsMargins(2, 2, 2, 2); // breathing room (counted in sizeHint, unlike QSS padding)
    content_->setSpacing(5);
    outer_->addWidget(content_widget_, 1);

    title_label_ = new QLabel(title, this);
    title_label_->setObjectName(QStringLiteral("RibbonPanelTitle"));
    title_label_->setAlignment(Qt::AlignHCenter);
    outer_->addWidget(title_label_);
}

RibbonPanel::~RibbonPanel() {
    // The popout is parented to the top-level window (so it can overlay below the tab strip),
    // and the click-outside dismiss installs an app-wide event filter -- neither is owned by
    // this widget's parent chain, so release both here to avoid a leak / use-after-free.
    if (filter_on_) {
        qApp->removeEventFilter(this);
        filter_on_ = false;
    }
    delete popout_;
    popout_ = nullptr;
}

void RibbonPanel::set_representative_icon(const QIcon& icon) { repr_icon_ = icon; }

QToolButton* RibbonPanel::make_button(const QIcon& icon, const QString& label, bool enabled,
                                      RibbonTier tier) {
    auto* btn = new QToolButton(content_widget_);
    btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    btn->setIcon(icon);
    btn->setIconSize(QSize(kFullIcon, kFullIcon));
    btn->setText(label);
    btn->setAutoRaise(true);
    btn->setEnabled(enabled);
    btn->setMinimumWidth(kFullMinW);
    if (!enabled) {
        btn->setToolTip(label + QStringLiteral(" (coming soon)"));
    }
    content_->addWidget(btn);
    buttons_.push_back({btn, tier});
    return btn;
}

QToolButton* RibbonPanel::add_button(const QIcon& icon, const QString& label, RibbonTier tier) {
    return make_button(icon, label, true, tier);
}

QToolButton* RibbonPanel::add_placeholder(const QIcon& icon, const QString& label,
                                          RibbonTier tier) {
    return make_button(icon, label, false, tier);
}

QToolButton* RibbonPanel::add_dropdown(const QIcon& icon, const QString& label, QMenu* menu,
                                       bool split, RibbonTier tier) {
    QToolButton* btn = make_button(icon, label, true, tier);
    btn->setMenu(menu); // menu keeps its own parent (see header)
    btn->setPopupMode(split ? QToolButton::MenuButtonPopup : QToolButton::InstantPopup);
    return btn;
}

void RibbonPanel::add_widget(QWidget* widget) { content_->addWidget(widget); }

void RibbonPanel::style_buttons(bool compact) {
    for (const Btn& b : buttons_) {
        const bool small = compact && b.tier == RibbonTier::Secondary;
        b.btn->setToolButtonStyle(small ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextUnderIcon);
        b.btn->setIconSize(QSize(small ? kCompactIcon : kFullIcon, small ? kCompactIcon : kFullIcon));
        b.btn->setMinimumWidth(small ? 0 : kFullMinW);
    }
}

void RibbonPanel::set_state(PanelState s) {
    if (s == state_ && s != PanelState::Full) {
        return; // Full is idempotent-safe to re-apply (re-styles after un-collapse)
    }
    // Leaving Collapsed: hide the popout and reclaim the content widget back inline.
    if (state_ == PanelState::Collapsed && s != PanelState::Collapsed) {
        if (popout_ != nullptr) {
            popout_->hide();
            popout_->layout()->removeWidget(content_widget_); // drop the stale layout item
        }
        content_widget_->setParent(this);
        outer_->insertWidget(0, content_widget_, 1);
        content_widget_->show();
        title_label_->show();
        if (flyout_btn_ != nullptr) {
            flyout_btn_->hide();
        }
    }

    state_ = s;

    if (s == PanelState::Collapsed) {
        title_label_->hide();
        if (flyout_btn_ == nullptr) {
            flyout_btn_ = new QToolButton(this);
            flyout_btn_->setObjectName(QStringLiteral("RibbonFlyout"));
            flyout_btn_->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            flyout_btn_->setIconSize(QSize(kFullIcon, kFullIcon));
            flyout_btn_->setAutoRaise(true);
            flyout_btn_->setMinimumWidth(kFullMinW);
            connect(flyout_btn_, &QToolButton::clicked, this, [this] { toggle_popout(); });
            outer_->insertWidget(0, flyout_btn_);
        }
        flyout_btn_->setIcon(repr_icon_);
        flyout_btn_->setText(title_);
        flyout_btn_->setToolTip(title_);
        // The popout: a child frame of the top-level window so it overlays inline below the
        // tab strip (and is captured by a per-window screenshot, unlike a Qt::Popup menu).
        if (popout_ == nullptr) {
            popout_ = new QFrame(window());
            popout_->setObjectName(QStringLiteral("RibbonPopout"));
            popout_->setFrameShape(QFrame::StyledPanel);
            auto* pl = new QHBoxLayout(popout_);
            pl->setContentsMargins(6, 6, 6, 6);
            pl->setSpacing(2);
            popout_->hide();
        }
        style_buttons(false); // the popout shows full-size buttons
        content_widget_->setParent(popout_);
        popout_->layout()->addWidget(content_widget_);
        content_widget_->show();
        // Clicking any button inside the popout dismisses it (AutoCAD behaviour).
        for (const Btn& b : buttons_) {
            connect(b.btn, &QToolButton::clicked, popout_, &QFrame::hide, Qt::UniqueConnection);
        }
        flyout_btn_->show();
    } else {
        style_buttons(s == PanelState::Compact);
    }
    updateGeometry();
    // Make this panel's sizeHint accurate IN-LINE, so the (synchronous) collapse fitter can trust
    // a measurement taken on the SAME tick -- see force_settle().
    force_settle();
}

// Force this panel's sizeHint to reflect its CURRENT state synchronously, without waiting for the
// deferred QEvent::Polish. After a Collapsed->Full transition the content widget is reparented back
// (RibbonPopout -> RibbonPanel), which queues a StyleChange/Polish that, until processed, leaves the
// QToolButtons' cached sizeHints ~170px short -- the stale read that made the fitter under-collapse
// (clipped titles + a spurious scroll bar) on the first maximize. The "triple" below flushes it:
//   ensurePolished() forces synchronous QStyleSheetStyle re-resolution (the new ancestor selector),
//   updateGeometry() drops the cached sizeHint that the posted StyleChange invalidated (ensurePolished
//   alone does NOT), and content_->activate() recomputes the panel sizeHint from the now-polished
//   metrics. Enumerated through the panel's OWN widgets (never page->findChildren), so a panel whose
//   content is still parented to the window-level popout_ is settled correctly all the same.
void RibbonPanel::force_settle() {
    content_widget_->ensurePolished();
    for (const Btn& b : buttons_) {
        b.btn->ensurePolished();
        b.btn->updateGeometry();
    }
    if (flyout_btn_ != nullptr) {
        flyout_btn_->ensurePolished();
        flyout_btn_->updateGeometry();
    }
    title_label_->ensurePolished();
    content_widget_->updateGeometry();
    if (content_ != nullptr) {
        content_->invalidate();
        content_->activate();
    }
}

void RibbonPanel::toggle_popout() {
    if (popout_ == nullptr || flyout_btn_ == nullptr) {
        return;
    }
    if (popout_->isVisible()) {
        popout_->hide();
        return;
    }
    // Position the popout just below the fly-out button, in the window's coordinates.
    popout_->adjustSize();
    const QPoint below = flyout_btn_->mapTo(window(), QPoint(0, flyout_btn_->height() + 1));
    int x = below.x();
    if (popout_->parentWidget() != nullptr) { // keep it on screen
        x = std::min(x, popout_->parentWidget()->width() - popout_->width() - 2);
        x = std::max(x, 2);
    }
    popout_->move(x, below.y());
    popout_->show();
    popout_->raise();
    if (!filter_on_) { // install exactly once -- Qt does not de-dup event filters
        qApp->installEventFilter(this); // dismiss on a click outside
        filter_on_ = true;
    }
}

bool RibbonPanel::eventFilter(QObject* watched, QEvent* event) {
    if (popout_ != nullptr && popout_->isVisible() && event->type() == QEvent::MouseButtonPress) {
        const auto* me = static_cast<QMouseEvent*>(event);
        const QPoint gp = me->globalPosition().toPoint();
        const bool in_popout = popout_->geometry().contains(popout_->parentWidget() != nullptr
                                                                ? popout_->parentWidget()->mapFromGlobal(gp)
                                                                : gp);
        const bool on_flyout = flyout_btn_ != nullptr &&
                               flyout_btn_->rect().contains(flyout_btn_->mapFromGlobal(gp));
        if (!in_popout && !on_flyout) {
            popout_->hide();
        }
    }
    if (event->type() == QEvent::Hide && watched == popout_ && filter_on_) {
        qApp->removeEventFilter(this);
        filter_on_ = false;
    }
    return QFrame::eventFilter(watched, event);
}

// ---------------------------------------------------------------------------
// RibbonBar
// ---------------------------------------------------------------------------
RibbonBar::RibbonBar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("Ribbon"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* qat = new QWidget(this);
    qat->setObjectName(QStringLiteral("QatStrip"));
    qat->setAttribute(Qt::WA_StyledBackground, true);
    qat_layout_ = new QHBoxLayout(qat);
    qat_layout_->setContentsMargins(2, 2, 2, 2);
    qat_layout_->setSpacing(2);

    app_button_ = new QPushButton(QStringLiteral("Musa CAD"), qat);
    app_button_->setObjectName(QStringLiteral("AppButton"));
    app_button_->setToolTip(QStringLiteral("Application menu"));
    qat_layout_->addWidget(app_button_);
    qat_layout_->addSpacing(8);
    root->addWidget(qat);

    tabs_ = new RibbonTabBar(this);
    tabs_->setObjectName(QStringLiteral("RibbonTabs"));
    tabs_->setExpanding(false);
    tabs_->setDrawBase(false); // no full-width base line under the tab row
    root->addWidget(tabs_);

    pages_ = new QStackedWidget(this);
    pages_->setObjectName(QStringLiteral("RibbonPages"));
    // Reserve enough height for a big button (icon + label) plus the panel title beneath it,
    // with breathing room, so the titles are never clipped and the ribbon doesn't crowd the
    // document tab strip below. (Collapse keeps the row within width, so the horizontal scroll
    // bar -- which would otherwise eat into this height -- only appears when fully collapsed.)
    pages_->setMinimumHeight(86);
    root->addWidget(pages_);

    // Tab index is decoupled from page (stack) index via tabData -- contextual tabs can
    // appear/disappear without renumbering the stable pages behind them.
    connect(tabs_, &QTabBar::currentChanged, this, [this](int tab) {
        if (tab < 0) {
            return;
        }
        const int page = page_of_tab(tab);
        if (page >= 0) {
            pages_->setCurrentIndex(page);
            if (!is_contextual_page(page)) {
                last_fixed_page_ = page;
            }
            schedule_relayout();
        }
    });
}

void RibbonBar::add_qat_action(QAction* action) {
    auto* btn = new QToolButton(this);
    btn->setDefaultAction(action);
    btn->setAutoRaise(true);
    qat_layout_->addWidget(btn);
}

int RibbonBar::create_page() {
    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("RibbonPage"));
    page->setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);
    layout->addStretch(1); // panels inserted before this stretch (left-aligned)

    auto* scroll = new QScrollArea(pages_);
    scroll->setObjectName(QStringLiteral("RibbonScroll"));
    scroll->setWidget(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    const int page_index = pages_->addWidget(scroll);
    page_layouts_.push_back(layout);
    page_scrolls_.push_back(scroll);
    return page_index;
}

int RibbonBar::add_tab(const QString& title) {
    const int page = create_page();
    const int tab = tabs_->addTab(title);
    tabs_->setTabData(tab, page);
    return page;
}

int RibbonBar::add_contextual_tab(const QString& title, const QColor& accent,
                                  std::function<bool(const RibbonSel&)> predicate) {
    const int page = create_page();
    contextual_.push_back(Contextual{page, title, accent, std::move(predicate), false});
    return page;
}

RibbonPanel* RibbonBar::add_panel(int page_index, const QString& title, int priority) {
    auto* panel = new RibbonPanel(title, this);
    panel->set_priority(priority);
    QHBoxLayout* layout = page_layouts_.at(static_cast<std::size_t>(page_index));
    layout->insertWidget(layout->count() - 1, panel); // before the trailing stretch
    return panel;
}

int RibbonBar::page_of_tab(int tab_index) const {
    if (tab_index < 0 || tab_index >= tabs_->count()) {
        return -1;
    }
    const QVariant v = tabs_->tabData(tab_index);
    return v.isValid() ? v.toInt() : -1;
}

int RibbonBar::tab_for_page(int page_index) const {
    for (int t = 0; t < tabs_->count(); ++t) {
        if (page_of_tab(t) == page_index) {
            return t;
        }
    }
    return -1;
}

bool RibbonBar::is_contextual_page(int page_index) const {
    return std::any_of(contextual_.begin(), contextual_.end(),
                       [&](const Contextual& c) { return c.page_index == page_index; });
}

std::vector<RibbonPanel*> RibbonBar::panels_of(int page_index) const {
    std::vector<RibbonPanel*> out;
    if (page_index < 0 || page_index >= static_cast<int>(page_layouts_.size())) {
        return out;
    }
    QHBoxLayout* layout = page_layouts_[static_cast<std::size_t>(page_index)];
    for (int i = 0; i < layout->count(); ++i) {
        if (QLayoutItem* it = layout->itemAt(i); it != nullptr && it->widget() != nullptr) {
            if (auto* p = qobject_cast<RibbonPanel*>(it->widget())) {
                out.push_back(p);
            }
        }
    }
    return out;
}

void RibbonBar::relayout_page(int page_index) {
    std::vector<RibbonPanel*> panels = panels_of(page_index);
    if (panels.empty() || page_index >= static_cast<int>(page_scrolls_.size())) {
        return;
    }
    QScrollArea* scroll = page_scrolls_[static_cast<std::size_t>(page_index)];
    QWidget* page = scroll->widget();
    if (page == nullptr || pages_->width() <= 1) {
        return; // not laid out yet -- a later resize/show will redo it
    }
    if (in_fit_) {
        return; // re-entrancy: a resize triggered by our own QLayout::activate() must not re-enter
    }
    // ONE synchronous, race-free pass. We reset every panel to Full, then collapse the minimal set
    // (lowest priority first: Full->Compact across panels, then Compact->Collapsed), re-measuring
    // after each step. Correctness hinges on set_state()/force_settle() making each panel's sizeHint
    // accurate IN-LINE -- so unlike the old deferred fitter, the decision does NOT race the deferred
    // QEvent::Polish that follows a Collapsed->Full reparent (the race that clipped the titles + grew
    // a scroll bar on the FIRST maximize, and that a tab switch happened to paper over).
    ++fit_generation_;
    in_fit_ = true;
    page->setUpdatesEnabled(false);
    // RAII: guarantee updates are re-enabled and the latch cleared on EVERY exit path (the avail<=1
    // guard, the all-collapsed fallback, normal convergence) -- the ribbon can never be left frozen.
    struct FitGuard {
        QWidget* page;
        bool* latch;
        ~FitGuard() {
            page->setUpdatesEnabled(true);
            *latch = false;
        }
    } guard{page, &in_fit_};

    for (RibbonPanel* p : panels) {
        p->set_state(PanelState::Full); // set_state force-settles each panel in-line
    }
    if (QLayout* lay = page->layout(); lay != nullptr) {
        lay->invalidate();
        lay->activate(); // aggregate the now-settled panel sizeHints into the page sizeHint
    }

    const int avail = scroll->viewport()->width();
    if (avail <= 1) {
        return; // geometry not ready; a later resize/show reruns relayout (guard re-enables)
    }

    std::vector<RibbonPanel*> order(panels.begin(), panels.end());
    std::stable_sort(order.begin(), order.end(),
                     [](RibbonPanel* a, RibbonPanel* b) { return a->priority() < b->priority(); });
    while (page->sizeHint().width() > avail) {
        // Pick the next victim: lowest-priority panel still Full (-> Compact); once none remain Full,
        // lowest-priority panel still Compact (-> Collapsed).
        RibbonPanel* victim = nullptr;
        PanelState to = PanelState::Compact;
        for (RibbonPanel* p : order) {
            if (p->state() == PanelState::Full) {
                victim = p;
                break;
            }
        }
        if (victim == nullptr) {
            to = PanelState::Collapsed;
            for (RibbonPanel* p : order) {
                if (p->state() == PanelState::Compact) {
                    victim = p;
                    break;
                }
            }
        }
        if (victim == nullptr) {
            break; // everything is collapsed and it STILL overflows -> the scroll bar is the fallback
        }
        victim->set_state(to); // force-settles the panel in-line
        if (QLayout* lay = page->layout(); lay != nullptr) {
            lay->invalidate();
            lay->activate();
        }
    }

    // Safety net (defensive): if -- on some Qt/style combination -- a panel's sizeHint were STILL
    // short after the in-line force-settle, run exactly one more pass on the next tick. Fires at
    // most once per request (did_recheck_, reset only by schedule_relayout), never when everything
    // is already Collapsed (so the genuine-overflow scroll-bar fallback can't ping-pong), and is
    // gated by fit_generation_ so a newer relayout cancels it. In practice it never fires.
    const bool any_collapsible =
        std::any_of(panels.begin(), panels.end(),
                    [](RibbonPanel* p) { return p->state() != PanelState::Collapsed; });
    if (!did_recheck_ && any_collapsible && page->sizeHint().width() > avail) {
        did_recheck_ = true;
        const int gen = fit_generation_;
        QTimer::singleShot(0, this, [this, page_index, gen] {
            if (gen == fit_generation_) {
                relayout_page(page_index);
            }
        });
    }
}

bool RibbonBar::active_page_fits() const {
    const int page_index = pages_->currentIndex();
    if (page_index < 0 || page_index >= static_cast<int>(page_scrolls_.size())) {
        return true;
    }
    QScrollArea* scroll = page_scrolls_[static_cast<std::size_t>(page_index)];
    QWidget* page = scroll->widget();
    if (page == nullptr) {
        return true;
    }
    const int avail = scroll->viewport()->width();
    if (avail <= 1) {
        return true; // not laid out yet
    }
    if (page->sizeHint().width() <= avail) {
        return true; // fits -- no scroll bar, titles not clipped
    }
    // Overflow is acceptable ONLY when every panel is already fully collapsed: that is the genuine
    // too-narrow case where the horizontal scroll bar is the intended fallback.
    const std::vector<RibbonPanel*> panels = panels_of(page_index);
    return std::all_of(panels.begin(), panels.end(),
                       [](RibbonPanel* p) { return p->state() == PanelState::Collapsed; });
}

void RibbonBar::schedule_relayout() {
    // Coalesce + defer to the event loop so the pass runs AFTER the resize geometry has settled
    // (the scroll viewport width is valid by then). The collapse itself is synchronous within
    // relayout_page; this deferral is only to read a correct available width.
    if (relayout_pending_) {
        return;
    }
    relayout_pending_ = true;
    did_recheck_ = false; // a genuine new request re-arms the one-shot ground-truth re-check
    QTimer::singleShot(0, this, [this] {
        relayout_pending_ = false;
        relayout_page(pages_->currentIndex());
    });
}

void RibbonBar::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    schedule_relayout();
}

void RibbonBar::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    schedule_relayout();
}

void RibbonBar::update_contextual(const RibbonSel& sel) {
    bool changed = false;
    int newly_shown_page = -1;
    // The page the user is on BEFORE we touch the tab bar -- needed because removeTab()
    // makes QTabBar auto-pick an adjacent tab, which we must not mistake for the user's choice.
    const int cur_page_before = page_of_tab(tabs_->currentIndex());
    {
        const QSignalBlocker block(tabs_);
        for (Contextual& c : contextual_) {
            const bool want = c.predicate(sel);
            if (want && !c.shown) {
                const int tab = tabs_->addTab(c.title); // contextual tabs append after fixed
                tabs_->setTabData(tab, c.page_index);
                tabs_->setTabTextColor(tab, c.accent);
                static_cast<RibbonTabBar*>(tabs_)->set_accent(c.page_index, c.accent);
                c.shown = true;
                changed = true;
                newly_shown_page = c.page_index;
            } else if (!want && c.shown) {
                const int tab = tab_for_page(c.page_index);
                if (tab >= 0) {
                    tabs_->removeTab(tab);
                }
                c.shown = false;
                changed = true;
            }
        }
        if (!changed) {
            return;
        }
        // Decide which tab should be current now.
        int target = -1;
        if (newly_shown_page >= 0) {
            target = tab_for_page(newly_shown_page); // auto-activate the new contextual tab
        } else if (int t = tab_for_page(cur_page_before); t >= 0) {
            target = t; // the page we were on is still tabbed -- stay there
        } else {
            // The active (contextual) tab vanished -> return to the last fixed tab (Home).
            target = tab_for_page(last_fixed_page_);
        }
        if (target >= 0) {
            tabs_->setCurrentIndex(target);
            const int page = page_of_tab(target);
            // page should always be valid (every tab carries its page in tabData); fall back
            // to the last fixed page rather than leave pages_ pointing at a stale index.
            pages_->setCurrentIndex(page >= 0 ? page : last_fixed_page_);
        }
    }
    schedule_relayout();
}

} // namespace musacad::ui
