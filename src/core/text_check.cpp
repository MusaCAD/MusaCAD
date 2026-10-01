// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/text_check.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

#include "musacad/core/block_resolve.hpp"
#include "musacad/core/dimension.hpp"
#include "musacad/core/font_engine.hpp"
#include "musacad/core/geometry_kernel.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/text/mtext.hpp"
#include "musacad/core/text/stroke_font.hpp"
#include "musacad/core/text/text_codes.hpp"
#include "musacad/core/text/text_frame.hpp"

namespace musacad::core {

namespace {

bool shown(const GeometryStore& store, const EntityProps& p) {
    const Layer* l = store.layer(p.layer);
    return l != nullptr && l->on && !l->frozen && !p.hidden();
}

std::string layer_name(const GeometryStore& store, const EntityProps& p) {
    const Layer* l = store.layer(p.layer);
    return l != nullptr ? l->name : std::string();
}

template <class Arena, class Fn>
void each(const Arena& arena, EntityKind kind, Fn&& fn) {
    const auto gens = arena.generations();
    for (std::uint32_t i = 0; i < arena.slot_count(); ++i) {
        if (arena.alive(i)) {
            fn(EntityHandle{i, gens[i], kind});
        }
    }
}

std::vector<char32_t> code_points(std::string_view s) {
    std::vector<char32_t> out;
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        const auto cont = [&](std::size_t k) { return static_cast<char32_t>(static_cast<unsigned char>(s[i + k]) & 0x3F); };
        if (c < 0x80) {
            out.push_back(c);
            i += 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            out.push_back((static_cast<char32_t>(c & 0x1F) << 6) | cont(1));
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
            out.push_back((static_cast<char32_t>(c & 0x0F) << 12) | (cont(1) << 6) | cont(2));
            i += 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
            out.push_back((static_cast<char32_t>(c & 0x07) << 18) | (cont(1) << 12) | (cont(2) << 6) | cont(3));
            i += 4;
        } else {
            i += 1;
        }
    }
    return out;
}

std::string utf8(char32_t cp) {
    std::string s;
    if (cp < 0x80) {
        s += static_cast<char>(cp);
    } else if (cp < 0x800) {
        s += static_cast<char>(0xC0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | (cp >> 18));
        s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return s;
}

/// Whether a run in `font` is drawn with the built-in stroke font, whose glyphs are
/// known here. With no font engine (the command line) an outline face cannot be told
/// apart from a missing one, so only the names that always mean the stroke font count.
bool stroke_drawn(const IFontEngine* fonts, std::string_view font) {
    if (fonts != nullptr) {
        return !fonts->is_outline_font(font);
    }
    if (font.empty()) {
        return true;
    }
    std::string u;
    for (const char c : font) {
        u += static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
    }
    return u == "STANDARD" || (u.size() > 4 && u.compare(u.size() - 4, 4, ".SHX") == 0);
}

struct Builder {
    const GeometryStore& store;
    TextCheckReport& report;
    std::vector<std::vector<char32_t>> missing; ///< per run
    std::size_t entity = 0;
    std::vector<Vec2> pts;

    /// Adds a run whose baseline starts at `origin`: `text` as drawn, `height` its cap
    /// height, at `rotation`, the style's width factor and obliquing applied. `stroke`
    /// forces the stroke font (dimension text is always drawn with it).
    void add(const char* type, const EntityProps& props, std::string_view text, std::uint16_t font_id,
             Vec2 origin, double height, double rotation, double width_factor, double oblique,
             bool stroke = false) {
        if (text.empty() || !(height > 0.0)) {
            return;
        }
        const std::string_view font = stroke ? std::string_view{} : store.font_name(font_id);
        const IFontEngine* fonts = store.font_engine();
        const bool outline = !stroke && fonts != nullptr && fonts->is_outline_font(font);
        pts.clear();
        if (outline && fonts != nullptr) {
            fonts->glyph_fills(font, text, {0.0, 0.0}, height, 0.0, pts);
        } else {
            text::append_text_segments(text, {0.0, 0.0}, height, 0.0, text::Justify::Left, pts);
        }
        text::apply_text_style(pts, {0.0, 0.0}, 0.0, width_factor, oblique);
        std::vector<char32_t> lacks;
        if (stroke || stroke_drawn(fonts, font)) {
            for (const char32_t cp : code_points(text)) {
                if (!text::has_glyph(cp) && std::find(lacks.begin(), lacks.end(), cp) == lacks.end()) {
                    lacks.push_back(cp);
                }
            }
        }
        if (pts.empty() && lacks.empty()) {
            return; // only spaces: nothing drawn, nothing missing
        }
        Vec2 lo{0.0, 0.0};
        Vec2 hi{0.0, 0.0};
        if (!pts.empty()) {
            lo = hi = pts.front();
            for (const Vec2& p : pts) {
                lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
                hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
            }
        }
        TextRun run;
        run.type = type;
        run.layer = layer_name(store, props);
        run.text = std::string(text);
        run.font = std::string(font);
        run.space = props.space();
        run.entity = entity;
        const double cs = std::cos(rotation);
        const double sn = std::sin(rotation);
        const Vec2 local[4] = {{lo.x, lo.y}, {hi.x, lo.y}, {hi.x, hi.y}, {lo.x, hi.y}};
        for (int i = 0; i < 4; ++i) {
            run.box[i] = {origin.x + local[i].x * cs - local[i].y * sn, origin.y + local[i].x * sn + local[i].y * cs};
        }
        run.lo = run.hi = run.box[0];
        for (const Vec2& p : run.box) {
            run.lo = {std::min(run.lo.x, p.x), std::min(run.lo.y, p.y)};
            run.hi = {std::max(run.hi.x, p.x), std::max(run.hi.y, p.y)};
        }
        report.runs.push_back(std::move(run));
        missing.push_back(std::move(lacks));
    }

    void paragraph(const char* type, const EntityProps& props, const MTextBlock& block) {
        const text::MTextLayout lay =
            text::layout_mtext(block, store.string_of(block), store.font_engine(), store.font_name(block.font));
        for (const text::MTextLine& line : lay.lines) {
            add(type, props, line.text, block.font, line.origin, block.height, block.rotation, block.width_factor, 0.0);
        }
    }
};

/// The run's own axes: along the baseline and up, unit, with the box's width and height.
struct Frame2 {
    Vec2 o;
    Vec2 u;
    Vec2 v;
    double w;
    double h;
};
Frame2 frame_of_run(const TextRun& r) {
    Frame2 f{r.box[0], {1.0, 0.0}, {0.0, 1.0}, length(r.box[1] - r.box[0]), length(r.box[3] - r.box[0])};
    if (f.w > 1e-15) {
        f.u = (r.box[1] - r.box[0]) / f.w;
    }
    if (f.h > 1e-15) {
        f.v = (r.box[3] - r.box[0]) / f.h;
    } else {
        f.v = {-f.u.y, f.u.x};
    }
    return f;
}

/// How deep two boxes overlap: the least penetration over the four axes their edges
/// give (separating axes); <= 0 when they do not overlap.
double penetration(const TextRun& a, const TextRun& b) {
    double least = 1e300;
    for (const TextRun* r : {&a, &b}) {
        const Frame2 f = frame_of_run(*r);
        for (const Vec2 axis : {f.u, f.v}) {
            double a0 = 1e300;
            double a1 = -1e300;
            double b0 = 1e300;
            double b1 = -1e300;
            for (const Vec2& p : a.box) {
                const double t = dot(p, axis);
                a0 = std::min(a0, t);
                a1 = std::max(a1, t);
            }
            for (const Vec2& p : b.box) {
                const double t = dot(p, axis);
                b0 = std::min(b0, t);
                b1 = std::max(b1, t);
            }
            least = std::min(least, std::min(a1, b1) - std::max(a0, b0));
        }
    }
    return least;
}

/// Whether segment p-q passes through the run's box, `inset` inside its edges (a line
/// that only touches the letters' outermost edge does not cross them).
bool crosses(const TextRun& r, Vec2 p, Vec2 q, double inset) {
    if (std::max(p.x, q.x) < r.lo.x || std::min(p.x, q.x) > r.hi.x || std::max(p.y, q.y) < r.lo.y ||
        std::min(p.y, q.y) > r.hi.y) {
        return false;
    }
    const Frame2 f = frame_of_run(r);
    const Vec2 a{dot(p - f.o, f.u), dot(p - f.o, f.v)};
    const Vec2 b{dot(q - f.o, f.u), dot(q - f.o, f.v)};
    const Vec2 d = b - a;
    double t0 = 0.0;
    double t1 = 1.0;
    const auto clip = [&](double den, double num) { // keep den * t <= num
        if (std::abs(den) < 1e-300) {
            return num >= 0.0;
        }
        const double t = num / den;
        if (den > 0.0) {
            t1 = std::min(t1, t);
        } else {
            t0 = std::max(t0, t);
        }
        return t0 <= t1;
    };
    return clip(-d.x, a.x - inset) && clip(d.x, f.w - inset - a.x) && clip(-d.y, a.y - inset) &&
           clip(d.y, f.h - inset - a.y);
}

struct Segment {
    Vec2 a;
    Vec2 b;
    std::size_t source;
};

} // namespace

TextCheckReport check_text(const GeometryStore& store, const IGeometryKernel& kernel,
                           const TextCheckOptions& options) {
    TextCheckReport report;
    Builder b{store, report, {}, 0, {}};

    each(store.texts(), EntityKind::Text, [&](EntityHandle h) {
        const TextData* t = store.text_like(h);
        if (t == nullptr || !shown(store, t->props)) {
            return;
        }
        const text::TextFrame f = text::frame_of(store, *t);
        b.add("TEXT", t->props, text::substitute_text(store.string_of(*t)), t->font, f.origin, f.height, f.rotation,
              f.width_factor, f.oblique);
        ++b.entity;
    });
    each(store.attdefs(), EntityKind::AttDef, [&](EntityHandle h) {
        const TextData* t = store.text_like(h);
        if (t == nullptr || !shown(store, t->props)) {
            return;
        }
        const text::TextFrame f = text::frame_of(store, *t);
        b.add("ATTDEF", t->props, text::substitute_text(store.string_of(*t)), t->font, f.origin, f.height,
              f.rotation, f.width_factor, f.oblique);
        ++b.entity;
    });
    each(store.mtexts(), EntityKind::MText, [&](EntityHandle h) {
        const MTextData* m = store.mtext(h);
        if (m == nullptr || !shown(store, m->props)) {
            return;
        }
        b.paragraph("MTEXT", m->props, m->text);
        ++b.entity;
    });
    each(store.mleaders(), EntityKind::MLeader, [&](EntityHandle h) {
        const MLeaderData* m = store.mleader(h);
        if (m == nullptr || !shown(store, m->props)) {
            return;
        }
        b.paragraph("MLEADER", m->props, m->text);
        ++b.entity;
    });
    each(store.dimensions(), EntityKind::Dimension, [&](EntityHandle h) {
        const DimData* d = store.dimension(h);
        if (d == nullptr || !shown(store, d->props)) {
            return;
        }
        const DimStyle* style = store.dimstyle(d->style);
        const DimGeometry g =
            compute_dim_geometry(*d, style != nullptr ? *style : DimStyle{}, Rgb{}, store.dim_text_parts(*d));
        for (const bool second : {false, true}) {
            Vec2 q[4];
            if (dim_label_quad(g, second, q)) {
                b.add("DIMENSION", d->props, second ? g.label2 : g.label, 0, q[0], g.text_height, g.text_rotation, 1.0,
                      0.0, /*stroke=*/true);
            }
        }
        ++b.entity;
    });
    each(store.leaders(), EntityKind::Leader, [&](EntityHandle h) {
        const LeaderData* l = store.leader(h);
        if (l == nullptr || !shown(store, l->props)) {
            return;
        }
        const DimStyle* style = store.dimstyle(l->style);
        const DimStyle s = apply_dim_overrides(style != nullptr ? *style : DimStyle{}, l->overrides);
        b.add("LEADER", l->props, text::substitute_text(store.string_of(*l)), l->font,
              l->knee + Vec2{s.arrow_size * 0.4, 0.0}, l->text_height, 0.0, 1.0, 0.0);
        ++b.entity;
    });

    const std::vector<TextRun>& runs = report.runs;

    // Characters drawn blank.
    for (std::size_t i = 0; i < runs.size(); ++i) {
        if (!b.missing[i].empty()) {
            TextProblem p;
            p.kind = TextProblemKind::MissingGlyph;
            p.run = i;
            p.missing = b.missing[i];
            report.problems.push_back(std::move(p));
        }
    }

    // Letters over letters: a sweep along x, then the boxes' own axes.
    std::vector<std::size_t> order(runs.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) { return runs[x].lo.x < runs[y].lo.x; });
    std::vector<std::pair<std::size_t, std::size_t>> overlaps;
    for (std::size_t k = 0; k < order.size(); ++k) {
        const TextRun& a = runs[order[k]];
        for (std::size_t m = k + 1; m < order.size() && runs[order[m]].lo.x <= a.hi.x; ++m) {
            const TextRun& c = runs[order[m]];
            if (a.entity == c.entity || a.space != c.space || c.lo.y > a.hi.y || c.hi.y < a.lo.y) {
                continue;
            }
            const double tol = 1e-6 * std::max(1e-9, std::min(length(a.box[3] - a.box[0]), length(c.box[3] - c.box[0])));
            if (penetration(a, c) > tol) {
                overlaps.emplace_back(std::min(order[k], order[m]), std::max(order[k], order[m]));
            }
        }
    }
    std::sort(overlaps.begin(), overlaps.end());
    for (const auto& [x, y] : overlaps) {
        TextProblem p;
        p.kind = TextProblemKind::Overlap;
        p.run = x;
        p.other = y;
        report.problems.push_back(std::move(p));
    }

