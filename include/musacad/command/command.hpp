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
};

} // namespace musacad::command
