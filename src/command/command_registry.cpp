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
                                       Factory factory, std::string_view icon,
                                       std::string_view description) {
    // Instantiate once to capture the command's full name for suggestions.
    std::string name;
    if (std::unique_ptr<ICommand> probe = factory()) {
        name = probe->name();
    }
    // The first alias is the short form (e.g. "L" for LINE); it doubles as the
    // tooltip alias unless it equals the command name (no separate short form).
    const std::string primary = aliases.size() > 0 ? upper(*aliases.begin()) : name;
    const CommandInfo meta{name, primary, std::string(icon), std::string(description)};
    for (const std::string_view a : aliases) {
        const std::string key = upper(a);
        table_[key] = factory;
        info_[key] = meta;
        entries_.push_back(CommandSuggestion{key, name, std::string(description)});
    }
}

const CommandInfo* CommandRegistry::find(std::string_view alias) const {
    const auto it = info_.find(upper(alias));
    return it == info_.end() ? nullptr : &it->second;
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
    // The alias table: classic AutoCAD aliases -> command factory + ribbon icon + a one-line
    // description (the single source of command truth -- the ribbon reads icon + tooltip from
    // here). Icons are Musa-authored SVGs under assets/ribbon/ ("" => placeholder square).
    using A = std::initializer_list<std::string_view>;
    const auto reg = [&](A aliases, CommandRegistry::Factory f, std::string_view icon,
                         std::string_view desc) { r.register_command(aliases, std::move(f), icon, desc); };

    // --- Draw ---
    reg({"L", "LINE"}, [] { return std::make_unique<LineCommand>(); }, "assets/ribbon/line.svg",
        "Create a series of straight-line segments.");
    reg({"C", "CIRCLE"}, [] { return std::make_unique<CircleCommand>(); }, "assets/ribbon/circle.svg",
        "Draw a circle from a center point and a radius or diameter.");
    reg({"PL", "PLINE"}, [] { return std::make_unique<PolylineCommand>(); },
        "assets/ribbon/polyline.svg",
        "Draw a connected sequence of line and arc segments as one object.");
    reg({"A", "ARC"}, [] { return std::make_unique<ArcCommand>(); }, "assets/ribbon/arc.svg",
        "Draw a circular arc through three points.");
    reg({"REC", "RECTANGLE", "RECTANG"}, [] { return std::make_unique<RectangleCommand>(); },
        "assets/ribbon/rectangle.svg", "Draw a rectangle from two opposite corners.");

    // --- Edit / view ---
    reg({"E", "ERASE"}, [] { return std::make_unique<EraseCommand>(); }, "assets/ribbon/erase.svg",
        "Delete selected objects from the drawing.");
    reg({"U", "UNDO"}, [] { return std::make_unique<UndoCommand>(); }, "assets/ribbon/undo.svg",
        "Reverse the most recent action.");
    reg({"Z", "ZOOM"}, [] { return std::make_unique<ZoomCommand>(); }, "assets/ribbon/zoom.svg",
        "Zoom in or out to change the view magnification.");

    // --- Modify (Phase 7) ---
    reg({"M", "MOVE"}, [] { return std::make_unique<MoveCommand>(); }, "assets/ribbon/move.svg",
        "Move selected objects a specified distance and direction.");
    reg({"CO", "CP", "COPY"}, [] { return std::make_unique<CopyCommand>(); },
        "assets/ribbon/copy.svg", "Duplicate selected objects at a specified offset.");
    reg({"MI", "MIRROR"}, [] { return std::make_unique<MirrorCommand>(); },
        "assets/ribbon/mirror.svg", "Create a mirror-image copy of objects across an axis.");
    reg({"O", "OFFSET"}, [] { return std::make_unique<OffsetCommand>(); }, "assets/ribbon/offset.svg",
        "Create a parallel copy of a curve at a specified distance.");
    reg({"TR", "TRIM"}, [] { return std::make_unique<TrimCommand>(); }, "assets/ribbon/trim.svg",
        "Trim objects to meet the edges of other objects.");
    reg({"J", "JOIN"}, [] { return std::make_unique<JoinCommand>(); }, "assets/ribbon/join.svg",
        "Join collinear or connected objects into a single object.");

    // --- Modify (Phase 10) ---
    reg({"RO", "ROTATE"}, [] { return std::make_unique<RotateCommand>(); },
        "assets/ribbon/rotate.svg", "Rotate selected objects around a base point.");
    reg({"SC", "SCALE"}, [] { return std::make_unique<ScaleCommand>(); }, "assets/ribbon/scale.svg",
        "Resize selected objects uniformly about a base point.");
    reg({"AR", "ARRAY"}, [] { return std::make_unique<ArrayCommand>(); }, "assets/ribbon/array.svg",
        "Create a rectangular or polar pattern of copies.");
    reg({"EX", "EXTEND"}, [] { return std::make_unique<ExtendCommand>(); },
        "assets/ribbon/extend.svg", "Extend objects to meet the edges of other objects.");
    reg({"F", "FILLET"}, [] { return std::make_unique<FilletCommand>(); }, "assets/ribbon/fillet.svg",
        "Round corners between two intersecting lines, arcs, or polylines.");
    reg({"CHA", "CHAMFER"}, [] { return std::make_unique<ChamferCommand>(); },
        "assets/ribbon/chamfer.svg", "Bevel corners between two intersecting lines.");
    reg({"MA", "MATCHPROP", "PAINTER"}, [] { return std::make_unique<MatchPropCommand>(); },
        "assets/ribbon/matchprop.svg",
        "Copy properties from a source object to one or more target objects.");
    reg({"H", "HATCH", "BHATCH"}, [] { return std::make_unique<HatchCommand>(); },
        "assets/ribbon/hatch.svg", "Fill an enclosed area with a pattern or solid color.");

    // --- Annotation (Phase 13) ---
    reg({"DT", "TEXT"}, [] { return std::make_unique<TextCommand>(); }, "assets/ribbon/text.svg",
        "Create a single-line text object.");
    reg({"DLI", "DIMLINEAR"},
        [] { return std::make_unique<LinearDimensionCommand>(core::DimType::Linear, "DIMLINEAR"); },
        "assets/ribbon/dim-linear.svg", "Create a horizontal or vertical linear dimension.");
    reg({"DAL", "DIMALIGNED"},
        [] { return std::make_unique<LinearDimensionCommand>(core::DimType::Aligned, "DIMALIGNED"); },
        "assets/ribbon/dim-aligned.svg", "Create a dimension aligned with two points.");
    reg({"DRA", "DIMRADIUS"},
        [] { return std::make_unique<RadialDimensionCommand>(core::DimType::Radius, "DIMRADIUS"); },
        "assets/ribbon/dim-radius.svg", "Create a radius dimension for a circle or arc.");
    reg({"DDI", "DIMDIAMETER"},
        [] {
            return std::make_unique<RadialDimensionCommand>(core::DimType::Diameter, "DIMDIAMETER");
        },
        "assets/ribbon/dim-diameter.svg", "Create a diameter dimension for a circle or arc.");
    reg({"DAN", "DIMANGULAR"}, [] { return std::make_unique<AngularDimensionCommand>(); },
        "assets/ribbon/dim-angular.svg", "Create an angular dimension between two lines.");
    reg({"DIM"}, [] { return std::make_unique<DimCommand>(); }, "assets/ribbon/dim.svg",
        "Create a dimension suited to the selected object.");
    reg({"LEADER"}, [] { return std::make_unique<LeaderCommand>(); }, "assets/ribbon/leader.svg",
        "Draw a leader line with an arrowhead and annotation.");
    reg({"MT", "MTEXT", "T"}, [] { return std::make_unique<MTextCommand>(); },
        "assets/ribbon/mtext.svg", "Create a multiline (paragraph) text object.");
    reg({"LE", "QLEADER", "QL"}, [] { return std::make_unique<QLeaderCommand>(); },
        "assets/ribbon/leader.svg", "Draw a quick leader with an arrowhead and annotation.");
    reg({"ED", "TEXTEDIT", "DDEDIT"}, [] { return std::make_unique<TextEditCommand>(); },
        "assets/ribbon/text.svg", "Edit the contents of an existing text object.");
    reg({"PR", "PROPERTIES", "PROPS", "CH"}, [] { return std::make_unique<PropertiesCommand>(); },
        "assets/ribbon/properties.svg",
        "Open the Properties palette to view and edit object properties.");

    // --- File / settings ---
    reg({"DWGIN"}, [] { return std::make_unique<DwgInCommand>(); }, "assets/ribbon/import.svg",
        "Import geometry from a DWG file.");
    reg({"DWGOUT"}, [] { return std::make_unique<DwgOutCommand>(); }, "assets/ribbon/export.svg",
        "Export the drawing to a DWG file.");
    reg({"PLOT", "PRINT"}, [] { return std::make_unique<PlotCommand>(); }, "assets/ribbon/plot.svg",
        "Plot or print the drawing to paper or PDF.");
    reg({"LTSCALE", "LTS"}, [] { return std::make_unique<LtscaleCommand>(); },
        "assets/ribbon/ltscale.svg", "Set the global linetype scale factor.");
    return r;
}

} // namespace musacad::command