    // The frames: the window, or each space's largest closed rectangle.
    std::set<std::uint8_t> spaces;
    for (const TextRun& r : runs) {
        spaces.insert(r.space);
    }
    if (options.window) {
        for (const std::uint8_t s : spaces) {
            const auto& [w0, w1] = *options.window;
            report.frames.push_back(TextCheckFrame{s, {std::min(w0.x, w1.x), std::min(w0.y, w1.y)},
                                                   {std::max(w0.x, w1.x), std::max(w0.y, w1.y)}, true});
        }
    } else {
        each(store.polylines(), EntityKind::Polyline, [&](EntityHandle h) {
            const PolylineData* pl = store.polyline(h);
            if (pl == nullptr || !shown(store, pl->props) || spaces.count(pl->props.space()) == 0) {
                return;
            }
            const auto v = store.vertices_of(*pl);
            const auto bulges = store.bulges_of(*pl);
            std::size_t n = v.size();
            const bool repeats = n == 5 && length(v[4] - v[0]) < 1e-9;
            if (!(n == 4 && pl->closed) && !repeats) {
                return;
            }
            n = 4;
            for (std::size_t i = 0; i < n; ++i) {
                if (i < bulges.size() && std::abs(bulges[i]) > 1e-12) {
                    return;
                }
                const Vec2 e = v[(i + 1) % n] - v[i];
                if (std::abs(e.x) > 1e-9 && std::abs(e.y) > 1e-9) {
                    return; // not axis-aligned
                }
            }
            Vec2 lo = v[0];
            Vec2 hi = v[0];
            for (std::size_t i = 1; i < n; ++i) {
                lo = {std::min(lo.x, v[i].x), std::min(lo.y, v[i].y)};
                hi = {std::max(hi.x, v[i].x), std::max(hi.y, v[i].y)};
            }
            const double area = (hi.x - lo.x) * (hi.y - lo.y);
            if (!(area > 0.0)) {
                return;
            }
            const std::uint8_t s = pl->props.space();
            for (TextCheckFrame& f : report.frames) {
                if (f.space == s) {
                    if (area > (f.hi.x - f.lo.x) * (f.hi.y - f.lo.y)) {
                        f.lo = lo;
                        f.hi = hi;
                    }
                    return;
                }
            }
            report.frames.push_back(TextCheckFrame{s, lo, hi, false});
        });
        std::sort(report.frames.begin(), report.frames.end(),
                  [](const TextCheckFrame& x, const TextCheckFrame& y) { return x.space < y.space; });
    }
    for (std::size_t i = 0; i < runs.size(); ++i) {
        for (const TextCheckFrame& f : report.frames) {
            if (f.space != runs[i].space) {
                continue;
            }
            const double tol = 1e-9 * std::max({1.0, std::abs(f.lo.x), std::abs(f.lo.y), std::abs(f.hi.x), std::abs(f.hi.y)});
            bool out = false;
            for (const Vec2& p : runs[i].box) {
                out = out || p.x < f.lo.x - tol || p.y < f.lo.y - tol || p.x > f.hi.x + tol || p.y > f.hi.y + tol;
            }
            if (out) {
                TextProblem p;
                p.kind = TextProblemKind::OutsideFrame;
                p.run = i;
                report.problems.push_back(std::move(p));
            }
        }
    }

