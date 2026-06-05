#include "musacad/ui/command_line_widget.hpp"

#include <algorithm>

#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QVBoxLayout>

#include "musacad/command/command_processor.hpp"

namespace musacad::ui {

CommandLineWidget::CommandLineWidget(QWidget* parent) : QWidget(parent) {
    scrollback_ = new QPlainTextEdit(this);
    scrollback_->setReadOnly(true);
    scrollback_->setMaximumBlockCount(500);
    scrollback_->setFixedHeight(110);

    prompt_label_ = new QLabel(prompt_text_, this);
    input_ = new QLineEdit(this);
    input_->installEventFilter(this);
    connect(input_, &QLineEdit::returnPressed, this, &CommandLineWidget::on_return);

    const QFont mono(QStringLiteral("monospace"));
    scrollback_->setFont(mono);
    prompt_label_->setFont(mono);
    input_->setFont(mono);

    auto* row = new QHBoxLayout;
    row->addWidget(prompt_label_);
    row->addWidget(input_, 1);

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(4, 2, 4, 2);
    col->addWidget(scrollback_);
    col->addLayout(row);
}

void CommandLineWidget::focus_input() { input_->setFocus(); }

void CommandLineWidget::append_line(const std::string& line) {
    scrollback_->appendPlainText(QString::fromStdString(line));
}

void CommandLineWidget::set_prompt(const std::string& prompt) {
    prompt_text_ = QString::fromStdString(prompt);
    prompt_label_->setText(prompt_text_);
}

void CommandLineWidget::on_return() {
    const QString entry = input_->text();
    append_line(prompt_text_.toStdString() + entry.toStdString()); // echo the typed line
    if (!entry.isEmpty()) {
        history_.push_back(entry);
    }
    history_index_ = static_cast<int>(history_.size());
    input_->clear();
    if (processor_) {
        processor_->submit_line(entry.toStdString());
    }
}

bool CommandLineWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched == input_ && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        switch (key->key()) {
        case Qt::Key_Up:
            if (!history_.empty()) {
                history_index_ = std::max(0, history_index_ - 1);
                input_->setText(history_[static_cast<std::size_t>(history_index_)]);
            }
            return true;
        case Qt::Key_Down:
            if (history_index_ < static_cast<int>(history_.size()) - 1) {
                ++history_index_;
                input_->setText(history_[static_cast<std::size_t>(history_index_)]);
            } else {
                history_index_ = static_cast<int>(history_.size());
                input_->clear();
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
