// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/properties_registry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <numbers>
#include <string>
#include <variant>

#include "musacad/core/math/math.hpp"

namespace musacad::core {

namespace {

constexpr double kRadToDeg = 180.0 / std::numbers::pi;
constexpr double kDegToRad = std::numbers::pi / 180.0;

// --- generic field accessors over the captured Add*Command --------------------
// Each uses a C++20 requires-expression so we never enumerate command types:
// the accessor simply applies to whichever command exposes the member. The
// descriptor's applies() gate decides whether the value is ever shown/written.

EntityProps get_props(const Command& c) {
    EntityProps p{};
    std::visit(
        [&](const auto& x) {
            if constexpr (requires { x.props; }) {
                if (x.props) {
                    p = *x.props;
                }
            }
        },
        c);
    return p;
}
void with_props(Command& c, const std::function<void(EntityProps&)>& fn) {
    std::visit(
        [&](auto& x) {
            if constexpr (requires { x.props; }) {
                if (!x.props) {
                    x.props = EntityProps{};
                }
                fn(*x.props);
            }
        },
        c);
}

// --- dimension per-dimension overrides + resolved-style snapshot ---------------
// Only AddDimensionCommand carries .overrides / .dim_style.
DimOverrides get_dim_ov(const Command& c) {
    DimOverrides o{};
    std::visit(
        [&](const auto& x) {
            if constexpr (requires { x.overrides; }) {
                o = x.overrides;
            }
        },
        c);
    return o;
}
DimStyle get_dim_style(const Command& c) {
    DimStyle s{};
    std::visit(
        [&](const auto& x) {
            if constexpr (requires { x.dim_style; }) {
                s = x.dim_style;
            }
        },
        c);
    return s;
}
void with_dim_ov(Command& c, const std::function<void(DimOverrides&)>& fn) {
    std::visit(
        [&](auto& x) {
            if constexpr (requires { x.overrides; }) {
                fn(x.overrides);
            }
        },
        c);
}

// --- dimension text decoration (issue #7) --------------------------------------
// Only AddDimensionCommand carries .prefix / .suffix / .tol, so the same
// requires-expression trick applies: the property "applies to whichever command
// exposes the member", with no per-type enumeration.
std::string get_dim_text_override(const Command& c) {
    std::string s;
    std::visit(
        [&](const auto& x) {
            if constexpr (requires { x.text_override; }) {
                s = x.text_override;
            }
        },
        c);
    return s;
}
void set_dim_text_override(Command& c, const std::string& v) {
    std::visit(
        [&](auto& x) {
            if constexpr (requires { x.text_override; }) {
                x.text_override = v;
            }
        },
        c);
}
/// Is the label displaced from its derived position (issue #21)?
bool get_dim_text_moved(const Command& c) {
    bool moved = false;
    std::visit(
        [&](const auto& x) {
            if constexpr (requires { x.text_offset; }) {
                moved = x.text_offset.x != 0.0 || x.text_offset.y != 0.0;
            }
        },
        c);
    return moved;
}
/// "Home text": clear the displacement so the label returns to where the geometry
/// (and the ISO 129-1 fit) puts it. AutoCAD spells this Text Position > Home text.
void set_dim_text_home(Command& c) {
    std::visit(
        [&](auto& x) {
            if constexpr (requires { x.text_offset; }) {
                x.text_offset = Vec2{};
            }
        },
        c);
}
std::string get_dim_decor_prefix(const Command& c) {
    std::string s;
    std::visit(
        [&](const auto& x) {
            if constexpr (requires { x.prefix; }) {
                s = x.prefix;
            }
        },
        c);
    return s;
}
std::string get_dim_decor_suffix(const Command& c) {
    std::string s;
    std::visit(
        [&](const auto& x) {
            if constexpr (requires { x.suffix; }) {
                s = x.suffix;
            }
        },
        c);
    return s;
}
void set_dim_decor_prefix(Command& c, const std::string& value) {
    std::visit(
        [&](auto& x) {
            if constexpr (requires { x.prefix; }) {
                x.prefix = value;
            }
        },
        c);
}
void set_dim_decor_suffix(Command& c, const std::string& value) {
    std::visit(
        [&](auto& x) {
            if constexpr (requires { x.suffix; }) {
                x.suffix = value;
            }
        },
        c);
}
DimTolerance get_dim_tol(const Command& c) {
    DimTolerance t{};
    std::visit(
        [&](const auto& x) {
            if constexpr (requires { x.tol; }) {
                t = x.tol;
            }
        },
        c);
    return t;
}
void with_dim_tol(Command& c, const std::function<void(DimTolerance&)>& fn) {
    std::visit(
        [&](auto& x) {
            if constexpr (requires { x.tol; }) {
                fn(x.tol);
            }
        },
        c);
}

// CELTSCALE rides as a top-level field on the linetype-dashing Add*Commands (not in
// props -- the store holds it sparsely). Commands without the field read 1.0 / no-op.
double get_celtscale(const Command& c) {
    double v = 1.0;
    std::visit(
        [&](const auto& x) {
            if constexpr (requires { x.celtscale; }) {
                v = x.celtscale;
            }
        },
        c);
    return v;
}
void set_celtscale(Command& c, double v) {
    std::visit(
        [&](auto& x) {
            if constexpr (requires { x.celtscale; }) {
                x.celtscale = v;
            }
        },
        c);
}

double get_height(const Command& c) {
    double v = 0.0;
    std::visit(
        [&](const auto& x) {
            if constexpr (requires { x.height; }) {
                v = x.height;
            } else if constexpr (requires { x.block.height; }) {
                v = x.block.height; // MTEXT + MLeader label
            } else if constexpr (requires { x.text_height; }) {
                v = x.text_height; // flat LEADER label
            }
        },
        c);
    return v;
}
void set_height(Command& c, double v) {
    std::visit(
        [&](auto& x) {
            if constexpr (requires { x.height; }) {
                x.height = v;
            } else if constexpr (requires { x.block.height; }) {
                x.block.height = v; // MTEXT + MLeader label
            } else if constexpr (requires { x.text_height; }) {
                x.text_height = v; // flat LEADER label
            }
        },
        c);
}

double get_rotation(const Command& c) {
    double v = 0.0;
    std::visit(
        [&](const auto& x) {
            if constexpr (requires { x.rotation; }) {
                v = x.rotation;
            } else if constexpr (requires { x.block.rotation; }) {
                v = x.block.rotation;
            }
        },
        c);
    return v;
}
void set_rotation(Command& c, double v) {
    std::visit(
        [&](auto& x) {
            if constexpr (requires { x.rotation; }) {
                x.rotation = v;
            } else if constexpr (requires { x.block.rotation; }) {
                x.block.rotation = v;
            }
        },
        c);
}

std::string get_content(const Command& c) {
    std::string s;
    std::visit(
        [&](const auto& x) {
            if constexpr (requires { x.content; }) {
                s = x.content;
            }
        },
        c);
    return s;
}
void set_content(Command& c, const std::string& s) {
    std::visit(
        [&](auto& x) {
            if constexpr (requires { x.content; }) {
                x.content = s;
            }
        },
        c);
}

int get_justify(const Command& c) {
    int v = 0;
    std::visit(
        [&](const auto& x) {
            if constexpr (requires { x.justify; }) {
                v = static_cast<int>(x.justify);
            }
        },
        c);
    return v;
}
void set_justify(Command& c, int v) {
    std::visit(
        [&](auto& x) {
            if constexpr (requires { x.justify; }) {
                x.justify = static_cast<std::uint8_t>(v);
            }
        },
        c);
}

// MTEXT block-only doubles + attach, addressed by a member-pointer-ish selector.
template <class Get>
double block_double(const Command& c, Get get) {
    double v = 0.0;
    std::visit(
        [&](const auto& x) {
            // .block.height distinguishes the MTextBlock-bearing commands from
            // AddInsertCommand, whose `block` is a uint16 definition index.
            if constexpr (requires { x.block.height; }) {
                v = get(x.block);
            }
        },
        c);
    return v;
}

// The font name of a text-bearing command ("" = the stroke font). Only AddTextCommand/
// AddLeaderCommand/AddMTextCommand carry a `.font` string.
std::string font_of(const Command& c) {
    std::string out;
    std::visit(
        [&](const auto& x) {
            if constexpr (requires { x.font; }) {
                out = x.font;
            }
        },
        c);
    return out;
}
/// Generic numeric field access for the editable geometry rows: `pick` returns a
/// pointer to the double inside the command (or nullptr for kinds without it).
template <class Pick>
PropertyValue read_field(const Command& c, Pick pick) {
    PropertyValue v;
    std::visit(
        [&](const auto& x) {
            if (const double* d = pick(x)) {
                v.num = *d;
            }
        },
        c);
    return v;
}
template <class Pick>
void write_field(Command& c, double value, Pick pick) {
    std::visit(
        [&](auto& x) {
            if (double* d = pick(x)) {
                *d = value;
            }
        },
        c);
}

void set_font(Command& c, const std::string& name) {
    std::visit(
        [&](auto& x) {
            if constexpr (requires { x.font; }) {
                x.font = name;
            }
        },
        c);
}

/// TEXT only: the style NAME ("" = Standard). Setting a style clears the explicit font
/// so the style's font applies (the style is the source of truth, as in AutoCAD).
std::string text_style_of(const Command& c) {
    if (const auto* t = std::get_if<AddTextCommand>(&c)) {
        return t->style;
    }
    return {};
}
void set_text_style(Command& c, const std::string& name) {
    if (auto* t = std::get_if<AddTextCommand>(&c)) {
        t->style = name;
        t->font.clear();
    }
}

std::string fmt(double v) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.4g", v);
    return buf;
}
std::string fmt_pt(Vec2 p) {
    return "(" + fmt(p.x) + ", " + fmt(p.y) + ")";
}

// --- geometry (read-only) accessors -----------------------------------------
PropertyValue read_text_pt(const Command& c, bool /*unused*/) {
    PropertyValue v;
    std::visit(
        [&](const auto& x) {
            if constexpr (requires { x.pos; }) {
                v.text = fmt_pt(x.pos);
            } else if constexpr (requires { x.block.pos; }) {
                v.text = fmt_pt(x.block.pos);
            }
        },
        c);
    return v;
}

// --- the descriptor table ----------------------------------------------------
struct Desc {
    PropertyId id;
    const char* group;
    const char* label;
    PropEditor editor;
    bool (*applies)(EntityKind);
    PropertyValue (*read)(const Command&);
    void (*write)(Command&, const PropertyValue&); ///< nullptr => read-only
};

bool any_kind(EntityKind) { return true; }
bool is_line(EntityKind k) { return k == EntityKind::Line; }
bool is_circular(EntityKind k) { return k == EntityKind::Circle || k == EntityKind::Arc; }
bool is_centered(EntityKind k) { return is_circular(k) || k == EntityKind::Ellipse; }
bool is_fcf(EntityKind k) { return k == EntityKind::Fcf; }
bool is_datum(EntityKind k) { return k == EntityKind::Datum; }
bool is_text(EntityKind k) { return k == EntityKind::Text || k == EntityKind::MText; }
bool is_text_only(EntityKind k) { return k == EntityKind::Text; }
bool is_mtext_only(EntityKind k) { return k == EntityKind::MText; }
// The whole Text family -- single-line TEXT, paragraph MTEXT, and the Leader/MLeader
// LABELS (both carry a font + height; MLeader's label is a full MTextBlock). Their label
// text props (font, height, content) are registry-exposed and MATCHPROP-matchable here.
bool is_text_family(EntityKind k) {
    return k == EntityKind::Text || k == EntityKind::MText || k == EntityKind::Leader ||
           k == EntityKind::MLeader;
}
// Paragraph-block text: MTEXT and the MLeader label (both own an MTextBlock, so width
// factor / line spacing / attachment apply). The flat LEADER label has none of these.
bool is_paragraph(EntityKind k) { return k == EntityKind::MText || k == EntityKind::MLeader; }
// Both leader kinds carry a per-leader arrow override (the dimstyle-arrow override model).
bool is_leader_arrow(EntityKind k) {
    return k == EntityKind::Leader || k == EntityKind::MLeader;
}
bool is_hatch(EntityKind k) { return k == EntityKind::Hatch; }
bool is_dimension(EntityKind k) { return k == EntityKind::Dimension; }
/// Dimensions AND GD&T. These three properties are what "GD&T annotation matches the
/// drawing's dimensions automatically" actually means: a frame's whole size derives
/// from its text height, and its border/text colours resolve like a dimension's.
bool is_dim_or_gdt(EntityKind k) {
    return k == EntityKind::Dimension || k == EntityKind::Fcf || k == EntityKind::Datum;
}
/// The datum symbol has a filled triangle sized by the style's arrow size; a feature
/// control frame has no arrowhead at all, so it is deliberately excluded.
bool is_dim_or_datum(EntityKind k) {
    return k == EntityKind::Dimension || k == EntityKind::Datum;
}
// Entities whose linetype actually dashes (so a per-entity linetype scale matters). These
// are the kinds whose Add*Command carries a celtscale field + go through dash_polyline.
bool is_linetypeable(EntityKind k) {
    return k == EntityKind::Line || k == EntityKind::Circle || k == EntityKind::Arc ||
           k == EntityKind::Polyline;
}

// Read a dim numeric override field: flag = ByStyle (no override); num = the
// effective value (override if set, else the resolved style value).
PropertyValue read_dim_num(const Command& c, DimOverrides::Bit bit, double ov_val,
                           double style_val) {
    PropertyValue v;
    const bool overridden = get_dim_ov(c).has(bit);
    v.flag = !overridden;
    v.num = overridden ? ov_val : style_val;
    return v;
}
// Read a dim colour override: flag = ByStyle; color = effective (override, else the
// style element colour -- shown as its explicit RGB, or white when the style
// element is ByLayer, a display approximation).
PropertyValue read_dim_color(const Command& c, DimOverrides::Bit bit, Rgb ov_col,
                             const ElementColor& style_col) {
    PropertyValue v;
    const bool overridden = get_dim_ov(c).has(bit);
    v.flag = !overridden;
    v.color = overridden ? ov_col : (style_col.by_layer ? Rgb{255, 255, 255} : style_col.color);
    return v;
}

// Deduced size -- the table is the single source of truth; range-for everywhere
// means no hand-maintained count to get wrong.
const Desc kDescs[] = {
    // -- General (universal) --
    {PropertyId::Layer, "General", "Layer", PropEditor::LayerCombo, any_kind,
     [](const Command& c) {
         PropertyValue v;
         v.choice = static_cast<int>(get_props(c).layer);
         return v;
     },
     [](Command& c, const PropertyValue& v) {
         with_props(c, [&](EntityProps& p) { p.layer = static_cast<std::uint16_t>(v.choice); });
     }},
    {PropertyId::Color, "General", "Color", PropEditor::ColorOverride, any_kind,
     [](const Command& c) {
         const EntityProps p = get_props(c);
         PropertyValue v;
         v.flag = p.color_by_layer();
         v.color = p.color;
         return v;
     },
     [](Command& c, const PropertyValue& v) {
         with_props(c, [&](EntityProps& p) {
             p.set_color_by_layer(v.flag);
             if (!v.flag) {
                 p.color = v.color;
             }
         });
     }},
    {PropertyId::Linetype, "General", "Linetype", PropEditor::LinetypeCombo, any_kind,
     [](const Command& c) {
         const EntityProps p = get_props(c);
         PropertyValue v;
         v.flag = p.linetype_by_layer();
         v.choice = static_cast<int>(p.linetype);
         return v;
     },
     [](Command& c, const PropertyValue& v) {
         with_props(c, [&](EntityProps& p) {
             p.set_linetype_by_layer(v.flag);
             if (!v.flag) {
                 p.linetype = static_cast<Linetype>(v.choice);
             }
         });
     }},
    {PropertyId::Celtscale, "General", "Linetype scale", PropEditor::Number, is_linetypeable,
     [](const Command& c) {
         PropertyValue v;
         v.num = get_celtscale(c);
         return v;
     },
     [](Command& c, const PropertyValue& v) { set_celtscale(c, v.num > 0.0 ? v.num : 1.0); }},
    {PropertyId::Lineweight, "General", "Lineweight", PropEditor::LineweightCombo, any_kind,
     [](const Command& c) {
         const EntityProps p = get_props(c);
         PropertyValue v;
         v.flag = p.lineweight_by_layer();
         v.num = static_cast<double>(p.lineweight);
         return v;
     },
     [](Command& c, const PropertyValue& v) {
         with_props(c, [&](EntityProps& p) {
             p.set_lineweight_by_layer(v.flag);
             if (!v.flag) {
                 p.lineweight = static_cast<std::uint8_t>(v.num);
             }
         });
     }},

    // -- Geometry (read-only) --
    {PropertyId::GeomLength, "Geometry", "Length", PropEditor::ReadOnly, is_line,
     [](const Command& c) {
         PropertyValue v;
         std::visit(
             [&](const auto& x) {
                 if constexpr (requires { x.a; x.b; }) {
                     v.text = fmt(length(x.b - x.a));
                 }
             },
             c);
         return v;
     },
     nullptr},
    {PropertyId::GeomStart, "Geometry", "Start", PropEditor::ReadOnly, is_line,
     [](const Command& c) {
         PropertyValue v;
         std::visit(
             [&](const auto& x) {
                 if constexpr (requires { x.a; }) {
                     v.text = fmt_pt(x.a);
                 }
             },
             c);
         return v;
     },
     nullptr},
    {PropertyId::GeomEnd, "Geometry", "End", PropEditor::ReadOnly, is_line,
     [](const Command& c) {
         PropertyValue v;
         std::visit(
             [&](const auto& x) {
                 if constexpr (requires { x.b; }) {
                     v.text = fmt_pt(x.b);
                 }
             },
             c);
         return v;
     },
     nullptr},
    {PropertyId::GeomCenter, "Geometry", "Center", PropEditor::ReadOnly, is_centered,
     [](const Command& c) {
         PropertyValue v;
         std::visit(
             [&](const auto& x) {
                 if constexpr (requires { x.center; }) {
                     v.text = fmt_pt(x.center);
                 }
             },
             c);
         return v;
     },
     nullptr},
    {PropertyId::GeomRadius, "Geometry", "Radius", PropEditor::Number, is_circular,
     [](const Command& c) {
         PropertyValue v;
         std::visit(
             [&](const auto& x) {
                 if constexpr (requires { x.radius; }) {
                     v.num = x.radius;
                 }
             },
             c);
         return v;
     },
     [](Command& c, const PropertyValue& v) {
         if (v.num > 0.0) {
             std::visit(
                 [&](auto& x) {
                     if constexpr (requires { x.radius; }) {
                         x.radius = v.num;
                     }
                 },
                 c);
         }
     }},
    {PropertyId::GeomPos, "Geometry", "Position", PropEditor::ReadOnly, is_text,
     [](const Command& c) { return read_text_pt(c, false); }, nullptr},
    // Editable coordinates (issue #32): one row per axis, like AutoCAD's Start X / Start
    // Y. The engine's apply path re-creates the entity from the edited command, so a
    // typed value moves the geometry exactly as a grip drag would.
    {PropertyId::GeomStartX, "Geometry", "Start X", PropEditor::Number, is_line,
     [](const Command& c) { return read_field(c, [](const auto& x) -> const double* {
         if constexpr (requires { x.a; }) { return &x.a.x; } else { return nullptr; } }); },
     [](Command& c, const PropertyValue& v) { write_field(c, v.num, [](auto& x) -> double* {
         if constexpr (requires { x.a; }) { return &x.a.x; } else { return nullptr; } }); }},
    {PropertyId::GeomStartY, "Geometry", "Start Y", PropEditor::Number, is_line,
     [](const Command& c) { return read_field(c, [](const auto& x) -> const double* {
         if constexpr (requires { x.a; }) { return &x.a.y; } else { return nullptr; } }); },
     [](Command& c, const PropertyValue& v) { write_field(c, v.num, [](auto& x) -> double* {
         if constexpr (requires { x.a; }) { return &x.a.y; } else { return nullptr; } }); }},
    {PropertyId::GeomEndX, "Geometry", "End X", PropEditor::Number, is_line,
     [](const Command& c) { return read_field(c, [](const auto& x) -> const double* {
         if constexpr (requires { x.b; }) { return &x.b.x; } else { return nullptr; } }); },
     [](Command& c, const PropertyValue& v) { write_field(c, v.num, [](auto& x) -> double* {
         if constexpr (requires { x.b; }) { return &x.b.x; } else { return nullptr; } }); }},
    {PropertyId::GeomEndY, "Geometry", "End Y", PropEditor::Number, is_line,
     [](const Command& c) { return read_field(c, [](const auto& x) -> const double* {
         if constexpr (requires { x.b; }) { return &x.b.y; } else { return nullptr; } }); },
     [](Command& c, const PropertyValue& v) { write_field(c, v.num, [](auto& x) -> double* {
         if constexpr (requires { x.b; }) { return &x.b.y; } else { return nullptr; } }); }},
    {PropertyId::GeomCenterX, "Geometry", "Center X", PropEditor::Number, is_centered,
     [](const Command& c) { return read_field(c, [](const auto& x) -> const double* {
         if constexpr (requires { x.center; }) { return &x.center.x; } else { return nullptr; } }); },
     [](Command& c, const PropertyValue& v) { write_field(c, v.num, [](auto& x) -> double* {
         if constexpr (requires { x.center; }) { return &x.center.x; } else { return nullptr; } }); }},
    {PropertyId::GeomCenterY, "Geometry", "Center Y", PropEditor::Number, is_centered,
     [](const Command& c) { return read_field(c, [](const auto& x) -> const double* {
         if constexpr (requires { x.center; }) { return &x.center.y; } else { return nullptr; } }); },
     [](Command& c, const PropertyValue& v) { write_field(c, v.num, [](auto& x) -> double* {
         if constexpr (requires { x.center; }) { return &x.center.y; } else { return nullptr; } }); }},
    {PropertyId::GeomPosX, "Geometry", "Position X", PropEditor::Number, is_text,
     [](const Command& c) { return read_field(c, [](const auto& x) -> const double* {
         if constexpr (requires { x.pos; }) { return &x.pos.x; } else { return nullptr; } }); },
     [](Command& c, const PropertyValue& v) { write_field(c, v.num, [](auto& x) -> double* {
         if constexpr (requires { x.pos; }) { return &x.pos.x; } else { return nullptr; } }); }},
    {PropertyId::GeomPosY, "Geometry", "Position Y", PropEditor::Number, is_text,
     [](const Command& c) { return read_field(c, [](const auto& x) -> const double* {
         if constexpr (requires { x.pos; }) { return &x.pos.y; } else { return nullptr; } }); },
     [](Command& c, const PropertyValue& v) { write_field(c, v.num, [](auto& x) -> double* {
         if constexpr (requires { x.pos; }) { return &x.pos.y; } else { return nullptr; } }); }},

    // -- Text / MTEXT --
    // GD&T (issue #32): the frame's cells as one editable line ("sym | 0.1 | A | B"), and
    // a datum symbol's letter. Cells are RAW (\\U+ codes stay), like TEXT contents.
    {PropertyId::FcfCells, "Tolerance", "Cells", PropEditor::TextContentEdit, is_fcf,
     [](const Command& c) {
         PropertyValue v;
         if (const auto* f = std::get_if<AddFcfCommand>(&c)) {
             for (std::size_t i = 0; i < f->cells.size(); ++i) {
                 v.text += (i > 0 ? " | " : "") + f->cells[i];
             }
         }
         return v;
     },
     [](Command& c, const PropertyValue& v) {
         if (auto* f = std::get_if<AddFcfCommand>(&c)) {
             std::vector<std::string> cells;
             std::string cur;
             for (std::size_t i = 0; i <= v.text.size(); ++i) {
                 if (i == v.text.size() || v.text[i] == '|') {
                     // trim
                     std::size_t a = 0;
                     std::size_t b = cur.size();
                     while (a < b && cur[a] == ' ') {
                         ++a;
                     }
                     while (b > a && cur[b - 1] == ' ') {
                         --b;
                     }
                     cells.push_back(cur.substr(a, b - a));
                     cur.clear();
                 } else {
                     cur += v.text[i];
                 }
             }
             if (!cells.empty()) {
                 f->cells = std::move(cells);
             }
         }
     }},
    {PropertyId::DatumLetter, "Tolerance", "Datum letter", PropEditor::TextContentEdit, is_datum,
     [](const Command& c) {
         PropertyValue v;
         if (const auto* d = std::get_if<AddDatumCommand>(&c)) {
             v.text = d->letter;
         }
         return v;
     },
     [](Command& c, const PropertyValue& v) {
         if (auto* d = std::get_if<AddDatumCommand>(&c)) {
             if (!v.text.empty()) {
                 d->letter = v.text;
             }
         }
     }},
    {PropertyId::TextContent, "Text", "Contents", PropEditor::TextContentEdit, is_text_family,
     [](const Command& c) {
         PropertyValue v;
         v.text = get_content(c);
         return v;
     },
     [](Command& c, const PropertyValue& v) { set_content(c, v.text); }},
    {PropertyId::TextHeight, "Text", "Height", PropEditor::Number, is_text_family,
     [](const Command& c) {
         PropertyValue v;
         v.num = get_height(c);
         return v;
     },
     [](Command& c, const PropertyValue& v) { set_height(c, v.num); }},
    {PropertyId::TextRotation, "Text", "Rotation (deg)", PropEditor::Number, is_text,
     [](const Command& c) {
         PropertyValue v;
         v.num = get_rotation(c) * kRadToDeg;
         return v;
     },
     [](Command& c, const PropertyValue& v) { set_rotation(c, v.num * kDegToRad); }},
    {PropertyId::TextJustify, "Text", "Justify", PropEditor::JustifyCombo, is_text_only,
     [](const Command& c) {
         PropertyValue v;
         v.choice = get_justify(c);
         return v;
     },
     [](Command& c, const PropertyValue& v) { set_justify(c, v.choice); }},
    {PropertyId::TextFont, "Text", "Font", PropEditor::FontCombo, is_text_family,
     [](const Command& c) {
         PropertyValue v;
         v.text = font_of(c); // current font name ("" = the stroke "Standard")
         return v;
     },
     [](Command& c, const PropertyValue& v) { set_font(c, v.text); }},
    {PropertyId::TextStyleName, "Text", "Style", PropEditor::StyleCombo, is_text_only,
     [](const Command& c) {
         PropertyValue v;
         v.text = text_style_of(c); // "" = Standard
         return v;
     },
     [](Command& c, const PropertyValue& v) { set_text_style(c, v.text); }},
    {PropertyId::MtWidthFactor, "Text", "Width factor", PropEditor::Number, is_paragraph,
     [](const Command& c) {
         PropertyValue v;
         v.num = block_double(c, [](const auto& b) { return b.width_factor; });
         return v;
     },
     [](Command& c, const PropertyValue& v) {
         std::visit(
             [&](auto& x) {
                 if constexpr (requires { x.block.width_factor; }) {
                     x.block.width_factor = v.num;
                 }
             },
             c);
     }},
    {PropertyId::MtLineSpacing, "Text", "Line spacing", PropEditor::Number, is_paragraph,
     [](const Command& c) {
         PropertyValue v;
         v.num = block_double(c, [](const auto& b) { return b.line_spacing; });
         return v;
     },
     [](Command& c, const PropertyValue& v) {
         std::visit(
             [&](auto& x) {
                 if constexpr (requires { x.block.line_spacing; }) {
                     x.block.line_spacing = v.num;
                 }
             },
             c);
     }},
    {PropertyId::MtWidth, "Text", "Defined width", PropEditor::Number, is_mtext_only,
     [](const Command& c) {
         PropertyValue v;
         v.num = block_double(c, [](const auto& b) { return b.width; });
         return v;
     },
     [](Command& c, const PropertyValue& v) {
         std::visit(
             [&](auto& x) {
                 if constexpr (requires { x.block.width; }) {
                     x.block.width = v.num;
                 }
             },
             c);
     }},
    {PropertyId::MtAttach, "Text", "Attachment", PropEditor::AttachCombo, is_paragraph,
     [](const Command& c) {
         PropertyValue v;
         std::visit(
             [&](const auto& x) {
                 if constexpr (requires { x.block.attach; }) {
                     v.choice = static_cast<int>(x.block.attach);
                 }
             },
             c);
         return v;
     },
     [](Command& c, const PropertyValue& v) {
         std::visit(
             [&](auto& x) {
                 if constexpr (requires { x.block.attach; }) {
                     x.block.attach = static_cast<std::uint8_t>(v.choice);
                 }
             },
             c);
     }},

    // The leader/MLeader LABEL colour: ByStyle (the leader's entity colour) unless overridden.
    // Distinct from the General colour, which drives the leader line + arrow.
    {PropertyId::LeaderTextColor, "Text", "Text color", PropEditor::ColorOverride, is_leader_arrow,
     [](const Command& c) {
         return read_dim_color(c, DimOverrides::kTextColor, get_dim_ov(c).text_color,
                               get_dim_style(c).text_color);
     },
     [](Command& c, const PropertyValue& v) {
         with_dim_ov(c, [&](DimOverrides& o) {
             o.set(DimOverrides::kTextColor, !v.flag);
             if (!v.flag) {
                 o.text_color = v.color;
             }
         });
     }},

    // -- Leader / MLeader arrow (per-leader override of the referenced dimstyle's arrow) --
    {PropertyId::LeaderArrowType, "Leader", "Arrowhead", PropEditor::DimArrowTypeCombo,
     is_leader_arrow,
     [](const Command& c) {
         PropertyValue v;
         const DimOverrides o = get_dim_ov(c);
         v.choice = o.has(DimOverrides::kArrowType) ? o.arrow_type + 1 : 0; // 0 = ByStyle
         return v;
     },
     [](Command& c, const PropertyValue& v) {
         with_dim_ov(c, [&](DimOverrides& o) {
             o.set(DimOverrides::kArrowType, v.choice > 0);
             if (v.choice > 0) {
                 o.arrow_type = static_cast<std::uint8_t>(v.choice - 1);
             }
         });
     }},
    {PropertyId::LeaderArrowSize, "Leader", "Arrow size", PropEditor::NumberOverride,
     is_leader_arrow,
     [](const Command& c) {
         return read_dim_num(c, DimOverrides::kArrowSize, get_dim_ov(c).arrow_size,
                             get_dim_style(c).arrow_size);
     },
     [](Command& c, const PropertyValue& v) {
         with_dim_ov(c, [&](DimOverrides& o) {
             o.set(DimOverrides::kArrowSize, !v.flag);
             if (!v.flag) {
                 o.arrow_size = v.num;
             }
         });
     }},

    // -- Dimension (per-dimension overrides; ByStyle unless set) --
    {PropertyId::DimArrowType, "Dimension", "Arrowhead", PropEditor::DimArrowTypeCombo, is_dimension,
     [](const Command& c) {
         PropertyValue v;
         const DimOverrides o = get_dim_ov(c);
         v.choice = o.has(DimOverrides::kArrowType) ? o.arrow_type + 1 : 0; // 0 = ByStyle
         return v;
     },
     [](Command& c, const PropertyValue& v) {
         with_dim_ov(c, [&](DimOverrides& o) {
             o.set(DimOverrides::kArrowType, v.choice > 0);
             if (v.choice > 0) {
                 o.arrow_type = static_cast<std::uint8_t>(v.choice - 1);
             }
         });
     }},
    {PropertyId::DimArrowSize, "Dimension", "Arrow size", PropEditor::NumberOverride, is_dim_or_datum,
     [](const Command& c) {
         return read_dim_num(c, DimOverrides::kArrowSize, get_dim_ov(c).arrow_size,
                             get_dim_style(c).arrow_size);
     },
     [](Command& c, const PropertyValue& v) {
         with_dim_ov(c, [&](DimOverrides& o) {
             o.set(DimOverrides::kArrowSize, !v.flag);
             if (!v.flag) {
                 o.arrow_size = v.num;
             }
         });
     }},
    {PropertyId::DimDimColor, "Dimension", "Line color", PropEditor::ColorOverride, is_dim_or_gdt,
     [](const Command& c) {
         return read_dim_color(c, DimOverrides::kDimColor, get_dim_ov(c).dim_color,
                               get_dim_style(c).dim_color);
     },
     [](Command& c, const PropertyValue& v) {
         with_dim_ov(c, [&](DimOverrides& o) {
             o.set(DimOverrides::kDimColor, !v.flag);
             if (!v.flag) {
                 o.dim_color = v.color;
             }
         });
     }},
    {PropertyId::DimExtColor, "Dimension", "Ext line color", PropEditor::ColorOverride, is_dimension,
     [](const Command& c) {
         return read_dim_color(c, DimOverrides::kExtColor, get_dim_ov(c).ext_color,
                               get_dim_style(c).ext_color);
     },
     [](Command& c, const PropertyValue& v) {
         with_dim_ov(c, [&](DimOverrides& o) {
             o.set(DimOverrides::kExtColor, !v.flag);
             if (!v.flag) {
                 o.ext_color = v.color;
             }
         });
     }},
    {PropertyId::DimTextHeight, "Dimension", "Text height", PropEditor::NumberOverride, is_dim_or_gdt,
     [](const Command& c) {
         return read_dim_num(c, DimOverrides::kTextHeight, get_dim_ov(c).text_height,
                             get_dim_style(c).text_height);
     },
     [](Command& c, const PropertyValue& v) {
         with_dim_ov(c, [&](DimOverrides& o) {
             o.set(DimOverrides::kTextHeight, !v.flag);
             if (!v.flag) {
                 o.text_height = v.num;
             }
         });
     }},
    {PropertyId::DimTextColor, "Dimension", "Text color", PropEditor::ColorOverride, is_dim_or_gdt,
     [](const Command& c) {
         return read_dim_color(c, DimOverrides::kTextColor, get_dim_ov(c).text_color,
                               get_dim_style(c).text_color);
     },
     [](Command& c, const PropertyValue& v) {
         with_dim_ov(c, [&](DimOverrides& o) {
             o.set(DimOverrides::kTextColor, !v.flag);
             if (!v.flag) {
                 o.text_color = v.color;
             }
         });
     }},
    {PropertyId::DimTextPlacement, "Dimension", "Text placement", PropEditor::DimPlacementCombo,
     is_dimension,
     [](const Command& c) {
         PropertyValue v;
         const DimOverrides o = get_dim_ov(c);
         v.choice = o.has(DimOverrides::kTextAbove) ? (o.text_above ? 1 : 2) : 0; // 0 = ByStyle
         return v;
     },
     [](Command& c, const PropertyValue& v) {
         with_dim_ov(c, [&](DimOverrides& o) {
             o.set(DimOverrides::kTextAbove, v.choice > 0);
             if (v.choice > 0) {
                 o.text_above = (v.choice == 1);
             }
         });
     }},
    {PropertyId::DimTextFit, "Dimension", "Text fit", PropEditor::DimTextFitCombo, is_dimension,
     [](const Command& c) {
         PropertyValue v;
         const DimOverrides o = get_dim_ov(c);
         // 0 = ByStyle; otherwise TextFit + 1 (Auto/Inside/Outside), the same
         // "index 0 means inherit" shape LinetypeCombo and DimPlacementCombo use.
         v.choice = o.has(DimOverrides::kTextFit) ? static_cast<int>(o.text_fit) + 1 : 0;
         return v;
     },
     [](Command& c, const PropertyValue& v) {
         with_dim_ov(c, [&](DimOverrides& o) {
             o.set(DimOverrides::kTextFit, v.choice > 0);
             if (v.choice > 0) {
                 o.text_fit = static_cast<std::uint8_t>(v.choice - 1);
             }
         });
     }},
    // -- Dimension text decoration (issue #7). Unlike the rows above these are NOT
    // ByStyle overrides: prefix/suffix/tolerance are the dimension's own content, so
    // they are plain values with no style to fall back to.
    // The text OVERRIDE (issue #20). `<>` expands to the measurement, so "<> H7" tracks
    // the geometry; an override with no `<>` deliberately replaces the value, which the
    // "Text moved/overridden" row below makes visible rather than silent.
    {PropertyId::DimTextOverride, "Dimension", "Text override", PropEditor::Text, is_dimension,
     [](const Command& c) {
         PropertyValue v;
         v.text = get_dim_text_override(c);
         return v;
     },
     [](Command& c, const PropertyValue& v) { set_dim_text_override(c, v.text); }},
    // Label displacement (issue #21). Read as a yes/no; WRITING anything resets it, which
    // is AutoCAD's "home text" -- there is no useful way to type a displacement, it is a
    // grip drag, so the row exists to make the state visible and undoable.
    {PropertyId::DimTextMoved, "Dimension", "Text moved", PropEditor::Bool, is_dimension,
     [](const Command& c) {
         PropertyValue v;
         v.flag = get_dim_text_moved(c);
         return v;
     },
     [](Command& c, const PropertyValue& v) {
         if (!v.flag) {
             set_dim_text_home(c);
         }
     }},
    {PropertyId::DimPrefix, "Dimension", "Text prefix", PropEditor::Text, is_dimension,
     [](const Command& c) {
         PropertyValue v;
         v.text = get_dim_decor_prefix(c);
         return v;
     },
     [](Command& c, const PropertyValue& v) { set_dim_decor_prefix(c, v.text); }},
    {PropertyId::DimSuffix, "Dimension", "Text suffix", PropEditor::Text, is_dimension,
     [](const Command& c) {
         PropertyValue v;
         v.text = get_dim_decor_suffix(c);
         return v;
     },
     [](Command& c, const PropertyValue& v) { set_dim_decor_suffix(c, v.text); }},
    {PropertyId::DimTolMode, "Dimension", "Tolerance", PropEditor::DimTolModeCombo, is_dimension,
     [](const Command& c) {
         PropertyValue v;
         v.choice = static_cast<int>(get_dim_tol(c).mode);
         return v;
     },
     [](Command& c, const PropertyValue& v) {
         with_dim_tol(c, [&](DimTolerance& t) {
             t.mode = static_cast<TolMode>(std::clamp(v.choice, 0, 4));
         });
     }},
    {PropertyId::DimTolUpper, "Dimension", "Upper deviation", PropEditor::Number, is_dimension,
     [](const Command& c) {
         PropertyValue v;
         v.num = get_dim_tol(c).upper;
         return v;
     },
     [](Command& c, const PropertyValue& v) {
         with_dim_tol(c, [&](DimTolerance& t) { t.upper = v.num; });
     }},
    {PropertyId::DimTolLower, "Dimension", "Lower deviation", PropEditor::Number, is_dimension,
     [](const Command& c) {
         PropertyValue v;
         v.num = get_dim_tol(c).lower;
         return v;
     },
     [](Command& c, const PropertyValue& v) {
         with_dim_tol(c, [&](DimTolerance& t) { t.lower = v.num; });
     }},
    {PropertyId::DimPrecision, "Dimension", "Precision", PropEditor::NumberOverride, is_dimension,
     [](const Command& c) {
         return read_dim_num(c, DimOverrides::kPrecision,
                             static_cast<double>(get_dim_ov(c).precision),
                             static_cast<double>(get_dim_style(c).precision));
     },
     [](Command& c, const PropertyValue& v) {
         with_dim_ov(c, [&](DimOverrides& o) {
             o.set(DimOverrides::kPrecision, !v.flag);
             if (!v.flag) {
                 o.precision = static_cast<std::uint8_t>(v.num < 0 ? 0 : v.num);
             }
         });
     }},

    // -- Hatch (pattern properties, family-scoped) --
    {PropertyId::HatchPattern, "Hatch", "Pattern", PropEditor::PatternCombo, is_hatch,
     [](const Command& c) {
         PropertyValue v;
         std::visit(
             [&](const auto& x) {
                 if constexpr (requires { x.pattern_name; }) {
                     v.text = x.pattern_name;
                 }
             },
             c);
         return v;
     },
     [](Command& c, const PropertyValue& v) {
         std::visit(
             [&](auto& x) {
                 if constexpr (requires { x.pattern_name; }) {
                     x.pattern_name = v.text;
                 }
             },
             c);
     }},
    {PropertyId::HatchScale, "Hatch", "Scale", PropEditor::Number, is_hatch,
     [](const Command& c) {
         PropertyValue v;
         std::visit(
             [&](const auto& x) {
                 if constexpr (requires { x.pattern_scale; }) {
                     v.num = x.pattern_scale;
                 }
             },
             c);
         return v;
     },
     [](Command& c, const PropertyValue& v) {
         std::visit(
             [&](auto& x) {
                 if constexpr (requires { x.pattern_scale; }) {
                     x.pattern_scale = v.num > 0.0 ? v.num : 1.0;
                 }
             },
             c);
     }},
    {PropertyId::HatchAngle, "Hatch", "Angle (deg)", PropEditor::Number, is_hatch,
     [](const Command& c) {
         PropertyValue v;
         std::visit(
             [&](const auto& x) {
                 if constexpr (requires { x.pattern_angle; }) {
                     v.num = x.pattern_angle * kRadToDeg;
                 }
             },
             c);
         return v;
     },
     [](Command& c, const PropertyValue& v) {
         std::visit(
             [&](auto& x) {
                 if constexpr (requires { x.pattern_angle; }) {
                     x.pattern_angle = v.num * kDegToRad;
                 }
             },
             c);
     }},
    {PropertyId::HatchOrigin, "Hatch", "Origin", PropEditor::ReadOnly, is_hatch,
     [](const Command& c) {
         PropertyValue v;
         std::visit(
             [&](const auto& x) {
                 if constexpr (requires { x.pattern_origin; }) {
                     v.text = fmt_pt(x.pattern_origin);
                 }
             },
             c);
         return v;
     },
     nullptr},
};

const Desc* find_desc(PropertyId id) {
    for (const Desc& d : kDescs) {
        if (d.id == id) {
            return &d;
        }
    }
    return nullptr;
}

} // namespace

