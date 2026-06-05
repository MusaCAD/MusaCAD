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
    for (const std::string_view a : aliases) {
        table_[upper(a)] = factory;
    }
}

std::unique_ptr<ICommand> CommandRegistry::create(std::string_view alias) const {
    const auto it = table_.find(upper(alias));
    return it == table_.end() ? nullptr : it->second();
}

bool CommandRegistry::contains(std::string_view alias) const {
    return table_.find(upper(alias)) != table_.end();
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
