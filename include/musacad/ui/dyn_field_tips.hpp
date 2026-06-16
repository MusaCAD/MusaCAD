// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <QFrame>
#include <QPoint>
#include <QRect>

#include "musacad/command/dyn_fields.hpp"
#include "musacad/core/math/math.hpp"

class QLabel;
class QLineEdit;

namespace musacad::command {
class CommandProcessor;
}

namespace musacad::ui {

/// One small floating value box anchored next to the rubber-band geometry it
/// describes (AutoCAD's on-geometry Dynamic Input). A frameless tool window with a
/// label + an editable value field. It is draggable from the label area (it remembers
/// an offset from its geometry anchor for the rest of the command) and reports key
/// gestures (Enter / Tab / Esc / edit) to the owning DynFieldTips. It holds no command
/// logic -- the manager composes the submit string and routes it to the processor.
class FieldTip : public QFrame {
    Q_OBJECT

public:
    explicit FieldTip(QWidget* parent = nullptr);

    void set_slot(int s) { slot_ = s; }
    [[nodiscard]] int slot() const { return slot_; }

    void set_label(const QString& text);
    /// The live (cursor-derived) value shown until the user types an override.
    void set_live(const QString& placeholder);
    /// Hide the value field -> a display-only tip (used for the floating prompt).
    void set_display_only(bool on);

    [[nodiscard]] QString typed() const; ///< trimmed typed text ("" if none)
    void clear_typed();
    void focus_value();
    [[nodiscard]] bool is_typing() const;        ///< focused AND non-empty (Delete-guard)
    [[nodiscard]] bool value_has_focus() const;  ///< the value field holds keyboard focus
    bool deliver_key(int key, Qt::KeyboardModifiers mods); ///< test: route a key to the value field
    void test_set_value(const QString& text);    ///< test: set the value field text
    bool test_drag(int dx, int dy);              ///< test: synth a frame drag; true if it moved

    /// Position: pos = projected geometry anchor + the user's drag offset, CLAMPED to
    /// stay within `bounds` (the viewport's on-screen rect) so a tip can never land in
    /// the ribbon / off-canvas / on the wrong monitor.
    void set_anchor_global(const QPoint& anchor_global);
    void set_bounds(const QRect& bounds) { bounds_ = bounds; }
    void reset_offset() { offset_ = QPoint(0, 0); }

Q_SIGNALS:
    void submitted(int slot);     ///< Enter -> compose + submit this step
    void tabbed(int slot, int dir); ///< Tab (+1) / Backtab (-1) -> cycle focus
    void escaped();               ///< Esc -> route to the command's escape
    void editChanged(int slot);   ///< text edited -> refresh the dimension lock

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QLabel* label_ = nullptr;
    QLineEdit* value_ = nullptr;
    int slot_ = 0;
    QPoint anchor_{};            ///< last projected geometry anchor (global)
    QPoint offset_{};            ///< user drag offset, persisted for the command
    QRect bounds_{};             ///< viewport on-screen rect; tip is clamped inside it
    bool dragging_ = false;
    QPoint drag_press_global_{};
    QPoint drag_origin_{};
};

/// The on-geometry Dynamic Input surface: a pool of FieldTip boxes positioned on the
/// live rubber-band (length under one edge, width by the other, radius on the radius
/// line, ...) plus a free-floating prompt label. The fields come from the declarative
/// command::dyn_fields() schema; typed values are submitted through the SAME
/// CommandProcessor pipeline as the bottom command line (command::compose_dyn_submit).
/// Tips are draggable and Tab cycles focus. This is a UI surface, not new command
/// logic -- it never touches the store.
class DynFieldTips : public QObject, public command::CommandOutput {
    Q_OBJECT

public:
    explicit DynFieldTips(QWidget* host, QObject* parent = nullptr);

    // CommandOutput: the prompt mirrors here (via the same fan-out as the DYN box);
    // scrollback stays on the command line.
    void append_line(const std::string&) override {}
    void set_prompt(const std::string& prompt) override; ///< the free-floating prompt label