const char* kind_name(EntityKind k) noexcept {
    switch (k) {
    case EntityKind::Point:
        return "Point";
    case EntityKind::Xline:
        return "Construction line";
    case EntityKind::Ellipse:
        return "Ellipse";
    case EntityKind::Line:
        return "Line";
    case EntityKind::Circle:
        return "Circle";
    case EntityKind::Arc:
        return "Arc";
    case EntityKind::Polyline:
        return "Polyline";
    case EntityKind::Spline:
        return "Spline";
    case EntityKind::Text:
        return "Text";
    case EntityKind::AttDef:
        return "Attribute definition";
    case EntityKind::Table:
        return "Table";
    case EntityKind::Image:
        return "Image";
    case EntityKind::Fcf:
        return "Feature control frame";
    case EntityKind::Datum:
        return "Datum feature";
    case EntityKind::Dimension:
        return "Dimension";
    case EntityKind::Leader:
        return "Leader";
    case EntityKind::MText:
        return "MText";
    case EntityKind::MLeader:
        return "MLeader";
    case EntityKind::Insert:
        return "Block Reference";
    case EntityKind::Hatch:
        return "Hatch";
    }
    return "Entity";
}

namespace {

} // namespace

EntityKind kind_of(const Command& c) noexcept {
    EntityKind k = EntityKind::Line;
    std::visit(
        [&](const auto& x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, AddLineCommand>) {
                k = EntityKind::Line;
            } else if constexpr (std::is_same_v<T, AddCircleCommand>) {
                k = EntityKind::Circle;
            } else if constexpr (std::is_same_v<T, AddArcCommand>) {
                k = EntityKind::Arc;
            } else if constexpr (std::is_same_v<T, AddPolylineCommand>) {
                k = EntityKind::Polyline;
            } else if constexpr (std::is_same_v<T, AddAttDefCommand>) {
                k = EntityKind::AttDef;
            } else if constexpr (std::is_same_v<T, AddTextCommand>) {
                k = EntityKind::Text;
            } else if constexpr (std::is_same_v<T, AddDimensionCommand>) {
                k = EntityKind::Dimension;
            } else if constexpr (std::is_same_v<T, AddLeaderCommand>) {
                k = EntityKind::Leader;
            } else if constexpr (std::is_same_v<T, AddMTextCommand>) {
                k = EntityKind::MText;
            } else if constexpr (std::is_same_v<T, AddMLeaderCommand>) {
                k = EntityKind::MLeader;
            } else if constexpr (std::is_same_v<T, AddInsertCommand>) {
                k = EntityKind::Insert;
            } else if constexpr (std::is_same_v<T, AddHatchCommand>) {
                k = EntityKind::Hatch;
            } else if constexpr (std::is_same_v<T, AddFcfCommand>) {
                k = EntityKind::Fcf;
            } else if constexpr (std::is_same_v<T, AddDatumCommand>) {
                k = EntityKind::Datum;
            } else if constexpr (std::is_same_v<T, AddXlineCommand>) {
                k = EntityKind::Xline;
            } else if constexpr (std::is_same_v<T, AddEllipseCommand>) {
                k = EntityKind::Ellipse;
            } else if constexpr (std::is_same_v<T, AddSplineCommand>) {
                k = EntityKind::Spline;
            } else if constexpr (std::is_same_v<T, AddImageCommand>) {
                k = EntityKind::Image;
            } else if constexpr (std::is_same_v<T, AddTableCommand>) {
                k = EntityKind::Table;
            }
        },
        c);
    return k;
}

