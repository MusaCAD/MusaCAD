#include "musacad/ui/dyn_input.hpp"

#include <cmath>

#include <algorithm>

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

#include "musacad/command/command_processor.hpp"
#include "musacad/command/command_registry.hpp"
#include "musacad/core/math/math.hpp"

namespace musacad::ui {

namespace {
QString fmt(double v) {
    return QString::number(v, 'g', 6);
}
double to_num(const QString& s, bool& ok) {
    return s.trimmed().toDouble(&ok);
}
constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
} // namespace

DynInput::DynInput(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("DynInput"));
    // A frameless, always-on-top tool surface that floats over the GL viewport and
    // can take keyboard focus (so type-at-cursor works) without deactivating picks.
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
    setFocusPolicy(Qt::StrongFocus);

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(4, 2, 4, 2);
    col->setSpacing(1);
    prompt_ = new QLabel(QStringLiteral("Command:"), this);
    prompt_->setObjectName(QStringLiteral("DynPrompt"));
    col->addWidget(prompt_);

    auto* fields = new QWidget(this);
    auto* row = new QHBoxLayout(fields);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);
    primary_ = new QLineEdit(fields);
    primary_->setMinimumWidth(80);
    secondary_ = new QLineEdit(fields);
    secondary_->setMinimumWidth(60);
    row->addWidget(primary_);
    row->addWidget(secondary_);
    col->addWidget(fields);
    secondary_->hide();

    // Ph6 autocomplete dropdown, reused at the cursor. A top-level NoFocus list so
    // it never steals typing from primary_ (the focus lesson). Driven by the SAME
    // registry suggest() as the bottom command line -- one suggestion source.
    suggest_popup_ = new QListWidget(this);
    suggest_popup_->setObjectName(QStringLiteral("DynSuggest"));
    suggest_popup_->setWindowFlags(Qt::FramelessWindowHint | Qt::Tool |
                                   Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    suggest_popup_->setFocusPolicy(Qt::NoFocus);
    suggest_popup_->setUniformItemSizes(true);
    suggest_popup_->hide();
    connect(suggest_popup_, &QListWidget::itemClicked, this,
            [this](QListWidgetItem*) { accept_suggestion(); });
    connect(primary_, &QLineEdit::textChanged, this, [this] { update_suggestions(); });

    primary_->installEventFilter(this);
    secondary_->installEventFilter(this);
}

void DynInput::set_prompt(const std::string& prompt) {
    prompt_->setText(prompt.empty() ? QStringLiteral("Command:") : QString::fromStdString(prompt));
    // A new prompt usually means a new command step -> clear stale typed text.
    primary_->clear();
    secondary_->clear();
    refresh_live();
    adjustSize();
}

void DynInput::place_at_global(const QPoint& global_top_left) {
    move(global_top_left);
}

void DynInput::focus_field() {
    QLineEdit* f = (secondary_->isVisible() && secondary_->hasFocus()) ? secondary_ : primary_;
    f->setFocus(Qt::OtherFocusReason);
}

void DynInput::test_set_primary(const std::string& s) {
    primary_->setText(QString::fromStdString(s));
}
void DynInput::test_set_secondary(const std::string& s) {
    secondary_->setText(QString::fromStdString(s));
}
bool DynInput::secondary_visible() const {
    return secondary_->isVisible();
}
std::string DynInput::prompt_text() const {
    return prompt_->text().toStdString();
}
bool DynInput::field_has_focus() const {
    return primary_->hasFocus() || (secondary_->isVisible() && secondary_->hasFocus());
}

bool DynInput::is_typing() const {
    const bool p = primary_->hasFocus() && !primary_->text().isEmpty();
    const bool s = secondary_->isVisible() && secondary_->hasFocus() && !secondary_->text().isEmpty();
    return p || s;
}

void DynInput::on_constrained_cursor(double cx, double cy) {
    cx_ = cx;
    cy_ = cy;
    refresh_live();
}

