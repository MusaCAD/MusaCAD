#include "musacad/command/command_registry.hpp"

#include <algorithm>
#include <cctype>

#include "musacad/command/commands.hpp"

namespace musacad::command {

namespace {
std::string upper(std::string_view s) {
    std::string r(s);
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return r;
}
} // namespace

void CommandRegistry::register_command(std::initializer_list<std::string_view> aliases,
                                       Factory factory) {
    // Instantiate once to capture the command's full name for suggestions.
    std::string name;
    if (std::unique_ptr<ICommand> probe = factory()) {
        name = probe->name();
    }
    for (const std::string_view a : aliases) {
        const std::string key = upper(a);
        table_[key] = factory;
        entries_.push_back(CommandSuggestion{key, name});
    }
}

std::unique_ptr<ICommand> CommandRegistry::create(std::string_view alias) const {
    const auto it = table_.find(upper(alias));
    return it == table_.end() ? nullptr : it->second();
}

bool CommandRegistry::contains(std::string_view alias) const {
    return table_.find(upper(alias)) != table_.end();
}

std::vector<CommandSuggestion> CommandRegistry::suggest(std::string_view prefix) const {
    const std::string p = upper(prefix);
    std::vector<CommandSuggestion> out;
    if (p.empty()) {
        return out;
    }
    const auto starts_with = [](const std::string& s, const std::string& pre) {
        return s.size() >= pre.size() && s.compare(0, pre.size(), pre) == 0;
    };
    for (const CommandSuggestion& e : entries_) {
        if (starts_with(e.alias, p) || starts_with(upper(e.name), p)) {
            out.push_back(e);
        }
    }
    // Order: alias-prefix matches first, then by alias length (short aliases
    // first), then alphabetically. Stable, deterministic for the dropdown.
    std::sort(out.begin(), out.end(), [&](const CommandSuggestion& a, const CommandSuggestion& b) {
        const bool ap = starts_with(a.alias, p);
        const bool bp = starts_with(b.alias, p);
        if (ap != bp) {
            return ap;
        }
        if (a.alias.size() != b.alias.size()) {
            return a.alias.size() < b.alias.size();
        }
        return a.alias < b.alias;
    });
    out.erase(std::unique(out.begin(), out.end(),
                          [](const CommandSuggestion& a, const CommandSuggestion& b) {
                              return a.alias == b.alias;
                          }),
              out.end());
    return out;
}

CommandRegistry CommandRegistry::make_default() {
    CommandRegistry r;
    // --- the alias table: classic AutoCAD aliases -> command factories ---
    r.register_command({"L", "LINE"}, [] { return std::make_unique<LineCommand>(); });
    r.register_command({"C", "CIRCLE"}, [] { return std::make_unique<CircleCommand>(); });
    r.register_command({"PL", "PLINE"}, [] { return std::make_unique<PolylineCommand>(); });
    r.register_command({"A", "ARC"}, [] { return std::make_unique<ArcCommand>(); });
    r.register_command({"REC", "RECTANGLE", "RECTANG"},
                       [] { return std::make_unique<RectangleCommand>(); });
    r.register_command({"E", "ERASE"}, [] { return std::make_unique<EraseCommand>(); });
    r.register_command({"U", "UNDO"}, [] { return std::make_unique<UndoCommand>(); });
    r.register_command({"Z", "ZOOM"}, [] { return std::make_unique<ZoomCommand>(); });
    return r;
}

} // namespace musacad::command