bool property_applies(PropertyId id, EntityKind kind) noexcept {
    const Desc* d = find_desc(id);
    return d != nullptr && d->applies(kind);
}

void write_property(Command& c, PropertyId id, const PropertyValue& value) {
    const Desc* d = find_desc(id);
    if (d == nullptr || d->write == nullptr || !d->applies(kind_of(c))) {
        return;
    }
    d->write(c, value);
}

namespace {
// The MATCHPROP category each property belongs to -- the single categorization table,
// kept here next to the descriptors it tags (MA reads it; it defines no table of its
// own). Content (TextContent), placement (TextRotation), the MTEXT wrap box (MtWidth)
// and read-only geometry are intentionally NOT matched, matching AutoCAD's MATCHPROP.
MatchSlot match_slot_for(PropertyId id) noexcept {
    switch (id) {
    case PropertyId::Color:
        return MatchSlot::Color;
    case PropertyId::Layer:
        return MatchSlot::Layer;
    case PropertyId::Lineweight:
        return MatchSlot::Lineweight;
    case PropertyId::Linetype:
        return MatchSlot::Linetype;
    case PropertyId::Celtscale:
        return MatchSlot::Celtscale;
    case PropertyId::TextHeight:
    case PropertyId::TextJustify:
    case PropertyId::TextFont:
    case PropertyId::TextStyleName:
    case PropertyId::MtWidthFactor:
    case PropertyId::MtLineSpacing:
    case PropertyId::MtAttach:
    case PropertyId::LeaderArrowType: // leader arrow + label colour ride the text family
    case PropertyId::LeaderArrowSize:
    case PropertyId::LeaderTextColor:
        return MatchSlot::Text;
    case PropertyId::DimArrowType:
    case PropertyId::DimArrowSize:
    case PropertyId::DimDimColor:
    case PropertyId::DimExtColor:
    case PropertyId::DimTextHeight:
    case PropertyId::DimTextColor:
    case PropertyId::DimTextPlacement:
    case PropertyId::DimPrecision:
    // Text FIT is presentation (where the value sits when it will not fit), not
    // semantics, so unlike the decoration rows below it IS matchable -- copying it
    // says nothing untrue about the target feature.
    case PropertyId::DimTextFit:
        return MatchSlot::Dimension;
    // Text decoration is SEMANTICS, not presentation: a fit class, a limit pair or a
    // "6X" prefix describes THIS feature, and painting it onto another dimension with
    // MATCHPROP would silently assert something untrue about a different feature. Same
    // reasoning that makes TextContent unmatched. Deliberately MatchSlot::None.
    case PropertyId::DimTextOverride:
    case PropertyId::DimTextMoved:
    case PropertyId::DimPrefix:
    case PropertyId::DimSuffix:
    case PropertyId::DimTolMode:
    case PropertyId::DimTolUpper:
    case PropertyId::DimTolLower:
    case PropertyId::HatchPattern:
    case PropertyId::HatchScale:
    case PropertyId::HatchAngle:
        return MatchSlot::Hatch;
    default:
        return MatchSlot::None;
    }
}
} // namespace

