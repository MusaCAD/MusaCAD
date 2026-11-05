// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/linetype.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>

#include "musacad/core/text/stroke_font.hpp"

namespace musacad::core {

namespace {
// Base patterns in drawing units (see header for the AutoCAD-proportion rationale).
constexpr std::array<double, 2> kDashed = {5.0, 2.5};
constexpr std::array<double, 2> kHidden = {2.5, 1.25};
constexpr std::array<double, 4> kCenter = {12.5, 2.5, 2.5, 2.5};
} // namespace

std::span<const double> dash_pattern(Linetype lt) noexcept {
    switch (lt) {
    case Linetype::Dashed:
        return kDashed;
    case Linetype::Hidden:
        return kHidden;
    case Linetype::Center:
        return kCenter;
    case Linetype::Continuous:
        break;
    }
    return {};
}

void dash_polyline(std::span<const Vec2> pts, Linetype lt, double ltscale,
                   std::vector<Vec2>& out) {
    const std::span<const double> pattern = dash_pattern(lt);
    // Continuous, degenerate scale, or too few points -> copy every segment as-is.
    if (pattern.empty() || ltscale <= 0.0 || pts.size() < 2) {
        for (std::size_t s = 1; s < pts.size(); ++s) {
            out.push_back(pts[s - 1]);
            out.push_back(pts[s]);
        }
        return;
    }

    // Walk arc-length, maintaining the current pattern element, how much of it is
    // left, and whether the pen is down (dash) or up (gap). The phase is continuous
    // across polyline vertices, so curves dash by true arc-length.
    std::size_t pi = 0;
    double remaining = pattern[0] * ltscale;
    bool pen_down = true; // even indices are "on"
    for (std::size_t s = 1; s < pts.size(); ++s) {
        const Vec2 a = pts[s - 1];
        const Vec2 b = pts[s];
        const Vec2 d = b - a;
        const double seg_len = length(d);
        if (seg_len < 1e-12) {
            continue;
        }
        const Vec2 dir = d / seg_len;
        double pos = 0.0;
        while (pos < seg_len - 1e-12) {
            const double step = std::min(remaining, seg_len - pos);
            if (pen_down) {
                out.push_back(a + dir * pos);
                out.push_back(a + dir * (pos + step));
            }
            pos += step;
            remaining -= step;
            if (remaining <= 1e-9) {
                pi = (pi + 1) % pattern.size();
                remaining = pattern[pi] * ltscale;
                pen_down = !pen_down;
            }
        }
    }
}

namespace {
// A complex linetype's text where the walk is at `at`, the line running `dir` there.
void emit_linetype_text(const LinetypeText& t, Vec2 at, Vec2 dir, double ltscale, std::vector<Vec2>& out) {
    const double h = t.scale * ltscale;
    if (t.text.empty() || !(h > 0.0)) {
        return;
    }
    const Vec2 across{-dir.y, dir.x};
    Vec2 origin = at + dir * (t.x * ltscale) + across * (t.y * ltscale);
    double rot = t.absolute ? t.rotation : std::atan2(dir.y, dir.x) + t.rotation;
    if (t.upright) {
        const Vec2 u{std::cos(rot), std::sin(rot)};
        if (u.x < -1e-9 || (std::abs(u.x) <= 1e-9 && u.y < 0.0)) {
            // Turned half round about its middle: in the same place, reading from the left.
            origin = origin + u * text::text_width(t.text, h) + Vec2{-u.y, u.x} * h;
            rot += kPi;
        }
    }
    text::append_text_segments(t.text, origin, h, rot, text::Justify::Left, out);
}

// Walks `pts` with a .lin pattern: positive a dash, negative a gap, 0 a dot (drawn as a dash a
// fiftieth of the pattern long, so it shows at any zoom the pattern does), and draws `texts`
// where their elements end. An empty or zero-length pattern draws solid.
void dash_lin(std::span<const Vec2> pts, const std::vector<double>* pattern, std::span<const LinetypeText> texts,
              double ltscale, std::vector<Vec2>& out) {
    double cycle = 0.0;
    if (pattern != nullptr) {
        for (const double e : *pattern) {
            cycle += std::abs(e);
        }
    }
    if (pattern == nullptr || pattern->empty() || !(cycle > 1e-12) || ltscale <= 0.0 || pts.size() < 2) {
        dash_polyline(pts, Linetype::Continuous, ltscale, out);
        return;
    }
    const double dot = cycle * ltscale / 50.0;
    const auto len_of = [&](std::size_t k) {
        const double e = (*pattern)[k];
        return e == 0.0 ? dot : std::abs(e) * ltscale;
    };
    std::size_t k = 0;
    double remaining = len_of(0);
    bool pen = (*pattern)[0] >= 0.0;
    bool started = false;
    const auto texts_at = [&](std::size_t after, Vec2 at, Vec2 dir) {
        for (const LinetypeText& t : texts) {
            if (t.after == after) {
                emit_linetype_text(t, at, dir, ltscale, out);
            }
        }
    };
    for (std::size_t s = 1; s < pts.size(); ++s) {
        const Vec2 a = pts[s - 1];
        const Vec2 d = pts[s] - a;
        const double seg_len = length(d);
        if (seg_len < 1e-12) {
            continue;
        }
        const Vec2 dir = d / seg_len;
        if (!started && !texts.empty()) {
            texts_at(0, a, dir);
        }
        started = true;
        double pos = 0.0;
        while (pos < seg_len - 1e-12) {
            const double step = std::min(remaining, seg_len - pos);
            if (pen) {
                out.push_back(a + dir * pos);
                out.push_back(a + dir * (pos + step));
            }
            pos += step;
            remaining -= step;
            if (remaining <= 1e-9) {
                if (!texts.empty()) {
                    texts_at(k + 1, a + dir * pos, dir);
                    if (k + 1 == pattern->size()) {
                        texts_at(0, a + dir * pos, dir); // the next repeat starts
                    }
                }
                k = (k + 1) % pattern->size();
                remaining = len_of(k);
                pen = (*pattern)[k] >= 0.0;
            }
        }
    }
}
} // namespace

void dash_polyline(std::span<const Vec2> pts, Linetype lt, std::span<const LinetypeDef> loaded, double ltscale,
                   std::vector<Vec2>& out) {
    dash_polyline(pts, lt, legacy_builtin_linetypes(), loaded, ltscale, out);
}

void dash_polyline(std::span<const Vec2> pts, Linetype lt, const BuiltinLinetypes& builtin,
                   std::span<const LinetypeDef> loaded, double ltscale, std::vector<Vec2>& out) {
    const auto v = static_cast<std::size_t>(lt);
    if (v == 0) {
        dash_polyline(pts, Linetype::Continuous, ltscale, out);
        return;
    }
    if (v < kFirstLoadedLinetype) {
        // Dashed 1, Center 2, Hidden 3 -- the BuiltinLinetypes order is Dashed, Center, Hidden.
        const std::size_t b = lt == Linetype::Dashed ? 0 : lt == Linetype::Center ? 1 : 2;
        dash_lin(pts, &builtin[b], {}, ltscale, out);
        return;
    }
    const std::size_t i = v - kFirstLoadedLinetype;
    if (i < loaded.size()) {
        dash_lin(pts, &loaded[i].pattern, loaded[i].texts, ltscale, out);
    } else {
        dash_lin(pts, nullptr, {}, ltscale, out);
    }
}

const BuiltinLinetypes& legacy_builtin_linetypes() {
    static const BuiltinLinetypes kLegacy = {std::vector<double>{5.0, -2.5}, std::vector<double>{12.5, -2.5, 2.5, -2.5},
                                             std::vector<double>{2.5, -1.25}};
    return kLegacy;
}

const BuiltinLinetypes& standard_builtin_linetypes(bool metric) {
    // acad.lin: DASHED .5,-.25; CENTER 1.25,-.25,.25,-.25; HIDDEN .25,-.125 (inches).
    // acadiso.lin: the same in millimetres (x 25.4).
    static const BuiltinLinetypes kInch = {std::vector<double>{0.5, -0.25}, std::vector<double>{1.25, -0.25, 0.25, -0.25},
                                           std::vector<double>{0.25, -0.125}};
    static const BuiltinLinetypes kIso = {std::vector<double>{12.7, -6.35}, std::vector<double>{31.75, -6.35, 6.35, -6.35},
                                          std::vector<double>{6.35, -3.175}};
    return metric ? kIso : kInch;
}

const char* builtin_linetype_name(Linetype lt, bool dxf) noexcept {
    switch (lt) {
    case Linetype::Dashed:
        return dxf ? "DASHED" : "Dashed";
    case Linetype::Center:
        return dxf ? "CENTER" : "Center";
    case Linetype::Hidden:
        return dxf ? "HIDDEN" : "Hidden";
    case Linetype::Continuous:
        break;
    }
    return "Continuous";
}

namespace {
// A .lin text element's insides -- `"GAS",STANDARD,S=.1,U=0.0,X=-0.1,Y=-.05` -- or nothing for
// a shape's (`ZIG,ltypeshp.shx,...`). Angles are in degrees unless they end in r (radians) or
// g (grads).
std::optional<LinetypeText> parse_lin_text(std::string_view in) {
    std::vector<std::string> fields;
    std::string f;
    bool quoted = false;
    for (const char c : in) {
        if (c == '"') {
            quoted = !quoted;
            f += c;
        } else if (c == ',' && !quoted) {
            fields.push_back(f);
            f.clear();
        } else {
            f += c;
        }
    }
    fields.push_back(f);
    for (std::string& s : fields) {
        const std::size_t b = s.find_first_not_of(" \t");
        const std::size_t e = s.find_last_not_of(" \t");
        s = b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
    }
    if (fields.size() < 2 || fields[0].size() < 2 || fields[0].front() != '"' || fields[0].back() != '"') {
        return std::nullopt;
    }
    LinetypeText t;
    t.text = fields[0].substr(1, fields[0].size() - 2);
    t.style = fields[1];
    for (std::size_t k = 2; k < fields.size(); ++k) {
        const std::string& s = fields[k];
        const std::size_t eq = s.find('=');
        if (eq == std::string::npos || eq == 0) {
            continue;
        }
        const char key = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
        const std::string value = s.substr(eq + 1);
        char* end = nullptr;
        const double v = std::strtod(value.c_str(), &end);
        if (end == value.c_str()) {
            continue;
        }
        const char unit = static_cast<char>(std::tolower(static_cast<unsigned char>(*end)));
        const double angle = unit == 'r' ? v : unit == 'g' ? v * kPi / 200.0 : v * kPi / 180.0;
        switch (key) {
        case 'S':
            t.scale = v;
            break;
        case 'R':
            t.rotation = angle;
            break;
        case 'A':
            t.rotation = angle;
            t.absolute = true;
            break;
        case 'U':
            t.rotation = angle;
            t.upright = true;
            break;
        case 'X':
            t.x = v;
            break;
        case 'Y':
            t.y = v;
            break;
        default:
            break;
        }
    }
    return t;
}
} // namespace

std::vector<LinetypeDef> parse_lin(std::string_view text) {
    std::vector<LinetypeDef> out;
    std::size_t at = 0;
    bool want_pattern = false;
    while (at < text.size()) {
        std::size_t nl = text.find('\n', at);
        if (nl == std::string_view::npos) {
            nl = text.size();
        }
        std::string line(text.substr(at, nl - at));
        at = nl + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        const std::size_t lead = line.find_first_not_of(" \t");
        if (lead == std::string::npos || line.compare(lead, 2, ";;") == 0) {
            continue;
        }
        line.erase(0, lead);
        if (line.front() == '*') {
            const std::size_t comma = line.find(',');
            LinetypeDef d;
            d.name = line.substr(1, comma == std::string::npos ? std::string::npos : comma - 1);
            d.description = comma == std::string::npos ? std::string() : line.substr(comma + 1);
            while (!d.name.empty() && d.name.back() == ' ') {
                d.name.pop_back();
            }
            out.push_back(std::move(d));
            want_pattern = true;
            continue;
        }
        if (!want_pattern || out.empty() || (line.front() != 'A' && line.front() != 'a')) {
            continue;
        }
        want_pattern = false;
        // A,.5,-.25,["GAS",STANDARD,S=.1,...],-.25: the numbers, and the text a [...] element
        // draws (a shape's left out).
        std::size_t i = line.find(',');
        int depth = 0;
        bool quoted = false;
        std::string item;
        std::string element;
        const auto take = [&] {
            const std::size_t b = item.find_first_not_of(" \t");
            if (b != std::string::npos) {
                char* end = nullptr;
                const double v = std::strtod(item.c_str() + b, &end);
                if (end != item.c_str() + b) {
                    out.back().pattern.push_back(v);
                }
            }
            item.clear();
        };
        for (i = i == std::string::npos ? line.size() : i + 1; i < line.size(); ++i) {
            const char c = line[i];
            if (depth > 0 && (quoted || c == '"')) {
                quoted = quoted != (c == '"');
                element += c;
            } else if (c == '[') {
                ++depth;
                item.clear();
                element.clear();
            } else if (c == ']') {
                depth = std::max(depth - 1, 0);
                item.clear();
                if (depth == 0) {
                    if (std::optional<LinetypeText> t = parse_lin_text(element)) {
                        t->after = static_cast<std::uint32_t>(out.back().pattern.size());
                        out.back().texts.push_back(std::move(*t));
                    }
                }
            } else if (depth > 0) {
                element += c;
            } else if (depth == 0 && c == ',') {
                take();
            } else if (depth == 0) {
                item += c;
            }
        }
        take();
    }
    out.erase(std::remove_if(out.begin(), out.end(), [](const LinetypeDef& d) { return d.name.empty(); }), out.end());
    return out;
}

const std::vector<LinetypeDef>& linetype_library(bool metric) {
    // acad.lin's sizes in inches; acadiso.lin has each 25.4 times as long. The 2 sizes are
    // half, the X2 sizes double (CENTER2's long dash is not half CENTER's, as acad.lin has it).
    const auto build = [](double unit) {
        struct Base {
            const char* name;
            const char* look;
            std::vector<double> pattern;
        };
        const std::vector<Base> bases = {
            {"BORDER", "Border __ __ . __ __ . __ __ .", {0.5, -0.25, 0.5, -0.25, 0.0, -0.25}},
            {"CENTER", "Center ____ _ ____ _ ____ _", {1.25, -0.25, 0.25, -0.25}},
            {"DASHDOT", "Dash dot __ . __ . __ . __ .", {0.5, -0.25, 0.0, -0.25}},
            {"DASHED", "Dashed __ __ __ __ __ __", {0.5, -0.25}},
            {"DIVIDE", "Divide ____ . . ____ . . ____", {0.5, -0.25, 0.0, -0.25, 0.0, -0.25}},
            {"DOT", "Dot . . . . . . . . . . . . .", {0.0, -0.25}},
            {"HIDDEN", "Hidden __ __ __ __ __ __ __", {0.25, -0.125}},
            {"PHANTOM", "Phantom ______ __ __ ______ __", {1.25, -0.25, 0.25, -0.25, 0.25, -0.25}},
        };
        struct Size {
            const char* suffix;
            double factor;
            const char* label;
        };
        static constexpr Size kSizes[] = {{"", 1.0, ""}, {"2", 0.5, " (.5x)"}, {"X2", 2.0, " (2x)"}};
        std::vector<LinetypeDef> all;
        for (const Base& b : bases) {
            for (const Size& z : kSizes) {
                LinetypeDef d;
                d.name = std::string(b.name) + z.suffix;
                d.description = std::string(b.look) + z.label;
                for (const double e : b.pattern) {
                    d.pattern.push_back(e * z.factor * unit);
                }
                if (d.name == "CENTER2") {
                    d.pattern = {0.75 * unit, -0.125 * unit, 0.125 * unit, -0.125 * unit};
                }
                all.push_back(std::move(d));
            }
        }
        // The ISO 128 linetypes, in millimetres in acad.lin and acadiso.lin alike.
        struct Iso {
            const char* name;
            const char* look;
            std::vector<double> pattern;
        };
        const std::vector<Iso> iso = {
            {"ACAD_ISO02W100", "ISO dash __ __ __ __ __ __ __ __ __ __ __ __ __", {12.0, -3.0}},
            {"ACAD_ISO03W100", "ISO dash space __    __    __    __    __    __", {12.0, -18.0}},
            {"ACAD_ISO04W100", "ISO long-dash dot ____ . ____ . ____ . ____ . _", {24.0, -3.0, 0.0, -3.0}},
            {"ACAD_ISO05W100", "ISO long-dash double-dot ____ .. ____ .. ____ . ", {24.0, -3.0, 0.0, -3.0, 0.0, -3.0}},
            {"ACAD_ISO06W100", "ISO long-dash triple-dot ____ ... ____ ... ____", {24.0, -3.0, 0.0, -3.0, 0.0, -3.0, 0.0, -3.0}},
            {"ACAD_ISO07W100", "ISO dot . . . . . . . . . . . . . . . . . . . . ", {0.0, -3.0}},
            {"ACAD_ISO08W100", "ISO long-dash short-dash ____ __ ____ __ ____ _", {24.0, -3.0, 6.0, -3.0}},
            {"ACAD_ISO09W100", "ISO long-dash double-short-dash ____ __ __ ____", {24.0, -3.0, 6.0, -3.0, 6.0, -3.0}},
            {"ACAD_ISO10W100", "ISO dash dot __ . __ . __ . __ . __ . __ . __ . ", {12.0, -3.0, 0.0, -3.0}},
            {"ACAD_ISO11W100", "ISO double-dash dot __ __ . __ __ . __ __ . __ _", {12.0, -3.0, 12.0, -3.0, 0.0, -3.0}},
            {"ACAD_ISO12W100", "ISO dash double-dot __ . . __ . . __ . . __ . .", {12.0, -3.0, 0.0, -3.0, 0.0, -3.0}},
            {"ACAD_ISO13W100", "ISO double-dash double-dot __ __ . . __ __ . . _", {12.0, -3.0, 12.0, -3.0, 0.0, -3.0, 0.0, -3.0}},
            {"ACAD_ISO14W100", "ISO dash triple-dot __ . . . __ . . . __ . . . _", {12.0, -3.0, 0.0, -3.0, 0.0, -3.0, 0.0, -3.0}},
            {"ACAD_ISO15W100", "ISO double-dash triple-dot __ __ . . . __ __ . .",
             {12.0, -3.0, 12.0, -3.0, 0.0, -3.0, 0.0, -3.0, 0.0, -3.0}},
        };
        for (const Iso& s : iso) {
            all.push_back(LinetypeDef{s.name, s.look, s.pattern});
        }
        // The two with text, as acad.lin has them (acadiso.lin's 25.4 times as big):
        // A,.5,-.2,["GAS",STANDARD,S=.1,U=0.0,X=-0.1,Y=-.05],-.25.
        const auto with_text = [&](const char* name, const char* look, const char* text, double last) {
            LinetypeDef d;
            d.name = name;
            d.description = look;
            d.pattern = {0.5 * unit, -0.2 * unit, last * unit};
            LinetypeText t;
            t.after = 2;
            t.text = text;
            t.scale = 0.1 * unit;
            t.upright = true;
            t.x = -0.1 * unit;
            t.y = -0.05 * unit;
            d.texts.push_back(std::move(t));
            all.push_back(std::move(d));
        };
        with_text("GAS_LINE", "Gas line ----GAS----GAS----GAS----GAS----GAS----GAS--", "GAS", -0.25);
        with_text("HOT_WATER_SUPPLY", "Hot water supply ---- HW ---- HW ---- HW ----", "HW", -0.2);
        return all;
    };
    static const std::vector<LinetypeDef> kInch = build(1.0);
    static const std::vector<LinetypeDef> kIso = build(25.4);
    return metric ? kIso : kInch;
}

} // namespace musacad::core
