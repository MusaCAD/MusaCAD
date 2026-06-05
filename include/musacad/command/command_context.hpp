#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "musacad/core/command.hpp"
#include "musacad/core/math/math.hpp"

namespace musacad::command {

/// Sink for command-line text output (scrollback + the active prompt).
/// Implemented by the command-line widget.
class CommandOutput {
public:
    virtual ~CommandOutput() = default;
    virtual void append_line(const std::string& line) = 0;
    virtual void set_prompt(const std::string& prompt) = 0;
};

/// View operations a command may request (ZOOM). Implemented by the viewport;
/// kept on the render/UI side so it never involves the geometry thread.
class ViewControl {
public:
    virtual ~ViewControl() = default;
    virtual void zoom_extents() = 0;
    virtual void zoom_scale(double factor) = 0;
};

/// The services a running command uses to interact with the system. Commands
/// never touch the GeometryStore: the only way they affect geometry is by
/// emitting a core::Command via submit(), which the processor forwards to the
/// UI->geometry MPSC queue.
class CommandContext {
public:
    virtual ~CommandContext() = default;

    virtual void echo(const std::string& line) = 0;     ///< scrollback message
    virtual void set_prompt(const std::string& prompt) = 0;

    virtual void submit(core::Command command) = 0;     ///< -> geometry queue
    [[nodiscard]] virtual std::uint64_t group_id() const = 0;

    [[nodiscard]] virtual std::optional<core::Vec2> last_point() const = 0;
    virtual void set_last_point(core::Vec2 p) = 0;
    virtual void clear_last_point() = 0;

    [[nodiscard]] virtual ViewControl* view() = 0;       ///< may be null in tests
};

} // namespace musacad::command
