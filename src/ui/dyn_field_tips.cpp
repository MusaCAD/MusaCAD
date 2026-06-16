// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#include "musacad/ui/dyn_field_tips.hpp"

#include <algorithm>
#include <cmath>

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>

#include "musacad/command/command_processor.hpp"

namespace musacad::ui {

namespace {
QString fmt_value(const command::DynField& f) {
    QString s = QString::number(f.value, 'g', 6);
    if (f.is_angle) {
        s += QStringLiteral("°"); // degree sign
    }
    return s;
}
std::optional<double> parse_typed(const QString& s) {
    if (s.trimmed().isEmpty()) {
        return std::nullopt;
    }
    bool ok = false;
    const double v = s.trimmed().toDouble(&ok);
    return ok ? std::optional<double>(v) : std::nullopt;
}
} // namespace

// ---------------------------------------------------------------------------
// FieldTip
// ---------------------------------------------------------------------------
FieldTip::FieldTip(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("DynFieldTip"));
    // A frameless, always-on-top tool surface floating over the GL viewport. The
    // frame itself does NOT take focus (so it never steals keys from the command
    // line); only its value field does, and only when clicked or Tab-cycled into.
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
    setFocusPolicy(Qt::NoFocus);

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(5, 2, 5, 2);
    row->setSpacing(4);
    label_ = new QLabel(this);
    label_->setObjectName(QStringLiteral("DynFieldLabel"));
    // The label is the drag handle: let mouse presses fall through to the frame so the
    // whole tip (except the editable value field) drags.
    label_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    value_ = new QLineEdit(this);
    value_->setObjectName(QStringLiteral("DynFieldValue"));
    value_->setMinimumWidth(54);
    value_->setFocusPolicy(Qt::ClickFocus);
    row->addWidget(label_);
    row->addWidget(value_);

    value_->installEventFilter(this);
    connect(value_, &QLineEdit::textEdited, this, [this] { Q_EMIT editChanged(slot_); });
}

void FieldTip::set_label(const QString& text) {
    label_->setText(text);
    label_->setVisible(!text.isEmpty());
}

void FieldTip::set_live(const QString& placeholder) {
    value_->setPlaceholderText(placeholder);
}

void FieldTip::set_display_only(bool on) {
    value_->setVisible(!on);
}

QString FieldTip::typed() const {
    return value_->text().trimmed();
}

void FieldTip::clear_typed() {
    value_->clear();
}

void FieldTip::focus_value() {
    if (!value_->isVisible()) {
        return;
    }
    // A frameless top-level needs activation before it can hold keyboard focus, so
    // typing flows into the field without a click (AutoCAD's dynamic-input behaviour).
    raise();
    activateWindow();
    value_->setFocus(Qt::OtherFocusReason);
    value_->selectAll();
}

bool FieldTip::is_typing() const {
    return value_->isVisible() && value_->hasFocus() && !value_->text().isEmpty();
}

bool FieldTip::value_has_focus() const {
    return value_->isVisible() && value_->hasFocus();
}

bool FieldTip::deliver_key(int key, Qt::KeyboardModifiers mods) {
    // Route a synthetic key through the value field's installed event filter, the
    // same path a real keystroke takes -- so tests exercise the real Tab/Esc routing.
    QKeyEvent ev(QEvent::KeyPress, key, mods);
    return QCoreApplication::sendEvent(value_, &ev);
}

void FieldTip::test_set_value(const QString& text) {
    value_->setText(text);
    Q_EMIT editChanged(slot_);
}

