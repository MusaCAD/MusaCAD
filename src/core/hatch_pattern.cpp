// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Hatch line-pattern engine (Part B). The stock pattern library below is AUTHORED from the
// public AutoCAD .PAT format specification (angle, origin, delta-x, delta-y, dash list) --
// it is NOT copied from Autodesk's proprietary acad.pat. Users can load any vendor .PAT
// file through parse_pat() for patterns beyond this set.

#include "musacad/core/hatch_pattern.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <unordered_map>

namespace musacad::core::hatch {

namespace {

std::string upper(std::string_view s) {
    std::string r(s);
    for (char& c : r) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return r;
}

std::string_view trim(std::string_view s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])) != 0) {
        ++a;
    }
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])) != 0) {
        --b;
    }
    return s.substr(a, b - a);
}

bool parse_double(std::string_view s, double& out) {
    s = trim(s);
    if (s.empty()) {
        return false;
    }
    // std::from_chars for double is not universal across libstdc++ versions in this tree;
    // strtod is reliable and the inputs are short, trusted pattern numbers.
    std::string buf(s);
    char* end = nullptr;
    const double v = std::strtod(buf.c_str(), &end);
    if (end == buf.c_str()) {
        return false;
    }
    out = v;
    return true;
}

// Split a family line on commas into doubles. Returns false if fewer than 5 fields (angle,
// x, y, dx, dy) or any field is not a number.
bool parse_family(std::string_view line, PatternLine& fam) {
    std::vector<double> nums;
    std::size_t pos = 0;
    while (pos <= line.size()) {
        const std::size_t comma = line.find(',', pos);
        const std::string_view tok =
            line.substr(pos, comma == std::string_view::npos ? std::string_view::npos : comma - pos);
        double v = 0.0;
        if (!parse_double(tok, v)) {
            return false;
        }
        nums.push_back(v);
        if (comma == std::string_view::npos) {
            break;
        }
        pos = comma + 1;
    }
    if (nums.size() < 5) {
        return false;
    }
    fam = PatternLine{};
    fam.angle = nums[0];
    fam.origin = {nums[1], nums[2]};
    fam.delta = {nums[3], nums[4]};
    fam.dashes.assign(nums.begin() + 5, nums.end());
    return true;
}

} // namespace

std::vector<Pattern> parse_pat(std::string_view text) {
    std::vector<Pattern> out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t nl = text.find('\n', pos);
        std::string_view raw =
            text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        pos = (nl == std::string_view::npos) ? text.size() : nl + 1;
        std::string_view line = trim(raw);
        if (line.empty() || line.front() == ';') {
            continue;
        }
        if (line.front() == '*') {
            // *NAME, description -- name is up to the first comma.
            std::string_view rest = trim(line.substr(1));
            const std::size_t comma = rest.find(',');
            std::string_view name = trim(comma == std::string_view::npos ? rest : rest.substr(0, comma));
            Pattern p;
            p.name = upper(name);
            out.push_back(std::move(p));
            continue;
        }
        if (out.empty()) {
            continue; // a family line before any *NAME -- ignore
        }
        PatternLine fam;
        if (parse_family(line, fam)) {
            out.back().lines.push_back(std::move(fam));
        }
    }
    // Drop any pattern that ended up with no families (e.g. SOLID stubs).
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](const Pattern& p) { return p.lines.empty(); }),
              out.end());
    return out;
}

