// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <functional>
#include <vector>

#include <QColor>
#include <QFrame>
#include <QWidget>

class QAction;
class QHBoxLayout;
class QIcon;
class QLabel;
class QMenu;
class QPushButton;
class QResizeEvent;
class QShowEvent;
class QScrollArea;
class QStackedWidget;
class QTabBar;
class QToolButton;
class QVBoxLayout;
class QWidgetAction;

namespace musacad::ui {

/// A button's importance within its panel. Drives the COMPACT state: Primary buttons stay
/// large (icon + label); Secondary buttons shrink to icon-only.
enum class RibbonTier { Primary, Secondary };

/// The three progressive display states of a panel (Office-Fluent / AutoCAD pattern).
enum class PanelState {
    Full,      ///< all buttons large (icon + label), title shown
    Compact,   ///< Primary buttons large, Secondary buttons icon-only
    Collapsed, ///< the whole panel folds to one fly-out button (icon = representative)
};

/// Plain-int view of the selection for contextual-tab predicates -- keeps RibbonBar
/// independent of core types (the predicates themselves are built in MainWindow, which
/// has core::family_of). `*_plus1` are EntityKind/EntityFamily + 1 (0 = not homogeneous).
struct RibbonSel {
    int count = 0;
    bool mixed = false;
    int kind_plus1 = 0;
    int family_plus1 = 0;
};

/// A labelled group of icon+label buttons inside a ribbon tab (e.g. "Draw"). Re-renders
/// itself in three states (Full/Compact/Collapsed) on the layout system's command.
class RibbonPanel : public QFrame {
    Q_OBJECT

public:
    explicit RibbonPanel(const QString& title, QWidget* parent = nullptr);
    ~RibbonPanel() override; // clean up the app-wide event filter + the window-parented popout

    /// Adds an enabled icon+label button. The caller connects its clicked() signal.
    QToolButton* add_button(const QIcon& icon, const QString& label,
                            RibbonTier tier = RibbonTier::Primary);

    /// Adds a disabled icon+label button for a not-yet-implemented feature.
    QToolButton* add_placeholder(const QIcon& icon, const QString& label,
                                 RibbonTier tier = RibbonTier::Secondary);

    /// Adds a button with a dropdown `menu` of related commands (AutoCAD-style split or
    /// instant-popup). `menu` keeps its existing parent (do NOT reparent it -- that strips
    /// its Qt::Popup flag and it renders inline).
    QToolButton* add_dropdown(const QIcon& icon, const QString& label, QMenu* menu, bool split,
                              RibbonTier tier = RibbonTier::Secondary);

    /// Adds an arbitrary widget (e.g. the current-layer combo); treated as Primary.
    void add_widget(QWidget* widget);

    void set_priority(int p) { priority_ = p; }
    [[nodiscard]] int priority() const { return priority_; }
    void set_representative_icon(const QIcon& icon);

    void set_state(PanelState s);
    [[nodiscard]] PanelState state() const { return state_; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override; // click-outside dismiss

private:
    QToolButton* make_button(const QIcon& icon, const QString& label, bool enabled,
                             RibbonTier tier);
    void style_buttons(bool compact); // Full/popout = false, Compact = true
    void toggle_popout();             // show/hide the collapsed fly-out panel
    void force_settle();              // make this panel's sizeHint accurate IN-LINE (see .cpp)

    QVBoxLayout* outer_ = nullptr;
    QWidget* content_widget_ = nullptr;
    QHBoxLayout* content_ = nullptr;
    QLabel* title_label_ = nullptr;
    QString title_;
    QIcon repr_icon_;
    int priority_ = 50;
    PanelState state_ = PanelState::Full;

    struct Btn {
        QToolButton* btn;
        RibbonTier tier;
    };
    std::vector<Btn> buttons_;

    // Collapsed: a fly-out button + a child-widget popout that hosts the (reparented)
    // content widget inline below the tab strip (captureable; click-outside dismisses).
    QToolButton* flyout_btn_ = nullptr;
    QFrame* popout_ = nullptr;
    bool filter_on_ = false; // is this panel currently a qApp event filter? (popout open)
};

/// AutoCAD-style ribbon: a Quick Access Toolbar strip on top, a row of tabs, and a stacked
/// set of pages -- each page a horizontal row of RibbonPanels. Panels collapse progressively
/// when the window narrows; contextual tabs appear/disappear with the selection.
class RibbonBar : public QWidget {
    Q_OBJECT

public:
    explicit RibbonBar(QWidget* parent = nullptr);

    QPushButton* app_button() const noexcept { return app_button_; }
    void add_qat_action(QAction* action);

    /// Adds a fixed tab and returns its PAGE index (use it with add_panel).
    int add_tab(const QString& title);

    /// Builds a contextual tab's page (added to the stack but NOT shown until its predicate
    /// matches). `accent` colours the tab label. Returns the page index for add_panel().
    int add_contextual_tab(const QString& title, const QColor& accent,
                           std::function<bool(const RibbonSel&)> predicate);

    /// Adds a panel to the given page (fixed or contextual). `priority`: higher collapses
    /// last. Returns it so the caller fills it with buttons + a representative icon.
    RibbonPanel* add_panel(int page_index, const QString& title, int priority = 50);

    /// Re-evaluate all contextual predicates against `sel`; show/hide their tabs, auto-
    /// activate a newly shown one, and restore the last fixed tab when none match.
    void update_contextual(const RibbonSel& sel);

    /// Test/diagnostic hook: does the active page's (force-settled) panel row fit its viewport --
    /// i.e. no horizontal scroll bar, so panel titles are not clipped? The collapse fitter must
    /// guarantee this except on a genuinely too-narrow window where even all-collapsed overflows.
    [[nodiscard]] bool active_page_fits() const;

protected:
    void resizeEvent(QResizeEvent* e) override;
    void showEvent(QShowEvent* e) override;

private:
    int create_page();                                       // page widget + scroll; -> index
    void schedule_relayout();                                // coalesced, deferred to event loop
    void relayout_page(int page_index);                      // synchronous force-settled collapse
    [[nodiscard]] std::vector<RibbonPanel*> panels_of(int page_index) const;
    [[nodiscard]] int page_of_tab(int tab_index) const;      // via tabData
    [[nodiscard]] int tab_for_page(int page_index) const;    // -1 if not shown
    [[nodiscard]] bool is_contextual_page(int page_index) const;

    QPushButton* app_button_ = nullptr;
    QHBoxLayout* qat_layout_ = nullptr;
    QTabBar* tabs_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    std::vector<QHBoxLayout*> page_layouts_; // indexed by page index
    std::vector<QScrollArea*> page_scrolls_; // indexed by page index

    struct Contextual {
        int page_index = -1;
        QString title;
        QColor accent;
        std::function<bool(const RibbonSel&)> predicate;
        bool shown = false;
    };
    std::vector<Contextual> contextual_;
    int last_fixed_page_ = 0;        // page to restore when contextual tabs vanish
    bool relayout_pending_ = false;  // coalesces deferred relayout requests
    int fit_generation_ = 0;         // bumped per relayout; gates the one-shot ground-truth re-check
    bool in_fit_ = false;            // re-entrancy latch: an activate()-induced resize must not re-enter
    bool did_recheck_ = false;       // one-shot ground-truth re-check fired this request (reset on a new one)
};

} // namespace musacad::ui
