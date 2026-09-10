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
    reg({"EL", "ELLIPSE"}, [] { return std::make_unique<EllipseCommand>(); },
        "assets/ribbon/ellipse.svg", "Draw an ellipse or an elliptical arc.");
    reg({"SPL", "SPLINE"}, [] { return std::make_unique<SplineCommand>(); },
        "assets/ribbon/spline.svg", "Draw a spline through fit points or by control vertices.");
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
    reg({"PU", "PURGE"}, [] { return std::make_unique<PurgeCommand>(); },
        "assets/ribbon/purge.svg", "Remove unused layers from the drawing.");
    reg({"AL", "ALIGN"}, [] { return std::make_unique<AlignCommand>(); },
        "assets/ribbon/align.svg",
        "Move, rotate and optionally scale a selection onto two destination points.");
    reg({"LEN", "LENGTHEN"}, [] { return std::make_unique<LengthenCommand>(); },
        "assets/ribbon/lengthen.svg", "Change the length of a line or arc.");
    reg({"BR", "BREAK"}, [] { return std::make_unique<BreakCommand>(false); },
        "assets/ribbon/break.svg", "Break a curve between two points.");
    reg({"BREAKATPOINT"}, [] { return std::make_unique<BreakCommand>(true); },
        "assets/ribbon/break.svg", "Split a curve at one point, leaving no gap.");
    reg({"XL", "XLINE"}, [] { return std::make_unique<XlineCommand>(false); },
        "assets/ribbon/xline.svg", "Draw an infinite construction line.");
    reg({"RAY"}, [] { return std::make_unique<XlineCommand>(true); },
        "assets/ribbon/ray.svg", "Draw a semi-infinite construction line.");
    reg({"WIPEOUT"}, [] { return std::make_unique<WipeoutCommand>(); }, "",
        "Mask what lies beneath a polygon (text still shows through); [Frames] and [Polyline].");
    reg({"FIELD"}, [] { return std::make_unique<FieldCommand>(); }, "",
        "Place text showing the date, time, file name or login, refreshed on regen.");
    reg({"OS", "OSNAP", "DDOSNAP"}, [] { return std::make_unique<OsnapCommand>(); }, "",
        "Open the running object snap settings.");
    reg({"-OSNAP"}, [] { return std::make_unique<OsnapModesCommand>(); }, "",
        "Set the running object snaps from a list of modes.");
    reg({"PE", "PEDIT"}, [] { return std::make_unique<PeditCommand>(); }, "",
        "Edit a polyline: close/open, join, vertices, spline, decurve, reverse.");
    reg({"B", "BLOCK", "-BLOCK"}, [] { return std::make_unique<BlockCommand>(); },
        "assets/ribbon/block.svg", "Make the selection a block definition (replaced by an insert).");
    reg({"ATT", "ATTDEF", "-ATTDEF"}, [] { return std::make_unique<AttdefCommand>(); }, "",
        "Define a block attribute: modes, tag, prompt, default value and text placement.");
    reg({"ATTDISP"}, [] { return std::make_unique<AttdispCommand>(); }, "",
        "Attribute visibility: Normal (each attribute's own mode), ON or OFF for all.");
    reg({"ATTEDIT", "-ATTEDIT"}, [] { return std::make_unique<AtteditCommand>(); }, "",
        "Change an attribute value on a block reference (one tag, or all).");
    reg({"REFEDIT"}, [] { return std::make_unique<RefeditCommand>(); }, "",
        "Edit a block definition in place: its members become a working set until REFCLOSE.");
    reg({"REFSET"}, [] { return std::make_unique<RefsetCommand>(); }, "",
        "Add objects to, or remove them from, the working set of the reference being edited.");
    reg({"REFCLOSE"}, [] { return std::make_unique<RefcloseCommand>(); }, "",
        "Save the working set back into the block definition, or discard the changes.");
    reg({"IAT", "IMAGEATTACH", "-IMAGEATTACH"}, [] { return std::make_unique<ImageAttachCommand>(); }, "",
        "Attach a raster image (PNG, JPEG, BMP, ...): file, embed or reference, insertion point, scale, rotation.");
    reg({"ICL", "IMAGECLIP"}, [] { return std::make_unique<ImageClipCommand>(); }, "",
        "Clip an image to a rectangular boundary, or delete, switch on or off its boundary.");
    reg({"IMAGEFRAME"}, [] { return std::make_unique<ImageFrameCommand>(); }, "",
        "Image frames: hidden, shown and plotted, or shown on screen only.");
    reg({"MVIEW", "MV"}, [] { return std::make_unique<MviewCommand>(); }, "",
        "A viewport on the layout showing model space: two corners or Fit; ON, OFF, Scale, Center.");
    reg({"MSPACE", "MS"}, [] { return std::make_unique<MspaceCommand>(); }, "",
        "Edit model space through a viewport (not available yet).");
    reg({"LAYOUT", "LO"}, [] { return std::make_unique<LayoutCommand>(); }, "",
        "Layouts (paper space sheets): Copy, Delete, New, Rename, Set, ?.");
    reg({"MODEL"}, [] { return std::make_unique<ModelSpaceCommand>(false); }, "",
        "Switch to model space.");
    reg({"PSPACE", "PS"}, [] { return std::make_unique<ModelSpaceCommand>(true); }, "",
        "Switch to paper space (the first layout).");
    reg({"I", "INSERT", "-INSERT"}, [] { return std::make_unique<InsertCommand>(); },
        "assets/ribbon/insert.svg", "Insert a block by name with scale and rotation.");
    reg({"W", "WBLOCK"}, [] { return std::make_unique<WblockCommand>(); }, "",
        "Write a block, or the whole drawing, to a .musa file.");
    reg({"RE", "REGEN"}, [] { return std::make_unique<RegenCommand>(); }, "",
        "Rebuild and redraw the scene.");
    reg({"ST", "STYLE", "-STYLE"}, [] { return std::make_unique<StyleCommand>(); }, "",
        "Create or change a named text style and make it current.");
    reg({"UN", "UNITS", "-UNITS"}, [] { return std::make_unique<UnitsCommand>(); }, "",
        "Set the display format and precision of lengths and angles.");
    reg({"AUDIT"}, [] { return std::make_unique<AuditDrawingCommand>(); }, "",
        "Check the drawing for bad references and structure; optionally fix them.");
    reg({"DO", "DONUT"}, [] { return std::make_unique<DonutCommand>(); },
        "assets/ribbon/donut.svg", "Draw a filled ring (or disc) from inside/outside diameters.");
    reg({"V", "VIEW", "-VIEW"}, [] { return std::make_unique<ViewCommand>(); },
        "assets/ribbon/view.svg", "Save, restore or delete named views.");
    reg({"G", "GROUP"}, [] { return std::make_unique<GroupCommand>(); },
        "assets/ribbon/group.svg", "Make the selected objects a named group.");
    reg({"UNGROUP"}, [] { return std::make_unique<UngroupCommand>(); },
        "assets/ribbon/ungroup.svg", "Dissolve a group.");
    reg({"PICKSTYLE"}, [] { return std::make_unique<PickStyleCommand>(); }, "",
        "Whether picking a group member selects the whole group.");
    reg({"REVCLOUD"}, [] { return std::make_unique<RevcloudCommand>(); },
        "assets/ribbon/revcloud.svg", "Draw a revision cloud, or turn an object into one.");
    reg({"X", "EXPLODE"}, [] { return std::make_unique<ExplodeCommand>(); },
        "assets/ribbon/explode.svg", "Break compound objects into their components.");
    reg({"POL", "POLYGON"}, [] { return std::make_unique<PolygonCommand>(); },
        "assets/ribbon/polygon.svg", "Draw a regular polygon by centre or by one edge.");
    reg({"PO", "POINT"}, [] { return std::make_unique<PointCommand>(); },
        "assets/ribbon/point.svg", "Place point objects; Esc ends.");
    reg({"DIV", "DIVIDE"}, [] { return std::make_unique<DivideCommand>(false); },
        "assets/ribbon/divide.svg", "Mark a curve into a number of equal segments with points.");
    reg({"ME", "MEASURE"}, [] { return std::make_unique<DivideCommand>(true); },
        "assets/ribbon/measure.svg", "Mark a curve at set intervals with points.");
    // The AutoCAD array family. ARRAY/-ARRAY ask for the type; the three ARRAY* forms
    // go straight to their own prompts. ARRAYEDIT/ARRAYCLOSE are absent by design --
    // they edit an ASSOCIATIVE array, and these arrays are non-associative (what
    // AutoCAD's own -ARRAY produces), so there is no association to reopen.
    reg({"AR", "ARRAY", "-ARRAY"}, [] { return std::make_unique<ArrayCommand>(); },
        "assets/ribbon/array.svg",
        "Create a rectangular, path or polar pattern of copies.");
    reg({"ARRAYRECT"}, [] { return std::make_unique<ArrayCommand>(ArrayCommand::Type::Rect); },
        "assets/ribbon/array.svg",
        "Create a rectangular pattern of copies in rows and columns.");
    reg({"ARRAYPOLAR"}, [] { return std::make_unique<ArrayCommand>(ArrayCommand::Type::Polar); },
        "assets/ribbon/array.svg", "Create a circular pattern of copies about a centre point.");
    reg({"ARRAYPATH"}, [] { return std::make_unique<ArrayCommand>(ArrayCommand::Type::Path); },
        "assets/ribbon/array.svg", "Distribute copies evenly along a path curve.");
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
    reg({"DOR", "DIMORDINATE"}, [] { return std::make_unique<OrdinateDimensionCommand>(); },
        "assets/ribbon/dim-ordinate.svg", "Create an ordinate (X or Y datum) dimension.");
    reg({"DJO", "DIMJOGGED"}, [] { return std::make_unique<JoggedDimensionCommand>(); },
        "assets/ribbon/dim-jogged.svg", "Create a jogged radius dimension for a large arc.");
    reg({"DAR", "DIMARC"}, [] { return std::make_unique<ArcLengthDimensionCommand>(); },
        "assets/ribbon/dim-arc.svg", "Create an arc length dimension.");
    reg({"DAN", "DIMANGULAR"}, [] { return std::make_unique<AngularDimensionCommand>(); },
        "assets/ribbon/dim-angular.svg", "Create an angular dimension between two lines.");
    reg({"DIM"}, [] { return std::make_unique<DimCommand>(); }, "assets/ribbon/dim.svg",
        "Create a dimension suited to the selected object.");
    reg({"LEADER"}, [] { return std::make_unique<LeaderCommand>(); }, "assets/ribbon/leader.svg",
        "Draw a leader line with an arrowhead and annotation.");
    // --- Dimension chaining (issue #28) ---
    reg({"DCO", "DIMCONTINUE"}, [] { return std::make_unique<ChainDimCommand>(false); },
        "assets/ribbon/dimcontinue.svg",
        "Continue a dimension chain from the last dimension's second extension line.");
    reg({"DBA", "DIMBASELINE"}, [] { return std::make_unique<ChainDimCommand>(true); },
        "assets/ribbon/dimbaseline.svg",
        "Stack dimensions from a common first extension line.");

    // --- Inquiry (issue #30) ---
    reg({"DI", "DIST"}, [] { return std::make_unique<DistCommand>(); },
        "assets/ribbon/measure.svg", "Measure the distance and angle between two points.");
    reg({"ID"}, [] { return std::make_unique<IdCommand>(); }, "assets/ribbon/measure.svg",
        "Report the coordinates of a point.");
    reg({"AA", "AREA"}, [] { return std::make_unique<AreaCommand>(); },
        "assets/ribbon/measure.svg", "Report the area and perimeter of an object.");
    reg({"LI", "LIST"}, [] { return std::make_unique<ListCommand>(); },
        "assets/ribbon/list.svg", "List an object's type, layer and defining parameters.");

    reg({"S", "STRETCH"}, [] { return std::make_unique<StretchCommand>(); },
        "assets/ribbon/stretch.svg",
        "Move the parts of objects inside a crossing window, keeping the rest anchored.");

    reg({"TB", "TABLE"}, [] { return std::make_unique<TableCommand>(); },
        "assets/ribbon/table.svg",
        "Insert a table of rows and columns for a BOM, revision block or parts list.");

    // --- GD&T (issue #8) ---
    reg({"TOL", "TOLERANCE"}, [] { return std::make_unique<ToleranceCommand>(); },
        "assets/ribbon/tolerance.svg",
        "Create a feature control frame with geometric tolerance symbols.");
    reg({"DATUM", "DIMDATUM"}, [] { return std::make_unique<DatumCommand>(); },
        "assets/ribbon/datum.svg", "Place a datum feature symbol with its leader triangle.");
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