namespace {

// The built-in stock patterns, authored from the public .PAT format (see file header).
// ANSI31..ANSI38 are the standard ANSI section hatches; the rest are common geometric
// fills. Lengths are in pattern units (scaled by pattern_scale at render time).
constexpr const char* kBuiltinPat = R"PAT(
*ANSI31, ANSI iron / brick / stone masonry
45, 0,0, 0,0.125
*ANSI32, ANSI steel
45, 0,0, 0,0.375
45, 0.176776695,0, 0,0.375
*ANSI33, ANSI bronze / brass / copper
45, 0,0, 0,0.25
45, 0.176776695,0, 0,0.25, 0.125,-0.0625
*ANSI34, ANSI plastic / rubber
45, 0,0, 0,0.75
45, 0.176776695,0, 0,0.75
45, 0.353553391,0, 0,0.75
45, 0.530330086,0, 0,0.75
*ANSI35, ANSI fire brick / refractory material
45, 0,0, 0,0.25
45, 0.176776695,0, 0,0.25, 0.3125,-0.0625,0,-0.0625
*ANSI36, ANSI marble / slate / glass
45, 0,0, 0.21875,0.125, 0.3125,-0.0625,0,-0.0625
*ANSI37, ANSI lead / zinc / magnesium / insulation
45, 0,0, 0,0.125
135, 0,0, 0,0.125
*ANSI38, ANSI aluminum
45, 0,0, 0,0.125
135, 0,0, 0,0.3125, 0.3125,-0.0625,0,-0.0625
*LINE, Parallel horizontal lines
0, 0,0, 0,0.125
*NET, Horizontal / vertical grid
0, 0,0, 0,0.125
90, 0,0, 0,0.125
*NET3, Network pattern 0-60-120
0, 0,0, 0,0.125
60, 0,0, 0,0.125
120, 0,0, 0,0.125
*GRID, Grid
0, 0,0, 0,0.25
90, 0,0, 0,0.25
*ANGLE, Angle steel
0, 0,0, 0,0.275, 0.2,-0.075
90, 0,0, 0,0.275, 0.2,-0.075
*DASH, Dashed lines
0, 0,0, 0,0.125, 0.125,-0.0625
*DOTS, A scattering of dots
0, 0,0, 0.03125,0.0625, 0,-0.0625
*SQUARE, Small aligned squares
0, 0,0, 0,0.125, 0.125,-0.125
90, 0,0, 0,0.125, 0.125,-0.125
*CROSS, A series of crosses
0, 0,0, 0.25,0.25, 0.125,-0.375
90, 0.0625,-0.0625, 0.25,0.25, 0.125,-0.375
*ZIGZAG, Staircase effect
0, 0,0, 0.125,0.125, 0.125,-0.125
90, 0.125,0, 0.125,0.125, 0.125,-0.125
*TRIANG, Equilateral triangles
60, 0,0, 0.1875,0.324759526, 0.1875,-0.1875
120, 0,0, 0.1875,0.324759526, 0.1875,-0.1875
0, -0.09375,0.162379763, 0.1875,0.324759526, 0.1875,-0.1875
*BRICK, Brick (running bond)
0, 0,0, 0,0.25
90, 0,0, 0.25,0.25, 0.25,-0.25
90, 0.125,0.125, 0.25,0.25, 0.25,-0.25
*BOX, Box steel
90, 0,0, 0,1, 1,-1
90, 0.25,0, 0,1, 1,-1
0, 0,0, 0,1, 1,-1
0, 0,0.25, 0,1, 1,-1
*PLAST, Plastic material
0, 0,0, 0,0.25
0, 0,0.0625, 0,0.25
0, 0,0.125, 0,0.25
*HEX, Hexagons
0, 0,0, 0.2165063509,0.375, 0.125,-0.25
120, 0,0, 0.2165063509,0.375, 0.125,-0.25
60, 0.125,0, 0.2165063509,0.375, 0.125,-0.25
*HONEY, Honeycomb pattern
0, 0,0, 0.1082531755,0.1875, 0.0625,-0.125
120, 0,0, 0.1082531755,0.1875, 0.0625,-0.125
60, 0.0625,0, 0.1082531755,0.1875, 0.0625,-0.125
*STARS, Star of David
0, 0,0, 0,0.216506351, 0.125,-0.125
60, 0,0, 0,0.216506351, 0.125,-0.125
120, 0.0625,-0.108253175, 0,0.216506351, 0.125,-0.125
*GRASS, Grass area
90, 0,0, 0.179,0.18, 0.25,-0.875
45, 0,0, 0.179,0.18, 0.09375,-0.96875
135, 0,0, 0.179,0.18, 0.09375,-0.96875
*EARTH, Earth or ground
0, 0,0, 0.25,0.25, 0.25,-0.25
0, 0,0.09375, 0.25,0.25, 0.25,-0.25
90, 0,0, 0.25,0.25, 0.25,-0.25
90, 0.09375,0, 0.25,0.25, 0.25,-0.25
*CLAY, Clay material
0, 0,0, 0,0.1875
0, 0,0.03125, 0,0.1875, 0.1875,-0.1875
*STEEL, Steel material
45, 0,0, 0,0.09375
*CORK, Cork material
0, 0,0, 0,0.125
135, 0.0625,0, 0.176776695,0.176776695, 0.125,-0.125
*GRAVEL, Gravel
0, 0,0, 0.21875,0.125, 0.125,-0.0625,0,-0.0625
0, 0.1,0.0625, 0.21875,0.125, 0.0625,-0.125
*CONC, Concrete
0, 0,0, 0.21875,0.125, 0.125,-0.0625
50, 0.1,0.05, 0.3,0.18, 0.0625,-0.25
*INSUL, Insulation material
0, 0,0, 0,0.375
0, 0,0.125, 0,0.375, 0.125,-0.125
*CHECKER, Checkerboard
0, 0,0, 0,0.25, 0.25,-0.25
0, 0,0.25, 0,0.5, 0.25,-0.25
90, 0,0, 0,0.25, 0.25,-0.25
90, 0.25,0, 0,0.5, 0.25,-0.25
)PAT";

