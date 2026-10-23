#include <algorithm>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_registry.hpp"
#include "musacad/command/sysvars.hpp"

using namespace musacad::command;

namespace {
bool has_alias(const std::vector<CommandSuggestion>& v, const std::string& alias) {
    return std::any_of(v.begin(), v.end(),
                       [&](const CommandSuggestion& s) { return s.alias == alias; });
}
} // namespace

TEST_CASE("Autocomplete is driven by the registry and matches alias prefixes") {
    const CommandRegistry reg = CommandRegistry::make_default();

    const auto c = reg.suggest("C");
    REQUIRE_FALSE(c.empty());
    REQUIRE(has_alias(c, "C")); // CIRCLE alias
    // Every suggestion genuinely starts with the prefix on alias or name.
    for (const auto& s : c) {
        const bool by_alias = s.alias.rfind("C", 0) == 0;
        const bool by_name = s.name.rfind("C", 0) == 0;
        REQUIRE((by_alias || by_name));
    }
}

TEST_CASE("Autocomplete matches on the full command name too") {
    const CommandRegistry reg = CommandRegistry::make_default();
    const auto re = reg.suggest("RE");
    // "REC"/"RECTANGLE" should surface; name RECTANGLE starts with RE.
    REQUIRE(has_alias(re, "REC"));
}

TEST_CASE("Autocomplete is case-insensitive and carries the full name") {
    const CommandRegistry reg = CommandRegistry::make_default();
    const auto lower = reg.suggest("li");
    REQUIRE(has_alias(lower, "LINE"));
    for (const auto& s : lower) {
        if (s.alias == "LINE" || s.alias == "L") {
            REQUIRE(s.name == "LINE");
        }
    }
}

TEST_CASE("Autocomplete returns nothing for empty or unknown prefixes") {
    const CommandRegistry reg = CommandRegistry::make_default();
    REQUIRE(reg.suggest("").empty());
    REQUIRE(reg.suggest("ZZZQ").empty());
}

TEST_CASE("New registry rows appear in suggestions automatically (single source)") {
    CommandRegistry reg = CommandRegistry::make_default();
    REQUIRE(reg.suggest("WOB").empty());
    reg.register_command({"WOBBLE"}, [] { return std::unique_ptr<ICommand>(nullptr); }, "",
                         "Test command.");
    REQUIRE(has_alias(reg.suggest("WOB"), "WOBBLE"));
}

TEST_CASE("aliases_of lists a command's aliases, the short form first") {
    const CommandRegistry reg = CommandRegistry::make_default();
    const std::vector<std::string> line = reg.aliases_of("LINE");
    REQUIRE(line.size() >= 2);
    REQUIRE(line.front() == "L");
    REQUIRE(std::find(line.begin(), line.end(), "LINE") != line.end());
    REQUIRE(reg.aliases_of("NOT-A-COMMAND").empty());
    // Every command the registry lists has at least its own name among its aliases.
    for (const CommandSuggestion& c : reg.commands()) {
        REQUIRE_FALSE(reg.aliases_of(c.name).empty());
    }
}

TEST_CASE("Every system variable SETVAR knows is a registered command") {
    const CommandRegistry reg = CommandRegistry::make_default();
    for (const char* name : system_variable_names()) {
        INFO(name);
        REQUIRE(reg.contains(name));
    }
}