int match_properties(const Command& source, Command& target, const MatchPropFilter& filter) {
    // ONE property-write path: reuse each descriptor's read()/write() (the same the PR
    // palette uses) -- no MA-specific entity-write logic. ByLayer/ByBlock travels as
    // state because the colour/linetype/lineweight write() functions set the flag and
    // only assign the literal when the flag is clear.
    const EntityKind sk = kind_of(source);
    const EntityKind tk = kind_of(target);
    int applied = 0;
    for (const Desc& d : kDescs) {
        if (d.write == nullptr) {
            continue;
        }
        const MatchSlot slot = match_slot_for(d.id);
        if (slot == MatchSlot::None || !filter.allows(slot)) {
            continue;
        }
        if (match_slot_universal(slot)) {
            if (!d.applies(tk)) { // universal applies to any kind; guard defensively
                continue;
            }
        } else {
            // Family-scoped: only when source and target share a family AND the
            // descriptor applies to both kinds (e.g. font copies Text<->MText).
            if (family_of(sk) != family_of(tk) || !d.applies(sk) || !d.applies(tk)) {
                continue;
            }
        }
        d.write(target, d.read(source));
        ++applied;
    }
    return applied;
}

SelectionSummary summarize_selection(const std::vector<Command>& captured) {
    SelectionSummary s;
    s.count = static_cast<int>(captured.size());
    if (captured.empty()) {
        return s;
    }
    const EntityKind first_kind = kind_of(captured.front());
    const EntityFamily first_family = family_of(first_kind);
    bool homogeneous = true;
    bool same_family = true;
    for (const Command& c : captured) {
        const EntityKind k = kind_of(c);
        if (k != first_kind) {
            homogeneous = false;
        }
        if (family_of(k) != first_family) {
            same_family = false;
        }
    }
    s.mixed = !homogeneous;
    // Family homogeneity drives the contextual ribbon tabs (e.g. TEXT + MTEXT are both the
    // Text family, so a mixed-kind but same-family selection still gets the Text editor).
    if (same_family) {
        s.family_plus1 = static_cast<std::uint8_t>(static_cast<int>(first_family) + 1);
    }
    if (homogeneous) {
        s.kind_plus1 = static_cast<std::uint8_t>(static_cast<int>(first_kind) + 1);
        s.type_label = s.count == 1 ? kind_name(first_kind)
                                    : std::to_string(s.count) + " " + kind_name(first_kind) + "s";
    } else {
        s.type_label = "Mixed (" + std::to_string(s.count) + ")";
    }

    for (const Desc& d : kDescs) {
        // A field is shown only if it applies to EVERY selected entity (universal
        // ids always do; type fields only for a homogeneous-enough selection).
        bool applies_all = true;
        for (const Command& c : captured) {
            if (!d.applies(kind_of(c))) {
                applies_all = false;
                break;
            }
        }
        if (!applies_all) {
            continue;
        }
        PropertyField f;
        f.id = d.id;
        f.group = d.group;
        f.label = d.label;
        f.editor = d.editor;
        f.value = d.read(captured.front());
        for (std::size_t i = 1; i < captured.size(); ++i) {
            if (!(d.read(captured[i]) == f.value)) {
                f.varies = true;
                break;
            }
        }
        s.fields.push_back(std::move(f));
    }
    return s;
}

} // namespace musacad::core