const std::vector<Pattern>& builtins() {
    static const std::vector<Pattern> kPats = parse_pat(kBuiltinPat);
    return kPats;
}

const std::unordered_map<std::string, const Pattern*>& builtin_index() {
    static const std::unordered_map<std::string, const Pattern*> kIdx = [] {
        std::unordered_map<std::string, const Pattern*> m;
        for (const Pattern& p : builtins()) {
            m.emplace(p.name, &p);
        }
        return m;
    }();
    return kIdx;
}

} // namespace

const Pattern* builtin_pattern(std::string_view name) {
    const std::string key = upper(trim(name));
    if (key.empty() || key == "SOLID") {
        return nullptr;
    }
    const auto& idx = builtin_index();
    const auto it = idx.find(key);
    return it == idx.end() ? nullptr : it->second;
}

const std::vector<std::string>& builtin_pattern_names() {
    static const std::vector<std::string> kNames = [] {
        std::vector<std::string> n;
        for (const Pattern& p : builtins()) {
            n.push_back(p.name);
        }
        std::sort(n.begin(), n.end());
        n.erase(std::unique(n.begin(), n.end()), n.end());
        return n;
    }();
    return kNames;
}

const std::vector<std::string>& pattern_choice_list() {
    static const std::vector<std::string> kChoices = [] {
        std::vector<std::string> n;
        n.emplace_back("SOLID"); // the fill special-case is always offered first
        for (const std::string& name : builtin_pattern_names()) {
            n.push_back(name);
        }
        return n;
    }();
    return kChoices;
}

namespace {

// Emit pen-down dash sub-segments of [ta,tb] along direction u from base `ln`. `dvals` is
// the scaled dash list, `period` their absolute-value sum, `phase0` the dash-phase at t=0
// (so successive parallel lines stagger). A dot (0) becomes a short pen-down tick.
void emit_dashed(Vec2 ln, Vec2 u, double ta, double tb, const std::vector<double>& dvals,
                 double period, double phase0, std::vector<Segment>& out) {
    if (period < 1e-9 || tb - ta < 1e-9) {
        return;
    }
    const double dot_len = period * 0.01; // a dot rendered as a tiny dash
    // Phase of `ta` within the repeating cell, normalised to [0, period).
    double phase = std::fmod(ta + phase0, period);
    if (phase < 0.0) {
        phase += period;
    }
    // Walk cells starting from the one containing `ta`, advancing along [ta,tb].
    double t = ta;
    std::size_t i = 0;
    double acc = 0.0;
    // Position the cursor `i`/`acc` at the dash cell that contains `phase`.
    for (; i < dvals.size(); ++i) {
        const double w = std::abs(dvals[i]) < 1e-12 ? dot_len : std::abs(dvals[i]);
        if (acc + w > phase) {
            break;
        }
        acc += w;
    }
    if (i >= dvals.size()) {
        i = 0;
        acc = 0.0;
    }
    double cell_start_t = t - (phase - acc); // world-param where the current cell began
    int guard = 0;
    const int kMaxCells = 1000000;
    while (t < tb - 1e-12 && guard++ < kMaxCells) {
        const double raw = dvals[i];
        const double w = std::abs(raw) < 1e-12 ? dot_len : std::abs(raw);
        const double cell_end_t = cell_start_t + w;
        const bool pen_down = raw >= 0.0; // dash (>0) or dot (==0) => on; gap (<0) => off
        if (pen_down) {
            const double a = std::max(t, cell_start_t);
            const double b = std::min(tb, cell_end_t);
            if (b - a > 1e-9) {
                out.push_back({{ln.x + u.x * a, ln.y + u.y * a}, {ln.x + u.x * b, ln.y + u.y * b}});
            }
        }
        t = cell_end_t;
        cell_start_t = cell_end_t;
        i = (i + 1) % dvals.size();
    }
}

} // namespace

