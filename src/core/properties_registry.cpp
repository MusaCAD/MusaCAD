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

double get_height(const Command& c) {
    double v = 0.0;
    std::visit(
        [&](const auto& x) {
            if constexpr (requires { x.height; }) {
                v = x.height;
            } else if constexpr (requires { x.block.height; }) {
                v = x.block.height;
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
                x.block.height = v;
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
            if constexpr (requires { x.block; }) {
                v = get(x.block);
            }
        },
        c);
    return v;
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
    {PropertyId::TextContent, "Text", "Contents", PropEditor::TextContentEdit, is_text,
     [](const Command& c) {
         PropertyValue v;
         v.text = get_content(c);
         return v;
     },
     [](Command& c, const PropertyValue& v) { set_content(c, v.text); }},
    {PropertyId::TextHeight, "Text", "Height", PropEditor::Number, is_text,
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
    {PropertyId::TextFont, "Text", "Font", PropEditor::FontCombo, is_text,
     [](const Command&) {
         PropertyValue v;
         v.choice = 0;
         return v;
     },
     nullptr}, // read-only: single stroke font today (font system is staged)
    {PropertyId::MtWidthFactor, "Text", "Width factor", PropEditor::Number, is_mtext_only,
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
    {PropertyId::MtLineSpacing, "Text", "Line spacing", PropEditor::Number, is_mtext_only,
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
    {PropertyId::MtAttach, "Text", "Attachment", PropEditor::AttachCombo, is_mtext_only,
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
