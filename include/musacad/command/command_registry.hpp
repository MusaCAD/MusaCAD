#pragma once

#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "musacad/command/command.hpp"

namespace musacad::command {

/// Maps command aliases (e.g. "L", "LINE") to command factories. This is the
/// data table: adding a command means adding one row in make_default(); no
/// parser control flow changes.
class CommandRegistry {
public:
    using Factory = std::function<std::unique_ptr<ICommand>()>;

    /// Registers the same factory under several aliases (case-insensitive).
    void register_command(std::initializer_list<std::string_view> aliases, Factory factory);

    /// Creates the command for `alias` (case-insensitive), or nullptr if unknown.
    [[nodiscard]] std::unique_ptr<ICommand> create(std::string_view alias) const;

    [[nodiscard]] bool contains(std::string_view alias) const;

    /// The built-in AutoCAD-style command table.
    [[nodiscard]] static CommandRegistry make_default();

private:
    std::unordered_map<std::string, Factory> table_;
};

} // namespace musacad::command