void DynInput::refresh_live() {
    if (processor_ == nullptr) {
        return;
    }
    using command::PreviewKind;
    const command::PreviewSpec& pv = processor_->preview();
    const auto& pts = pv.points;
    const core::Vec2 cur{cx_, cy_};
    const bool have_anchor = !pts.empty();
    auto length_angle = [&](double& len, double& ang) {
        const core::Vec2 a = have_anchor ? pts[0] : core::Vec2{0, 0};
        len = core::distance(a, cur);
        ang = std::atan2(cur.y - a.y, cur.x - a.x) * kRadToDeg;
    };

    bool two = false;
    switch (pv.kind) {
    case PreviewKind::Segment: {
        double len = 0, ang = 0;
        length_angle(len, ang);
        primary_->setPlaceholderText(QStringLiteral("len %1").arg(fmt(len)));
        secondary_->setPlaceholderText(QStringLiteral("ang %1").arg(fmt(ang)));
        two = have_anchor;
        break;
    }
    case PreviewKind::Circle: {
        const double r = have_anchor ? core::distance(pts[0], cur) : 0.0;
        primary_->setPlaceholderText(QStringLiteral("radius %1").arg(fmt(r)));
        break;
    }
    case PreviewKind::Rectangle: {
        const core::Vec2 a = have_anchor ? pts[0] : core::Vec2{0, 0};
        primary_->setPlaceholderText(QStringLiteral("w %1").arg(fmt(std::abs(cur.x - a.x))));
        secondary_->setPlaceholderText(QStringLiteral("h %1").arg(fmt(std::abs(cur.y - a.y))));
        two = have_anchor;
        break;
    }
    default:
        primary_->setPlaceholderText(processor_->has_active_command()
                                         ? QStringLiteral("x,y / @dx,dy / @d<a")
                                         : QStringLiteral("command"));
        break;
    }
    if (two != secondary_->isVisible()) {
        secondary_->setVisible(two);
        adjustSize();
    }
}

void DynInput::update_suggestions() {
    // Only while idle: suggestions are command tokens, not coordinates/values.
    if (processor_ == nullptr || processor_->has_active_command()) {
        hide_suggestions();
        return;
    }
    const QString token = primary_->text().trimmed();
    if (token.isEmpty()) {
        hide_suggestions();
        return;
    }
    const auto matches = processor_->registry().suggest(token.toStdString());
    if (matches.empty()) {
        hide_suggestions();
        return;
    }
    suggest_popup_->clear();
    for (const auto& m : matches) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1\t%2").arg(QString::fromStdString(m.alias),
                                         QString::fromStdString(m.name)));
        item->setData(Qt::UserRole, QString::fromStdString(m.alias));
        suggest_popup_->addItem(item);
    }
    suggest_popup_->setCurrentRow(0);
    const int rows = std::min(suggest_popup_->count(), 8);
    const int h = rows * 20 + 6;
    const int w = std::max(primary_->width(), 140);
    // Anchor just below the primary field, in global coords (the popup is top-level).
    suggest_popup_->setGeometry(QRect(primary_->mapToGlobal(QPoint(0, primary_->height())), QSize(w, h)));
    suggest_popup_->show();
    suggest_popup_->raise();
}

void DynInput::hide_suggestions() {
    if (suggest_popup_ != nullptr) {
        suggest_popup_->hide();
    }
}

void DynInput::move_suggestion(int delta) {
    const int count = suggest_popup_->count();
    if (count == 0) {
        return;
    }
    const int row = std::clamp(suggest_popup_->currentRow() + delta, 0, count - 1);
    suggest_popup_->setCurrentRow(row);
}

void DynInput::accept_suggestion() {
    QListWidgetItem* item = suggest_popup_->currentItem();
    if (item == nullptr) {
        return;
    }
    const QString alias = item->data(Qt::UserRole).toString();
    hide_suggestions();
    primary_->setText(alias);
    submit(); // starts the command (submit_line(alias) when idle)
}

bool DynInput::suggestions_visible() const {
    return suggest_popup_ != nullptr && suggest_popup_->isVisible();
}
int DynInput::suggestion_count() const {
    return suggest_popup_ != nullptr ? suggest_popup_->count() : 0;
}

