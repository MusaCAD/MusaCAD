// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <QWidget>

#include "musacad/command/command_context.hpp"

class QLabel;
class QLineEdit;
class QListWidget;

namespace musacad::command {
class CommandProcessor;
}

namespace musacad::ui {

/// Fans a single CommandOutput stream (prompt + scrollback echo) to several sinks,
/// so the bottom command line and the cursor-anchored Dynamic Input both reflect the
/// exact same command state. Pure forwarding -- no logic.
class FanoutOutput : public command::CommandOutput {
public:
    void add(command::CommandOutput* sink) { sinks_.push_back(sink); }
    void append_line(const std::string& line) override {
        for (command::CommandOutput* s : sinks_) {
            s->append_line(line);
        }
    }
    void set_prompt(const std::string& prompt) override {
        for (command::CommandOutput* s : sinks_) {
            s->set_prompt(prompt);
        }
    }

private:
    std::vector<command::CommandOutput*> sinks_;
};

/// AutoCAD-style Dynamic Input: a small frameless surface anchored at the crosshair.
/// It is NOT new command logic -- it routes typed text through the existing
/// CommandProcessor (submit_line, the Ph4 coordinate parser, Ph6 tokens) and DISPLAYS
/// the live prompt + the rubber-band values the preview already computes. During a
/// dimensional preview it shows length/angle (line), radius (circle) or width/height
/// (rectangle), and typing an exact value commits through the same command step as a
/// click -- by composing the existing coordinate string (`@len<ang`, `@rad<ang`,
/// `@w,h`) and submitting it.
class DynInput : public QWidget, public command::CommandOutput {
    Q_OBJECT

public:
    explicit DynInput(QWidget* parent = nullptr);

    void set_processor(command::CommandProcessor* p) { processor_ = p; }
    /// Esc in the field routes here (the host calls the viewport's escape handler).
    void set_escape_callback(std::function<void()> cb) { escape_cb_ = std::move(cb); }

    // CommandOutput: DYN mirrors the prompt; scrollback stays on the command line.
    void append_line(const std::string&) override {}
    void set_prompt(const std::string& prompt) override;

    /// Position the surface near the crosshair (global screen coords already offset).
    void place_at_global(const QPoint& global_top_left);
    /// The constrained (ortho/polar/snap-applied) world cursor, for live values.
    void on_constrained_cursor(double cx, double cy);
    /// Grab keyboard focus on the active field (called on enable / after a pick).
    void focus_field();

    /// Focused AND non-empty -> the user is mid-typing (the Delete-guard contract,
    /// mirroring the command line). An empty focused field is NOT "typing".
    [[nodiscard]] bool is_typing() const;

    // --- test hooks (real-window self-test) ---
    void test_set_primary(const std::string& s);
    void test_set_secondary(const std::string& s);
    void test_submit() { submit(); }
    [[nodiscard]] bool secondary_visible() const;
    [[nodiscard]] std::string prompt_text() const;
    [[nodiscard]] bool field_has_focus() const;
    [[nodiscard]] int suggestion_count() const; ///< rows in the autocomplete popup
    [[nodiscard]] bool suggestions_visible() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void hideEvent(QHideEvent* event) override; ///< also dismiss the autocomplete popup

private:
    void submit();
    void refresh_live(); ///< update placeholders + field visibility from the preview
    // Ph6 command-autocomplete, reused at the cursor (one suggestion SOURCE: the
    // registry). Shown only while idle (typing a command token, not a value).
    void update_suggestions();
    void hide_suggestions();
    void move_suggestion(int delta);
    void accept_suggestion();

    command::CommandProcessor* processor_ = nullptr;
    std::function<void()> escape_cb_;
    QLabel* prompt_ = nullptr;
    QLineEdit* primary_ = nullptr;   ///< length / radius / width / coordinate / command
    QLineEdit* secondary_ = nullptr; ///< angle / height
    QListWidget* suggest_popup_ = nullptr; ///< autocomplete dropdown (top-level, NoFocus)
    double cx_ = 0.0;                ///< last constrained cursor (world)
    double cy_ = 0.0;
};

} // namespace musacad::ui
