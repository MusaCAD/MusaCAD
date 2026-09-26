// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <optional>
#include <string>

#include "musacad/command/command_context.hpp"
#include "musacad/core/entity_handle.hpp"

namespace musacad::command {

/// A single interactive command, modelled as a small state machine. The
/// processor drives it: start() emits the first prompt; input() handles each
/// submitted line; cancel() handles ESC. The command reports completion via
/// done(). This is intentionally separate from the alias table (which only maps
/// tokens to command factories) -- parsing is data, behaviour is a state machine.
class ICommand {
public:
    virtual ~ICommand() = default;

    /// Human-readable command name (for the echo line).
    [[nodiscard]] virtual std::string name() const = 0;

    virtual void start(CommandContext& ctx) = 0;
    virtual void input(CommandContext& ctx, const std::string& text) = 0;
    virtual void cancel(CommandContext& ctx) = 0;

    /// Optional live feedback as the cursor rolls over entities (the smart DIM
    /// command uses it to preview which dimension type it will create). `kind` is
    /// the entity under the cursor, or nullopt when over empty space. Default no-op.
    virtual void hover(CommandContext& /*ctx*/, std::optional<core::EntityKind> /*kind*/) {}

    [[nodiscard]] virtual bool done() const = 0;

    /// True if the command consumes cursor picks as object selection (e.g.
    /// ERASE) rather than as coordinate input.
    [[nodiscard]] virtual bool wants_selection() const { return false; }

    /// True while the command sits at AutoCAD's "Select objects:" prompt. The viewport
    /// then runs its ordinary selection gestures -- a pick, a window (left-to-right) or a
    /// crossing window (right-to-left), accumulating -- and Enter or right-click ends the
    /// phase. Selection drags take the raw cursor, never osnap or ortho: a selection
    /// window is a screen region, not geometry. State-dependent by design: the same
    /// command's later coordinate picks DO snap and honour ortho.
    [[nodiscard]] virtual bool in_selection_phase() const { return false; }
    /// Remove mode at "Select objects:" (the Remove keyword): the viewport's picks and
    /// windows take objects out of the selection instead of adding them.
    [[nodiscard]] virtual bool selection_removing() const { return false; }
    /// The viewport finished a selection gesture (a pick, a window, a lasso) while the
    /// command sat at "Select objects:". SIngle mode ends the phase on it; MATCHPROP
    /// applies to what was just chosen.
    virtual void selection_gesture(CommandContext& /*ctx*/) {}
};

} // namespace musacad::command
