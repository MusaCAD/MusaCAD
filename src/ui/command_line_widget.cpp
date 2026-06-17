// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#include "musacad/ui/command_line_widget.hpp"

#include <algorithm>

#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QVBoxLayout>

#include "musacad/command/command_processor.hpp"

namespace musacad::ui {

namespace {
QString first_token(const QString& s) {
    const QString t = s.trimmed();
    int sp = t.indexOf(QLatin1Char(' '));
    const int tab = t.indexOf(QLatin1Char('\t'));
    if (tab >= 0 && (sp < 0 || tab < sp)) {
        sp = tab;
    }
    return sp < 0 ? t : t.left(sp);
}
} // namespace

CommandLineWidget::CommandLineWidget(QWidget* parent) : QWidget(parent) {
    scrollback_ = new QPlainTextEdit(this);
    scrollback_->setObjectName(QStringLiteral("CommandScrollback"));
    scrollback_->setReadOnly(true);
    scrollback_->setMaximumBlockCount(500);
    scrollback_->setFixedHeight(110);

    prompt_label_ = new QLabel(prompt_text_, this);
    prompt_label_->setObjectName(QStringLiteral("CommandPrompt"));
    input_ = new QLineEdit(this);
    input_->setObjectName(QStringLiteral("CommandInput"));
    input_->installEventFilter(this);
    connect(input_, &QLineEdit::returnPressed, this, &CommandLineWidget::on_return);
    connect(input_, &QLineEdit::textChanged, this, [this] { update_suggestions(); });

    const QFont mono(QStringLiteral("monospace"));
    scrollback_->setFont(mono);
    prompt_label_->setFont(mono);
    input_->setFont(mono);

    // Registry-driven autocomplete popup (child overlay, above the input).
    suggest_popup_ = new QListWidget(this);
    suggest_popup_->setObjectName(QStringLiteral("CommandSuggest"));
    suggest_popup_->setFont(mono);
    suggest_popup_->setFocusPolicy(Qt::NoFocus); // keep typing focus in input_
    suggest_popup_->setUniformItemSizes(true);
    suggest_popup_->hide();
    connect(suggest_popup_, &QListWidget::itemClicked, this,
            [this](QListWidgetItem*) { accept_highlighted(); });

    auto* row = new QHBoxLayout;
    row->addWidget(prompt_label_);
    row->addWidget(input_, 1);

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(4, 2, 4, 2);
    col->addWidget(scrollback_);
    col->addLayout(row);
}

void CommandLineWidget::focus_input() { input_->setFocus(); }

bool CommandLineWidget::is_typing() const {
    return input_->hasFocus() && !input_->text().isEmpty();
}

void CommandLineWidget::debug_set_input(const QString& text) {
    input_->setFocus();
    input_->setText(text);
}

void CommandLineWidget::append_line(const std::string& line) {
    scrollback_->appendPlainText(QString::fromStdString(line));
}

std::string CommandLineWidget::debug_scrollback() const {
    return scrollback_->toPlainText().toStdString();
}

void CommandLineWidget::set_prompt(const std::string& prompt) {
    prompt_text_ = QString::fromStdString(prompt);
    prompt_label_->setText(prompt_text_);
}

bool CommandLineWidget::suggestions_visible() const {
    return suggest_popup_ != nullptr && suggest_popup_->isVisible();
}

void CommandLineWidget::hide_suggestions() {
    suggest_popup_->hide();
    suggest_popup_->clear();
}

void CommandLineWidget::update_suggestions() {
    // Only while idle (not entering points for an active command).
    if (processor_ == nullptr || processor_->has_active_command()) {
        hide_suggestions();
        return;
    }
    const QString token = first_token(input_->text());
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
        const QString alias = QString::fromStdString(m.alias);
        const QString name = QString::fromStdString(m.name);
        auto* item = new QListWidgetItem(QStringLiteral("%1\t%2").arg(alias, name));
        item->setData(Qt::UserRole, alias);
        suggest_popup_->addItem(item);
    }
    suggest_popup_->setCurrentRow(0);

    const int rows = std::min(suggest_popup_->count(), 8);
    const int h = rows * 18 + 6;
    const int w = input_->width();
    const QPoint top_left = input_->geometry().topLeft();
    suggest_popup_->setGeometry(top_left.x(), std::max(0, top_left.y() - h), w, h);
    suggest_popup_->show();
    suggest_popup_->raise();
}

void CommandLineWidget::move_selection(int delta) {
    const int count = suggest_popup_->count();
    if (count == 0) {
        return;
    }
    int row = suggest_popup_->currentRow() + delta;
    row = std::clamp(row, 0, count - 1);
    suggest_popup_->setCurrentRow(row);
}

void CommandLineWidget::accept_highlighted() {
    QListWidgetItem* item = suggest_popup_->currentItem();
    if (item == nullptr) {
        return;
    }
    const QString alias = item->data(Qt::UserRole).toString();
    hide_suggestions();
    input_->setText(alias);
    on_return();
}

void CommandLineWidget::on_return() {
    const QString entry = input_->text();
    append_line(prompt_text_.toStdString() + entry.toStdString()); // echo the typed line
    input_->clear();
    hide_suggestions();
    if (processor_) {
        processor_->submit_line(entry.toStdString()); // history is recorded by the processor
    }
}

bool CommandLineWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched == input_ && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);

        if (suggestions_visible()) {
            switch (key->key()) {
            case Qt::Key_Down:
            case Qt::Key_Tab:
                move_selection(+1);
                return true;
            case Qt::Key_Up:
            case Qt::Key_Backtab:
                move_selection(-1);
                return true;
            case Qt::Key_Escape:
                hide_suggestions();
                return true;
            case Qt::Key_Return:
            case Qt::Key_Enter: {
                const QString typed = first_token(input_->text());
                if (!enter_picks_first_ && processor_ != nullptr &&
                    processor_->registry().contains(typed.toStdString())) {
                    on_return(); // typed command is complete -> run it
                } else {
                    accept_highlighted(); // otherwise accept the highlighted suggestion
                }
                return true;
            }
            default:
                break; // let the keystroke through; textChanged refreshes the popup
            }
            return QWidget::eventFilter(watched, event);
        }

        switch (key->key()) {
        case Qt::Key_Up:
            if (processor_ != nullptr) {
                input_->setText(QString::fromStdString(processor_->history_recall(+1))); // older
            }
            return true;
        case Qt::Key_Down:
            if (processor_ != nullptr) {
                input_->setText(QString::fromStdString(processor_->history_recall(-1))); // newer
            }
            return true;
        case Qt::Key_Escape:
            input_->clear();
            if (processor_) {
                processor_->cancel();
            }
            return true;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace musacad::ui