    // Lines through letters.
    if (options.lines && !runs.empty()) {
        struct Source {
            std::string type;
            std::string layer;
            std::uint8_t space;
        };
        std::vector<Source> sources;
        std::vector<Segment> segs;
        std::vector<Vec2> poly;
        const auto curve = [&](EntityHandle h, const EntityProps& props, const char* type, bool closed) {
            if (!shown(store, props) || spaces.count(props.space()) == 0) {
                return;
            }
            poly.clear();
            kernel.tessellate(store, h, kDefaultTessTolerance, poly);
            if (poly.size() < 2) {
                return;
            }
            sources.push_back(Source{type, layer_name(store, props), props.space()});
            for (std::size_t i = 1; i < poly.size(); ++i) {
                segs.push_back(Segment{poly[i - 1], poly[i], sources.size() - 1});
            }
            if (closed && length(poly.back() - poly.front()) > 1e-12) {
                segs.push_back(Segment{poly.back(), poly.front(), sources.size() - 1});
            }
        };
        each(store.lines(), EntityKind::Line, [&](EntityHandle h) {
            const LineData* l = store.line(h);
            if (l != nullptr && shown(store, l->props) && spaces.count(l->props.space()) != 0) {
                sources.push_back(Source{"LINE", layer_name(store, l->props), l->props.space()});
                segs.push_back(Segment{l->a, l->b, sources.size() - 1});
            }
        });
        each(store.polylines(), EntityKind::Polyline, [&](EntityHandle h) {
            const PolylineData* pl = store.polyline(h);
            if (pl != nullptr) {
                curve(h, pl->props, "POLYLINE", pl->closed);
            }
        });
        each(store.arcs(), EntityKind::Arc, [&](EntityHandle h) {
            if (const ArcData* a = store.arc(h)) {
                curve(h, a->props, "ARC", false);
            }
        });
        each(store.circles(), EntityKind::Circle, [&](EntityHandle h) {
            if (const CircleData* c = store.circle(h)) {
                curve(h, c->props, "CIRCLE", true);
            }
        });
        each(store.ellipses(), EntityKind::Ellipse, [&](EntityHandle h) {
            if (const EllipseData* e = store.ellipse(h)) {
                curve(h, e->props, "ELLIPSE", false);
            }
        });
        each(store.splines(), EntityKind::Spline, [&](EntityHandle h) {
            if (const SplineData* s = store.spline(h)) {
                curve(h, s->props, "SPLINE", false);
            }
        });
        std::vector<InsertSeg> iseg;
        each(store.inserts(), EntityKind::Insert, [&](EntityHandle h) {
            const InsertData* ins = store.insert(h);
            if (ins == nullptr || !shown(store, ins->props) || spaces.count(ins->props.space()) == 0) {
                return;
            }
            iseg.clear();
            resolve_insert(store, *ins, kDefaultTessTolerance, iseg);
            if (iseg.empty()) {
                return;
            }
            sources.push_back(Source{"INSERT", layer_name(store, ins->props), ins->props.space()});
            for (const InsertSeg& s : iseg) {
                segs.push_back(Segment{s.a, s.b, sources.size() - 1});
            }
        });
        for (std::size_t i = 0; i < runs.size(); ++i) {
            const TextRun& r = runs[i];
            const double inset = 1e-6 * std::max(1e-9, length(r.box[3] - r.box[0]));
            std::vector<bool> reported(sources.size(), false);
            for (const Segment& s : segs) {
                if (reported[s.source] || sources[s.source].space != r.space || !crosses(r, s.a, s.b, inset)) {
                    continue;
                }
                reported[s.source] = true;
                TextProblem p;
                p.kind = TextProblemKind::CrossesLine;
                p.run = i;
                p.line_type = sources[s.source].type;
                p.line_layer = sources[s.source].layer;
                p.line_a = s.a;
                p.line_b = s.b;
                report.problems.push_back(std::move(p));
            }
        }
    }
    return report;
}

