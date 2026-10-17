// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/linetype.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

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
// Walks `pts` with a .lin pattern: positive a dash, negative a gap, 0 a dot (drawn as a dash a
// fiftieth of the pattern long, so it shows at any zoom the pattern does). An empty or
// zero-length pattern draws solid.
void dash_lin(std::span<const Vec2> pts, const std::vector<double>* pattern, double ltscale, std::vector<Vec2>& out) {
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
    for (std::size_t s = 1; s < pts.size(); ++s) {
        const Vec2 a = pts[s - 1];
        const Vec2 d = pts[s] - a;
        const double seg_len = length(d);
        if (seg_len < 1e-12) {
            continue;
        }
        const Vec2 dir = d / seg_len;
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
        dash_lin(pts, &builtin[b], ltscale, out);
        return;
    }
    const std::size_t i = v - kFirstLoadedLinetype;
    dash_lin(pts, i < loaded.size() ? &loaded[i].pattern : nullptr, ltscale, out);
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
        // A,.5,-.25,["GAS",STANDARD,S=.1,...],-.25: the numbers, a [...] element left out.
        std::size_t i = line.find(',');
        int depth = 0;
        std::string item;
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
            if (c == '[') {
                ++depth;
                item.clear();
            } else if (c == ']') {
                depth = std::max(depth - 1, 0);
                item.clear();
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
        return all;
    };
    static const std::vector<LinetypeDef> kInch = build(1.0);
    static const std::vector<LinetypeDef> kIso = build(25.4);
    return metric ? kIso : kInch;
}

} // namespace musacad::core
