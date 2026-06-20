// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/properties_registry.hpp"

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
void set_font(Command& c, const std::string& name) {
    std::visit(
        [&](auto& x) {
            if constexpr (requires { x.font; }) {
                x.font = name;
            }
        },
        c);
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
bool is_dimension(EntityKind k) { return k == EntityKind::Dimension; }
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
    {PropertyId::GeomCenter, "Geometry", "Center", PropEditor::ReadOnly, is_circular,
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
    {PropertyId::GeomRadius, "Geometry", "Radius", PropEditor::ReadOnly, is_circular,
     [](const Command& c) {
         PropertyValue v;
         std::visit(
             [&](const auto& x) {
                 if constexpr (requires { x.radius; }) {
                     v.text = fmt(x.radius);
                 }
             },
             c);
         return v;
     },
     nullptr},
    {PropertyId::GeomPos, "Geometry", "Position", PropEditor::ReadOnly, is_text,
     [](const Command& c) { return read_text_pt(c, false); }, nullptr},

    // -- Text / MTEXT --
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
    {PropertyId::DimArrowSize, "Dimension", "Arrow size", PropEditor::NumberOverride, is_dimension,
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
    {PropertyId::DimDimColor, "Dimension", "Dim line color", PropEditor::ColorOverride, is_dimension,
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
    {PropertyId::DimTextHeight, "Dimension", "Text height", PropEditor::NumberOverride, is_dimension,
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
    {PropertyId::DimTextColor, "Dimension", "Text color", PropEditor::ColorOverride, is_dimension,
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
};

const Desc* find_desc(PropertyId id) {
    for (const Desc& d : kDescs) {
        if (d.id == id) {
            return &d;
        }
    }
    return nullptr;
}

const char* kind_name(EntityKind k) {
    switch (k) {
    case EntityKind::Point:
        return "Point";
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
    }
    return "Entity";
}

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
        return MatchSlot::Dimension;
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
    bool homogeneous = true;
    for (const Command& c : captured) {
        if (kind_of(c) != first_kind) {
            homogeneous = false;
            break;
        }
    }
    s.mixed = !homogeneous;
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
