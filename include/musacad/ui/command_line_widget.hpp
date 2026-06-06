#pragma once

#include <string>
#include <vector>

#include <QString>
#include <QWidget>

#include "musacad/command/command_context.hpp"

class QPlainTextEdit;
class QLineEdit;
class QLabel;
class QListWidget;
class QEvent;
class QObject;

namespace musacad::command {
class CommandProcessor;
}

namespace musacad::ui {

/// AutoCAD-style command line docked at the bottom: a read-only scrollback above
/// a prompt + input field. Up/Down recall previous entries; ENTER submits (an
/// empty line repeats the last command); ESC cancels the active command. It is
/// the CommandOutput for the processor.
class CommandLineWidget : public QWidget, public command::CommandOutput {
    Q_OBJECT

public:
    explicit CommandLineWidget(QWidget* parent = nullptr);

    void set_processor(command::CommandProcessor* processor) { processor_ = processor; }

    void focus_input();

    /// True only when the user is actively editing the command-line field (it has
    /// focus AND contains text). Used to decide whether Delete should edit text
    /// or erase the selection. An empty, idle field does NOT count as typing.
    [[nodiscard]] bool is_typing() const;

    /// Test hook: focus the input and set its text (used by MUSACAD_SELFTEST).
    void debug_set_input(const QString& text);

    /// Test hook: the full scrollback text (used by MUSACAD_SELFTEST to verify the
    /// engine's command-result messages reach the command line).
    [[nodiscard]] std::string debug_scrollback() const;

    /// When true, ENTER always accepts the highlighted suggestion. Default false
    /// (AutoCAD behavior: ENTER runs the typed command if complete, else accepts
    /// the highlighted suggestion).
    void set_enter_picks_first(bool on) { enter_picks_first_ = on; }

    // CommandOutput
    void append_line(const std::string& line) override;
    void set_prompt(const std::string& prompt) override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void on_return();
    void update_suggestions();
    void hide_suggestions();
    [[nodiscard]] bool suggestions_visible() const;
    void move_selection(int delta);
    void accept_highlighted();

    command::CommandProcessor* processor_ = nullptr;
    QPlainTextEdit* scrollback_ = nullptr;
    QLabel* prompt_label_ = nullptr;
    QLineEdit* input_ = nullptr;
    QListWidget* suggest_popup_ = nullptr;
    std::vector<QString> history_;
    int history_index_ = 0;
    bool enter_picks_first_ = false;
    QString prompt_text_ = QStringLiteral("Command: ");
};

} // namespace musacad::ui