void DynInput::submit() {
    if (processor_ == nullptr) {
        return;
    }
    using command::PreviewKind;
    const QString p = primary_->text().trimmed();
    const QString s = secondary_->text().trimmed();

    if (!processor_->has_active_command()) {
        if (p.isEmpty()) {
            return; // nothing to start
        }
        processor_->submit_line(p.toStdString());
        primary_->clear();
        return;
    }

    const command::PreviewSpec& pv = processor_->preview();
    const auto& pts = pv.points;
    const core::Vec2 cur{cx_, cy_};
    const core::Vec2 anchor = pts.empty() ? core::Vec2{0, 0} : pts[0];
    std::string line;

    switch (pv.kind) {
    case PreviewKind::Segment: {
        if (p.isEmpty() && s.isEmpty()) {
            return; // need at least one typed value to override the cursor
        }
        bool ok = false;
        const double live_len = core::distance(anchor, cur);
        const double live_ang = std::atan2(cur.y - anchor.y, cur.x - anchor.x) * kRadToDeg;
        const double len = p.isEmpty() ? live_len : to_num(p, ok);
        const double ang = s.isEmpty() ? live_ang : to_num(s, ok);
        line = "@" + std::to_string(len) + "<" + std::to_string(ang);
        break;
    }
    case PreviewKind::Circle: {
        if (p.isEmpty()) {
            return;
        }
        bool ok = false;
        const double rad = to_num(p, ok);
        double ang = std::atan2(cur.y - anchor.y, cur.x - anchor.x) * kRadToDeg;
        if (core::distance(anchor, cur) < 1e-9) {
            ang = 0.0;
        }
        line = "@" + std::to_string(rad) + "<" + std::to_string(ang);
        break;
    }
    case PreviewKind::Rectangle: {
        if (p.isEmpty() && s.isEmpty()) {
            return;
        }
        bool ok = false;
        const double live_w = cur.x - anchor.x;
        const double live_h = cur.y - anchor.y;
        const double w = p.isEmpty() ? live_w : to_num(p, ok);
        const double h = s.isEmpty() ? live_h : to_num(s, ok);
        line = "@" + std::to_string(w) + "," + std::to_string(h);
        break;
    }
    default:
        if (p.isEmpty()) {
            return;
        }
        line = p.toStdString(); // a coordinate (x,y / @dx,dy / @d<a) or an option keyword
        break;
    }
    processor_->submit_line(line);
    primary_->clear();
    secondary_->clear();
}

void DynInput::hideEvent(QHideEvent* event) {
    hide_suggestions();
    QWidget::hideEvent(event);
}

bool DynInput::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        // While the autocomplete popup is open (primary_ only), navigate it (Ph6).
        if (watched == primary_ && suggestions_visible()) {
            switch (ke->key()) {
            case Qt::Key_Down:
            case Qt::Key_Tab:
                move_suggestion(+1);
                return true;
            case Qt::Key_Up:
            case Qt::Key_Backtab:
                move_suggestion(-1);
                return true;
            case Qt::Key_Escape:
                hide_suggestions(); // close the list only; do NOT cancel the command
                return true;
            case Qt::Key_Return:
            case Qt::Key_Enter: {
                const std::string typed = primary_->text().trimmed().toStdString();
                if (processor_ != nullptr && processor_->registry().contains(typed)) {
                    hide_suggestions();
                    submit(); // typed token is a complete command -> run it
                } else {
                    accept_suggestion(); // else accept the highlighted suggestion
                }
                return true;
            }
            default:
                break; // let the key through; textChanged refreshes the list
            }
            return QWidget::eventFilter(watched, event);
        }
        switch (ke->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            submit();
            return true;
        case Qt::Key_Escape:
            primary_->clear();
            secondary_->clear();
            if (escape_cb_) {
                escape_cb_();
            }
            return true;
        case Qt::Key_Tab:
            if (secondary_->isVisible()) {
                (watched == primary_ ? secondary_ : primary_)->setFocus(Qt::TabFocusReason);
                return true;
            }
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace musacad::ui