namespace {

std::string json_string(std::string_view s) {
    std::string out = "\"";
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                out += buf;
            } else {
                out += ch;
            }
        }
    }
    return out + "\"";
}

std::string num(double v) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.10g", std::abs(v) < 1e-12 ? 0.0 : v);
    return buf;
}

std::string point(Vec2 p) { return "[" + num(p.x) + ", " + num(p.y) + "]"; }

std::string code_point(char32_t cp) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "U+%04X", static_cast<unsigned>(cp));
    return buf;
}

const char* kind_word(TextProblemKind k) {
    switch (k) {
    case TextProblemKind::Overlap:
        return "overlap";
    case TextProblemKind::OutsideFrame:
        return "outside-frame";
    case TextProblemKind::CrossesLine:
        return "crosses-line";
    case TextProblemKind::MissingGlyph:
        break;
    }
    return "missing-glyph";
}

std::string run_json(const TextCheckReport& r, std::size_t i) {
    const TextRun& t = r.runs[i];
    std::string s = "{\"index\": " + std::to_string(i) + ", \"type\": " + json_string(t.type) +
                    ", \"layer\": " + json_string(t.layer) + ", \"text\": " + json_string(t.text) +
                    ", \"font\": " + json_string(t.font) + ", \"space\": " + std::to_string(t.space) +
                    ", \"entity\": " + std::to_string(t.entity) + ", \"box\": [";
    for (int k = 0; k < 4; ++k) {
        s += (k > 0 ? ", " : "") + point(t.box[k]);
    }
    return s + "], \"bounds\": [" + num(t.lo.x) + ", " + num(t.lo.y) + ", " + num(t.hi.x) + ", " + num(t.hi.y) + "]}";
}