bool FieldTip::test_drag(int dx, int dy) {
    // Synthesize press-move-release on the frame (the drag handle), as a real drag does.
    const QPoint start = pos();
    const QPointF local(5, 5); // on the frame, not the value field
    const QPoint g0 = mapToGlobal(local.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress, local, QPointF(g0), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(this, &press);
    const QPointF lmove(5 + dx, 5 + dy);
    QMouseEvent move(QEvent::MouseMove, lmove, QPointF(g0 + QPoint(dx, dy)), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(this, &move);
    QMouseEvent rel(QEvent::MouseButtonRelease, lmove, QPointF(g0 + QPoint(dx, dy)), Qt::LeftButton,
                    Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(this, &rel);
    const QPoint delta = pos() - start;
    return std::abs(delta.x() - dx) <= 3 && std::abs(delta.y() - dy) <= 3;
}

void FieldTip::set_anchor_global(const QPoint& anchor_global) {
    anchor_ = anchor_global;
    if (dragging_) {
        return;
    }
    QPoint p = anchor_ + offset_;
    if (bounds_.isValid()) {
        // Keep the whole tip inside the viewport rect: never the ribbon, never off the
        // geometry's monitor (the multi-monitor failure the screenshots showed).
        const int w = width() > 0 ? width() : 80;
        const int h = height() > 0 ? height() : 22;
        const int maxx = std::max(bounds_.left(), bounds_.right() - w);
        const int maxy = std::max(bounds_.top(), bounds_.bottom() - h);
        p.setX(std::clamp(p.x(), bounds_.left(), maxx));
        p.setY(std::clamp(p.y(), bounds_.top(), maxy));
    }
    move(p);
}

void FieldTip::mousePressEvent(QMouseEvent* event) {
    // Drag from anywhere on the frame (the value field handles its own clicks).
    if (event->button() == Qt::LeftButton) {
        dragging_ = true;
        drag_press_global_ = event->globalPosition().toPoint();
        drag_origin_ = pos();
        event->accept();
        return;
    }
    QFrame::mousePressEvent(event);
}

void FieldTip::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_) {
        const QPoint now = event->globalPosition().toPoint();
        const QPoint p = drag_origin_ + (now - drag_press_global_);
        move(p);
        offset_ = p - anchor_; // remember the displacement for the rest of the command
        event->accept();
        return;
    }
    QFrame::mouseMoveEvent(event);
}

void FieldTip::mouseReleaseEvent(QMouseEvent* event) {
    if (dragging_ && event->button() == Qt::LeftButton) {
        dragging_ = false;
        event->accept();
        return;
    }
    QFrame::mouseReleaseEvent(event);
}

bool FieldTip::eventFilter(QObject* watched, QEvent* event) {
    if (watched == value_ && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        switch (ke->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            Q_EMIT submitted(slot_);
            return true;
        case Qt::Key_Tab:
            Q_EMIT tabbed(slot_, +1);
            return true;
        case Qt::Key_Backtab:
            Q_EMIT tabbed(slot_, -1);
            return true;
        case Qt::Key_Escape:
            Q_EMIT escaped();
            return true;
        default:
            break;
        }
    }
    return QFrame::eventFilter(watched, event);
}

// ---------------------------------------------------------------------------
// DynFieldTips
// ---------------------------------------------------------------------------
DynFieldTips::DynFieldTips(QWidget* host, QObject* parent) : QObject(parent), host_(host) {
    prompt_tip_ = new FieldTip(host_);
    prompt_tip_->set_display_only(true);
    prompt_tip_->set_slot(-1);
    prompt_tip_->hide();
}

void DynFieldTips::set_prompt(const std::string& prompt) {
    prompt_ = prompt;
    prompt_tip_->set_label(QString::fromStdString(prompt));
    // A new prompt is a new command step: drop typed text, offsets and any lock so
    // the next step's tips re-anchor fresh on the geometry.
    for (FieldTip* t : tips_) {
        t->clear_typed();
        t->reset_offset();
    }
    prompt_tip_->reset_offset();
    if (lock_cb_) {
        lock_cb_(std::nullopt, std::nullopt);
    }
    refresh();
}

bool DynFieldTips::dimensional() const {
    if (processor_ == nullptr) {
        return false;
    }
    using command::PreviewKind;
    const PreviewKind k = processor_->preview().kind;
    return k == PreviewKind::Segment || k == PreviewKind::Circle || k == PreviewKind::Rectangle;
}

FieldTip* DynFieldTips::ensure_tip(int index) {
    while (static_cast<int>(tips_.size()) <= index) {
        auto* t = new FieldTip(host_);
        const int slot = static_cast<int>(tips_.size());
        t->set_slot(slot);
        connect(t, &FieldTip::submitted, this, [this](int s) { submit_from(s); });
        connect(t, &FieldTip::tabbed, this, [this](int s, int d) { cycle_focus(s, d); });
        connect(t, &FieldTip::escaped, this, [this] {
            if (escape_cb_) {
                escape_cb_();
            }
        });
        connect(t, &FieldTip::editChanged, this, [this](int) { update_lock(); });
        tips_.push_back(t);
    }
    return tips_[static_cast<std::size_t>(index)];
}

void DynFieldTips::set_enabled(bool on) {
    enabled_ = on;
    if (!on) {
        hide_all();
    } else {
        refresh();
    }
}

void DynFieldTips::on_constrained_cursor(double cx, double cy) {
    cursor_ = {cx, cy};
    refresh();
}

void DynFieldTips::refresh() {
    if (!enabled_ || processor_ == nullptr) {
        for (FieldTip* t : tips_) {
            t->hide();
        }
        prompt_tip_->hide();
        return;
    }

    // The free-floating prompt shows whenever a command is running.
    const bool active = processor_->has_active_command();
    prompt_tip_->setVisible(active && !prompt_.empty());

    // Anchor the tips on the geometry AS DRAWN: a typed value locks that DOF, so the
    // rubber-band (and thus the tip's anchor + shown value) reflects it while the
    // cursor still drives the unlocked DOF. Read the locks from the prior frame's tips.
    std::optional<double> lp;
    std::optional<double> ls;
    for (FieldTip* t : tips_) {
        if (!t->isVisible()) {
            continue;
        }
        if (t->slot() == 0) {
            lp = parse_typed(t->typed());
        } else if (t->slot() == 1) {
            ls = parse_typed(t->typed());
        }
    }
    const core::Vec2 eff =
        command::apply_dyn_lock(processor_->preview(), cursor_, lp, ls);
    fields_ = dimensional() ? command::dyn_fields(processor_->preview(), eff)
                            : std::vector<command::DynField>{};
    for (std::size_t i = 0; i < fields_.size(); ++i) {
        FieldTip* t = ensure_tip(static_cast<int>(i)); // grow the pool if needed
        t->set_label(QString::fromStdString(fields_[i].label));
        t->set_live(fmt_value(fields_[i]));
        t->show();
    }
    for (std::size_t i = fields_.size(); i < tips_.size(); ++i) {
        tips_[i]->hide(); // surplus pooled tips
    }
    reposition();
}

void DynFieldTips::reposition() {
    if (!project_) {
        return;
    }
    const QRect bounds = bounds_fn_ ? bounds_fn_() : QRect();
    for (std::size_t i = 0; i < fields_.size() && i < tips_.size(); ++i) {
        tips_[i]->set_bounds(bounds);
        tips_[i]->set_anchor_global(project_(fields_[i].anchor));
    }
    if (prompt_tip_->isVisible()) {
        // Float the prompt a little above-left of the cursor (its own draggable offset).
        prompt_tip_->set_bounds(bounds);
        prompt_tip_->set_anchor_global(project_(cursor_) + QPoint(14, -28));
    }
}

void DynFieldTips::update_lock() {
    if (!lock_cb_) {
        return;
    }
    std::optional<double> primary;
    std::optional<double> secondary;
    for (FieldTip* t : tips_) {
        if (!t->isVisible()) {
            continue;
        }
        if (t->slot() == 0) {
            primary = parse_typed(t->typed());
        } else if (t->slot() == 1) {
            secondary = parse_typed(t->typed());
        }
    }
    lock_cb_(primary, secondary);
}

void DynFieldTips::submit_from(int /*slot*/) {
    if (processor_ == nullptr) {
        return;
    }
    std::optional<double> primary;
    std::optional<double> secondary;
    for (FieldTip* t : tips_) {
        if (!t->isVisible()) {
            continue;
        }
        if (t->slot() == 0) {
            primary = parse_typed(t->typed());
        } else if (t->slot() == 1) {
            secondary = parse_typed(t->typed());
        }
    }
    const std::string line =
        command::compose_dyn_submit(processor_->preview(), cursor_, primary, secondary);
    if (line.empty()) {
        return;
    }
    // The step advances: clear typed text + lock; the next prompt re-anchors the tips.
    for (FieldTip* t : tips_) {
        t->clear_typed();
    }
    if (lock_cb_) {
        lock_cb_(std::nullopt, std::nullopt);
    }
    processor_->submit_line(line);
}

void DynFieldTips::cycle_focus(int slot, int dir) {
    int n = 0;
    for (FieldTip* t : tips_) {
        if (t->isVisible()) {
            ++n;
        }
    }
    if (n <= 1) {
        return;
    }
    const int next = ((slot + dir) % n + n) % n;
    if (next >= 0 && next < static_cast<int>(tips_.size())) {
        tips_[static_cast<std::size_t>(next)]->focus_value();
    }
}

void DynFieldTips::focus_first() {
    for (FieldTip* t : tips_) {
        if (t->isVisible()) {
            t->focus_value();
            return;
        }
    }
}

bool DynFieldTips::is_typing() const {
    for (FieldTip* t : tips_) {
        if (t->is_typing()) {
            return true;
        }
    }
    return false;
}

bool DynFieldTips::any_field_focused() const {
    for (FieldTip* t : tips_) {
        if (t->value_has_focus()) {
            return true;
        }
    }
    return false;
}

int DynFieldTips::visible_field_count() const {
    int n = 0;
    for (FieldTip* t : tips_) {
        if (t->isVisible()) {
            ++n;
        }
    }
    return n;
}

bool DynFieldTips::all_tips_on_anchor(int tol_px) const {
    if (!project_ || fields_.empty()) {
        return false;
    }
    for (std::size_t i = 0; i < fields_.size() && i < tips_.size(); ++i) {
        if (!tips_[i]->isVisible()) {
            return false;
        }
        const QPoint expected = project_(fields_[i].anchor);
        const QPoint actual = tips_[i]->pos();
        if (std::abs(actual.x() - expected.x()) > tol_px ||
            std::abs(actual.y() - expected.y()) > tol_px) {
            return false;
        }
    }
    return true;
}

std::string DynFieldTips::debug_state() const {
    QString s;
    for (std::size_t i = 0; i < fields_.size() && i < tips_.size(); ++i) {
        const QPoint exp = project_ ? project_(fields_[i].anchor) : QPoint(-1, -1);
        const QPoint act = tips_[i]->pos();
        s += QStringLiteral("  tip[%1] %2 anchorW=(%3,%4) expectG=(%5,%6) actualG=(%7,%8) vis=%9\n")
                 .arg(i)
                 .arg(QString::fromStdString(fields_[i].label))
                 .arg(fields_[i].anchor.x, 0, 'f', 1)
                 .arg(fields_[i].anchor.y, 0, 'f', 1)
                 .arg(exp.x())
                 .arg(exp.y())
                 .arg(act.x())
                 .arg(act.y())
                 .arg(tips_[i]->isVisible() ? 1 : 0);
    }
    return s.toStdString();
}

std::string DynFieldTips::debug_winids() const {
    QString s;
    for (FieldTip* t : tips_) {
        if (!t->isVisible()) {
            continue;
        }
        const QRect g = t->frameGeometry();
        s += QStringLiteral("0x%1 %2 %3 %4 %5\n")
                 .arg(static_cast<qulonglong>(t->winId()), 0, 16)
                 .arg(g.x())
                 .arg(g.y())
                 .arg(g.width())
                 .arg(g.height());
    }
    if (prompt_tip_->isVisible()) {
        const QRect g = prompt_tip_->frameGeometry();
        s += QStringLiteral("0x%1 %2 %3 %4 %5\n")
                 .arg(static_cast<qulonglong>(prompt_tip_->winId()), 0, 16)
                 .arg(g.x())
                 .arg(g.y())
                 .arg(g.width())
                 .arg(g.height());
    }
    return s.toStdString();
}

bool DynFieldTips::tip_field_focused(int slot) const {
    return slot >= 0 && slot < static_cast<int>(tips_.size()) && tips_[slot]->value_has_focus();
}

bool DynFieldTips::focus_slot(int slot) {
    if (slot < 0 || slot >= static_cast<int>(tips_.size()) || !tips_[slot]->isVisible()) {
        return false;
    }
    tips_[slot]->focus_value();
    return true;
}

bool DynFieldTips::press_tab_on(int slot, int dir) {
    if (slot < 0 || slot >= static_cast<int>(tips_.size())) {
        return false;
    }
    return tips_[slot]->deliver_key(dir >= 0 ? Qt::Key_Tab : Qt::Key_Backtab, Qt::NoModifier);
}

bool DynFieldTips::set_field_text(int slot, const std::string& text) {
    if (slot < 0 || slot >= static_cast<int>(tips_.size())) {
        return false;
    }
    tips_[slot]->test_set_value(QString::fromStdString(text));
    return true;
}

bool DynFieldTips::submit_slot(int slot) {
    if (slot < 0 || slot >= static_cast<int>(tips_.size())) {
        return false;
    }
    return tips_[slot]->deliver_key(Qt::Key_Return, Qt::NoModifier);
}

bool DynFieldTips::drag_slot(int slot, int dx, int dy) {
    if (slot < 0 || slot >= static_cast<int>(tips_.size())) {
        return false;
    }
    return tips_[slot]->test_drag(dx, dy);
}

void DynFieldTips::hide_all() {
    for (FieldTip* t : tips_) {
        t->clear_typed();
        t->reset_offset();
        t->hide();
    }
    prompt_tip_->reset_offset();
    prompt_tip_->hide();
    fields_.clear();
    if (lock_cb_) {
        lock_cb_(std::nullopt, std::nullopt);
    }
}

} // namespace musacad::ui