void generate_pattern_segments(const std::vector<std::vector<Vec2>>& loops, const Pattern& pat,
                               double scale, double angle, Vec2 origin, std::vector<Segment>& out) {
    if (loops.empty() || loops[0].size() < 3) {
        return;
    }
    const double s = scale > 1e-9 ? scale : 1.0;
    // Region bounding box (world).
    double xmin = loops[0][0].x;
    double ymin = loops[0][0].y;
    double xmax = xmin;
    double ymax = ymin;
    for (const auto& loop : loops) {
        for (const Vec2& v : loop) {
            xmin = std::min(xmin, v.x);
            ymin = std::min(ymin, v.y);
            xmax = std::max(xmax, v.x);
            ymax = std::max(ymax, v.y);
        }
    }
    const double diag = std::hypot(xmax - xmin, ymax - ymin);
    if (diag < 1e-9) {
        return;
    }
    const double ca = std::cos(angle);
    const double sa = std::sin(angle);
    // Pattern origin in world: o + s * R(angle) * (0,0) == o (pattern origin is at HPORIGIN).
    const Vec2 o_world = origin;

    std::vector<double> crossings;
    std::vector<double> dvals;
    std::size_t emitted_lines = 0;
    const std::size_t kMaxLinesPerFamily = 200000; // perf guard against tiny-scale blowups

    for (const PatternLine& fam : pat.lines) {
        const double fa = angle + fam.angle * 3.14159265358979323846 / 180.0;
        const Vec2 u{std::cos(fa), std::sin(fa)};      // line direction
        const Vec2 nrm{-std::sin(fa), std::cos(fa)};   // perpendicular
        const double dy = s * fam.delta.y;             // perpendicular spacing
        if (std::abs(dy) < 1e-9) {
            continue; // degenerate family
        }
        const double dx = s * fam.delta.x; // per-row stagger along u
        // Family base point in world: o + s * R(angle) * fam.origin.
        const Vec2 fo{o_world.x + s * (ca * fam.origin.x - sa * fam.origin.y),
                      o_world.y + s * (sa * fam.origin.x + ca * fam.origin.y)};
        // Scaled dash list + period.
        dvals.clear();
        double period = 0.0;
        for (const double d : fam.dashes) {
            dvals.push_back(d * s);
            period += std::abs(d * s);
        }
        const bool solid = dvals.empty() || period < 1e-9;
        // Perpendicular extent of the region relative to the family base.
        double pmin = 1e300;
        double pmax = -1e300;
        for (const auto& loop : loops) {
            for (const Vec2& v : loop) {
                const double d = (v.x - fo.x) * nrm.x + (v.y - fo.y) * nrm.y;
                pmin = std::min(pmin, d);
                pmax = std::max(pmax, d);
            }
        }
        // Robust to the sign of dy: the covering line indices span min..max of pmin/dy,pmax/dy.
        const double a0 = pmin / dy;
        const double a1 = pmax / dy;
        const long n_lo = static_cast<long>(std::floor(std::min(a0, a1))) - 1;
        const long n_hi = static_cast<long>(std::ceil(std::max(a0, a1))) + 1;
        if (n_hi < n_lo) {
            continue;
        }
        if (static_cast<unsigned long>(n_hi - n_lo) > kMaxLinesPerFamily) {
            continue; // scale too small for this region -- skip rather than hang
        }
        for (long n = n_lo; n <= n_hi; ++n) {
            if (emitted_lines++ > kMaxLinesPerFamily) {
                return;
            }
            const Vec2 ln{fo.x + nrm.x * (static_cast<double>(n) * dy),
                          fo.y + nrm.y * (static_cast<double>(n) * dy)};
            // Crossings of this infinite line with every boundary edge (half-open rule on the
            // signed perpendicular distance, so shared vertices count once).
            crossings.clear();
            for (const auto& loop : loops) {
                const std::size_t m = loop.size();
                for (std::size_t k = 0; k < m; ++k) {
                    const Vec2 a = loop[k];
                    const Vec2 b = loop[(k + 1) % m];
                    const double sA = (a.x - ln.x) * nrm.x + (a.y - ln.y) * nrm.y;
                    const double sB = (b.x - ln.x) * nrm.x + (b.y - ln.y) * nrm.y;
                    if ((sA <= 0.0) != (sB <= 0.0)) {
                        const double f = sA / (sA - sB);
                        const Vec2 p{a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f};
                        crossings.push_back((p.x - ln.x) * u.x + (p.y - ln.y) * u.y);
                    }
                }
            }
            if (crossings.size() < 2) {
                continue;
            }
            std::sort(crossings.begin(), crossings.end());
            // Even-odd: inside spans are consecutive crossing pairs.
            const double phase0 = -static_cast<double>(n) * dx; // per-row dash stagger
            for (std::size_t k = 0; k + 1 < crossings.size(); k += 2) {
                const double ta = crossings[k];
                const double tb = crossings[k + 1];
                if (tb - ta < 1e-9) {
                    continue;
                }
                if (solid) {
                    out.push_back({{ln.x + u.x * ta, ln.y + u.y * ta},
                                   {ln.x + u.x * tb, ln.y + u.y * tb}});
                } else {
                    emit_dashed(ln, u, ta, tb, dvals, period, phase0, out);
                }
            }
        }
    }
}

} // namespace musacad::core::hatch
