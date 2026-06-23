// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "musacad/command/command.hpp"

namespace musacad::command {

/// One autocomplete entry: a typed alias, its full command name, and the
/// command's one-line description (for a richer dropdown -- secondary line).
struct CommandSuggestion {
    std::string alias;
    std::string name;
    std::string description;
};

/// Presentation metadata for a command, shared by the ribbon (icon + tooltip)
/// and any future help/command-search UI. Carried in the registration so a new
/// command lights up the ribbon by filling in one row -- no ribbon-side wiring.
struct CommandInfo {
    std::string name;          ///< full name, e.g. "LINE"
    std::string primary_alias; ///< the short alias, e.g. "L" (== name when there is none)
    std::string icon;          ///< asset path, e.g. "assets/ribbon/line.svg" ("" = placeholder)
    std::string description;   ///< one concise sentence
};

/// Maps command aliases (e.g. "L", "LINE") to command factories + presentation
/// metadata. This is the data table: adding a command means adding one row in
/// make_default(); no parser control flow changes, and the ribbon reads its
/// icon + tooltip straight from here (single source of command truth).
class CommandRegistry {
public:
    using Factory = std::function<std::unique_ptr<ICommand>()>;

    /// Registers the same factory under several aliases (case-insensitive). The
    /// `icon` (asset path; "" = placeholder) and `description` (one sentence) are
    /// REQUIRED -- every command carries its own ribbon presentation, so a new
    /// command needs no separate ribbon edit. The first alias is the short form.
    void register_command(std::initializer_list<std::string_view> aliases, Factory factory,
                          std::string_view icon, std::string_view description);

    /// Creates the command for `alias` (case-insensitive), or nullptr if unknown.
    [[nodiscard]] std::unique_ptr<ICommand> create(std::string_view alias) const;

    [[nodiscard]] bool contains(std::string_view alias) const;

    /// Presentation metadata for the command bound to `alias` (case-insensitive),
    /// or nullptr if unknown. The ribbon resolves icon + tooltip through this.
    [[nodiscard]] const CommandInfo* find(std::string_view alias) const;

    /// Autocomplete: entries whose alias OR full name starts with `prefix`
    /// (case-insensitive). Sorted with exact-alias and alias-prefix matches
    /// first. Driven entirely by the registered command table -- the single
    /// source of command truth.
    [[nodiscard]] std::vector<CommandSuggestion> suggest(std::string_view prefix) const;

    /// The built-in AutoCAD-style command table.
    [[nodiscard]] static CommandRegistry make_default();

private:
    std::unordered_map<std::string, Factory> table_;
    std::unordered_map<std::string, CommandInfo> info_; // key = upper alias -> metadata
    std::vector<CommandSuggestion> entries_;            // (alias, name, desc) for suggestions
};

} // namespace musacad::command
