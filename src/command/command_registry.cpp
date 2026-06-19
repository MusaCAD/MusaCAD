// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

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
    // Modify (Phase 7).
    r.register_command({"M", "MOVE"}, [] { return std::make_unique<MoveCommand>(); });
    r.register_command({"CO", "CP", "COPY"}, [] { return std::make_unique<CopyCommand>(); });
    r.register_command({"MI", "MIRROR"}, [] { return std::make_unique<MirrorCommand>(); });
    r.register_command({"O", "OFFSET"}, [] { return std::make_unique<OffsetCommand>(); });
    r.register_command({"TR", "TRIM"}, [] { return std::make_unique<TrimCommand>(); });
    r.register_command({"J", "JOIN"}, [] { return std::make_unique<JoinCommand>(); });
    // Modify (Phase 10).
    r.register_command({"RO", "ROTATE"}, [] { return std::make_unique<RotateCommand>(); });
    r.register_command({"SC", "SCALE"}, [] { return std::make_unique<ScaleCommand>(); });
    r.register_command({"AR", "ARRAY"}, [] { return std::make_unique<ArrayCommand>(); });
    r.register_command({"EX", "EXTEND"}, [] { return std::make_unique<ExtendCommand>(); });
    r.register_command({"F", "FILLET"}, [] { return std::make_unique<FilletCommand>(); });
    r.register_command({"CHA", "CHAMFER"}, [] { return std::make_unique<ChamferCommand>(); });
    r.register_command({"MA", "MATCHPROP", "PAINTER"},
                       [] { return std::make_unique<MatchPropCommand>(); });
    // Annotation (Phase 13).
    r.register_command({"DT", "TEXT"}, [] { return std::make_unique<TextCommand>(); });
    r.register_command({"DLI", "DIMLINEAR"}, [] {
        return std::make_unique<LinearDimensionCommand>(core::DimType::Linear, "DIMLINEAR");
    });
    r.register_command({"DAL", "DIMALIGNED"}, [] {
        return std::make_unique<LinearDimensionCommand>(core::DimType::Aligned, "DIMALIGNED");
    });
    r.register_command({"DRA", "DIMRADIUS"}, [] {
        return std::make_unique<RadialDimensionCommand>(core::DimType::Radius, "DIMRADIUS");
    });
    r.register_command({"DDI", "DIMDIAMETER"}, [] {
        return std::make_unique<RadialDimensionCommand>(core::DimType::Diameter, "DIMDIAMETER");
    });
    r.register_command({"DAN", "DIMANGULAR"},
                       [] { return std::make_unique<AngularDimensionCommand>(); });
    r.register_command({"DIM"}, [] { return std::make_unique<DimCommand>(); });
    r.register_command({"LEADER"}, [] { return std::make_unique<LeaderCommand>(); });
    r.register_command({"MT", "MTEXT", "T"}, [] { return std::make_unique<MTextCommand>(); });
    r.register_command({"LE", "QLEADER", "QL"},
                       [] { return std::make_unique<QLeaderCommand>(); });
    r.register_command({"ED", "TEXTEDIT", "DDEDIT"},
                       [] { return std::make_unique<TextEditCommand>(); });
    r.register_command({"PR", "PROPERTIES", "PROPS", "CH"},
                       [] { return std::make_unique<PropertiesCommand>(); });
    r.register_command({"DWGIN"}, [] { return std::make_unique<DwgInCommand>(); });
    r.register_command({"DWGOUT"}, [] { return std::make_unique<DwgOutCommand>(); });
    r.register_command({"PLOT", "PRINT"}, [] { return std::make_unique<PlotCommand>(); });
    r.register_command({"LTSCALE", "LTS"}, [] { return std::make_unique<LtscaleCommand>(); });
    return r;
}

} // namespace musacad::command