std::string run_words(const TextCheckReport& r, std::size_t i) {
    const TextRun& t = r.runs[i];
    return t.type + " \"" + t.text + "\" (layer " + (t.layer.empty() ? std::string("0") : t.layer) + ", at " +
           num(t.box[0].x) + "," + num(t.box[0].y) + ")";
}

} // namespace

std::string text_check_json(const TextCheckReport& report, std::string_view file) {
    std::string s = "{\n  \"file\": " + json_string(file) + ",\n  \"texts\": " + std::to_string(report.runs.size()) +
                    ",\n  \"problems_found\": " + std::to_string(report.problems.size()) + ",\n  \"frames\": [";
    for (std::size_t i = 0; i < report.frames.size(); ++i) {
        const TextCheckFrame& f = report.frames[i];
        s += std::string(i > 0 ? "," : "") + "\n    {\"space\": " + std::to_string(f.space) + ", \"from\": \"" +
             (f.from_window ? "window" : "rectangle") + "\", \"min\": " + point(f.lo) + ", \"max\": " + point(f.hi) + "}";
    }
    s += report.frames.empty() ? "],\n  \"problems\": [" : "\n  ],\n  \"problems\": [";
    for (std::size_t i = 0; i < report.problems.size(); ++i) {
        const TextProblem& p = report.problems[i];
        s += std::string(i > 0 ? "," : "") + "\n    {\"kind\": \"" + kind_word(p.kind) + "\", \"text\": " + run_json(report, p.run);
        switch (p.kind) {
        case TextProblemKind::Overlap:
            s += ", \"with\": " + run_json(report, p.other);
            break;
        case TextProblemKind::CrossesLine:
            s += ", \"line\": {\"type\": " + json_string(p.line_type) + ", \"layer\": " + json_string(p.line_layer) +
                 ", \"from\": " + point(p.line_a) + ", \"to\": " + point(p.line_b) + "}";
            break;
        case TextProblemKind::MissingGlyph: {
            s += ", \"missing\": [";
            for (std::size_t k = 0; k < p.missing.size(); ++k) {
                s += std::string(k > 0 ? ", " : "") + "{\"character\": " + json_string(utf8(p.missing[k])) +
                     ", \"code\": \"" + code_point(p.missing[k]) + "\"}";
            }
            s += "]";
            break;
        }
        case TextProblemKind::OutsideFrame:
            for (const TextCheckFrame& f : report.frames) {
                if (f.space == report.runs[p.run].space) {
                    s += ", \"frame\": {\"min\": " + point(f.lo) + ", \"max\": " + point(f.hi) + "}";
                }
            }
            break;
        }
        s += "}";
    }
    s += report.problems.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return s;
}

std::string text_check_text(const TextCheckReport& report, std::string_view file) {
    std::string s;
    const std::string f(file);
    for (const TextProblem& p : report.problems) {
        switch (p.kind) {
        case TextProblemKind::Overlap:
            s += f + ": overlap: " + run_words(report, p.run) + " and " + run_words(report, p.other) + "\n";
            break;
        case TextProblemKind::OutsideFrame:
            s += f + ": outside the frame: " + run_words(report, p.run) + "\n";
            break;
        case TextProblemKind::CrossesLine:
            s += f + ": crosses a line: " + run_words(report, p.run) + " and " + p.line_type + " (layer " +
                 (p.line_layer.empty() ? std::string("0") : p.line_layer) + ") " + num(p.line_a.x) + "," +
                 num(p.line_a.y) + " to " + num(p.line_b.x) + "," + num(p.line_b.y) + "\n";
            break;
        case TextProblemKind::MissingGlyph: {
            s += f + ": no glyph in the font: " + run_words(report, p.run) + ":";
            for (const char32_t cp : p.missing) {
                s += " " + utf8(cp) + " (" + code_point(cp) + ")";
            }
            s += "\n";
            break;
        }
        }
    }
    return s;
}

} // namespace musacad::core