    void set_processor(command::CommandProcessor* p) { processor_ = p; }
    /// Maps a world point to a GLOBAL screen position for anchoring a tip (the host
    /// projects through the camera and applies the device-pixel ratio + mapToGlobal).
    void set_project(std::function<QPoint(core::Vec2)> fn) { project_ = std::move(fn); }
    /// The viewport's current on-screen rect (global) -- tips are clamped to it so they
    /// stay over the drawing area (never the ribbon) and on the geometry's monitor.
    void set_bounds_provider(std::function<QRect()> fn) { bounds_fn_ = std::move(fn); }
    /// Esc inside a tip routes here (same handler as the DYN box's Esc).
    void set_escape_callback(std::function<void()> cb) { escape_cb_ = std::move(cb); }
    /// A typed value (un)locks a preview dimension; the host forwards it to the
    /// viewport's render-side lock so the rubber-band reflects it.
    void set_lock_callback(std::function<void(std::optional<double>, std::optional<double>)> cb) {
        lock_cb_ = std::move(cb);
    }

    void set_enabled(bool on);                  ///< F12 master toggle (mirrors the DYN box)
    void on_constrained_cursor(double cx, double cy); ///< live cursor each frame -> refresh
    void hide_all();                            ///< command end / Esc: clear tips, offsets, lock
    void focus_first();                         ///< auto-focus the first tip (type without a click)
    [[nodiscard]] bool is_typing() const;       ///< any tip field focused + non-empty
    [[nodiscard]] bool any_field_focused() const; ///< any tip value field holds keyboard focus

    // --- test / diagnostic hooks (real-window verification) ---
    [[nodiscard]] int visible_field_count() const; ///< number of shown value tips
    /// True iff every visible tip's window sits within `tol_px` of its projected
    /// geometry anchor (proves the tooltips track the rubber-band, not a stale layout).
    [[nodiscard]] bool all_tips_on_anchor(int tol_px) const;
    [[nodiscard]] std::string debug_state() const; ///< per-tip anchor/expected/actual dump
    [[nodiscard]] std::string debug_winids() const; ///< "winid x y w h" per visible tip (capture)
    [[nodiscard]] bool tip_field_focused(int slot) const; ///< for Tab-cycle verification
    bool focus_slot(int slot);                  ///< programmatically focus a tip (tests)
    bool press_tab_on(int slot, int dir);       ///< drive the Tab gesture (tests)
    bool set_field_text(int slot, const std::string& text); ///< type into a tip (tests)
    bool submit_slot(int slot);                 ///< drive Enter on a tip (tests)
    bool drag_slot(int slot, int dx, int dy);   ///< drive a frame drag on a tip (tests)

private:
    void refresh();                ///< rebuild tips from the live preview + cursor
    void reposition();             ///< move each tip to its projected anchor + offset
    void submit_from(int slot);    ///< Enter in a tip -> compose + submit the step
    void cycle_focus(int slot, int dir);
    void update_lock();            ///< gather typed values -> lock_cb_
    [[nodiscard]] bool dimensional() const; ///< the live preview has on-geometry fields
    FieldTip* ensure_tip(int index);

    QWidget* host_ = nullptr;
    command::CommandProcessor* processor_ = nullptr;
    std::function<QPoint(core::Vec2)> project_;
    std::function<QRect()> bounds_fn_;
    std::function<void()> escape_cb_;
    std::function<void(std::optional<double>, std::optional<double>)> lock_cb_;

    bool enabled_ = false;
    std::vector<FieldTip*> tips_;          ///< pooled; index order matches the schema
    FieldTip* prompt_tip_ = nullptr;       ///< display-only floating prompt
    std::vector<command::DynField> fields_; ///< last computed schema
    std::string prompt_;
    core::Vec2 cursor_{};
};

} // namespace musacad::ui
