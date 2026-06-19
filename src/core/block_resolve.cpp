// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/block_resolve.hpp"

#include <algorithm>
#include <cmath>

#include "musacad/core/geometry_store.hpp"
#include "musacad/core/math/mat3.hpp"
#include "musacad/core/math/math.hpp"
#include "musacad/core/text/mtext.hpp"
#include "musacad/core/text/stroke_font.hpp"
#include "musacad/core/text/text_codes.hpp"

namespace musacad::core {

namespace {

constexpr std::size_t kMaxCurveSegments = 4096;

Mat3 insert_matrix(const InsertData& ins) {
    // Apply scale, then rotation, then translation to a local point.
    return Mat3::translation(ins.pos) * Mat3::rotation(ins.rotation) *
           Mat3::scale({ins.scale_x, ins.scale_y});
}

std::size_t curve_segments(double radius, double sweep_abs, double tolerance) {
    sweep_abs = std::abs(sweep_abs);
    if (radius <= 0.0 || sweep_abs <= 0.0) {
        return 1;
    }
    double max_step = kHalfPi;
    if (tolerance > 0.0 && tolerance < radius) {
        max_step = 2.0 * std::acos(1.0 - tolerance / radius);
    }
    if (!(max_step > 0.0)) {
        max_step = kHalfPi;
    }
    const auto n = static_cast<std::size_t>(std::ceil(sweep_abs / max_step));
    return std::clamp(n, std::size_t{1}, kMaxCurveSegments);
}

/// Resolve a block member's effective props against the store, with a pragmatic
/// ByBlock approximation: a fully-ByLayer member on layer 0 inherits the placing
/// insert's resolved props (the common "draw on layer 0, colour by the reference"
/// convention). Members with explicit layers/overrides keep their own.
ResolvedProps member_props(const GeometryStore& store, const EntityProps& mp,
                           const ResolvedProps& inherited) {
    const Layer* ml = store.layer(mp.layer);
    ResolvedProps r = resolve(mp, ml != nullptr ? *ml : Layer{});
    if (mp.layer == 0) { // ByBlock-ish: inherit from the insert
        if (mp.color_by_layer()) {
            r.color = inherited.color;
        }
        if (mp.lineweight_by_layer()) {
            r.lineweight = inherited.lineweight;
        }
        if (mp.linetype_by_layer()) {
            r.linetype = inherited.linetype;
        }
    }
    return r;
}

void emit_pairs_transformed(const Mat3& m, const std::vector<Vec2>& local_pairs,
                            const ResolvedProps& rp, std::vector<InsertSeg>& out) {
    // local_pairs holds 2 Vec2 per segment (stroke-font / dimension convention).
    for (std::size_t i = 0; i + 1 < local_pairs.size(); i += 2) {
        out.push_back(InsertSeg{m.transform_point(local_pairs[i]),
                                m.transform_point(local_pairs[i + 1]), rp.color, rp.lineweight,
                                rp.linetype});
    }
}

void emit_polyline_transformed(const Mat3& m, const std::vector<Vec2>& local_chain,
                               const ResolvedProps& rp, std::vector<InsertSeg>& out) {
    // local_chain is a connected run (consecutive points = segments); transform each
    // and emit as pairs so distinct primitives never join.
    for (std::size_t i = 1; i < local_chain.size(); ++i) {
        out.push_back(InsertSeg{m.transform_point(local_chain[i - 1]),
                                m.transform_point(local_chain[i]), rp.color, rp.lineweight,
                                rp.linetype});
    }
}

void emit_block(const GeometryStore& store, const BlockContent& bc, const Mat3& xform,
                const ResolvedProps& inherited, double tol, int depth,
                std::vector<InsertSeg>& out);

void emit_block(const GeometryStore& store, const BlockContent& bc, const Mat3& xform,
                const ResolvedProps& inherited, double tol, int depth,
                std::vector<InsertSeg>& out) {
    for (const LineData& l : bc.lines) {
        const ResolvedProps rp = member_props(store, l.props, inherited);
        out.push_back(InsertSeg{xform.transform_point(l.a), xform.transform_point(l.b), rp.color,
                                rp.lineweight, rp.linetype});
    }
    std::vector<Vec2> chain;
    for (const CircleData& c : bc.circles) {
        const ResolvedProps rp = member_props(store, c.props, inherited);
        const std::size_t n = curve_segments(c.radius, kTwoPi, tol);
        chain.clear();
        for (std::size_t i = 0; i <= n; ++i) {
            const double a = static_cast<double>(i) / static_cast<double>(n) * kTwoPi;
            chain.push_back({c.center.x + c.radius * std::cos(a), c.center.y + c.radius * std::sin(a)});
        }
        emit_polyline_transformed(xform, chain, rp, out);
    }
    for (const ArcData& arc : bc.arcs) {
        const ResolvedProps rp = member_props(store, arc.props, inherited);
        double sweep = arc.end_angle - arc.start_angle;
        while (sweep < 0.0) {
            sweep += kTwoPi;
        }
        if (sweep <= 0.0) {
            sweep = kTwoPi;
        }
        const std::size_t n = curve_segments(arc.radius, sweep, tol);
        chain.clear();
        for (std::size_t i = 0; i <= n; ++i) {
            const double a = arc.start_angle + sweep * (static_cast<double>(i) / static_cast<double>(n));
            chain.push_back(
                {arc.center.x + arc.radius * std::cos(a), arc.center.y + arc.radius * std::sin(a)});
        }
        emit_polyline_transformed(xform, chain, rp, out);
    }
    for (const BlockPolyline& pl : bc.polylines) {
        const ResolvedProps rp = member_props(store, pl.props, inherited);
        if (pl.verts.empty()) {
            continue;
        }
        const std::size_t nv = pl.verts.size();
        const std::size_t segs = (pl.closed && nv >= 2) ? nv : nv - 1;
        chain.clear();
        chain.push_back(pl.verts[0]);
        for (std::size_t i = 0; i < segs; ++i) {
            const Vec2 p0 = pl.verts[i];
            const Vec2 p1 = pl.verts[(i + 1) % nv];
            const double b = pl.bulges.empty() ? 0.0 : pl.bulges[i];
            if (b == 0.0) {
                chain.push_back(p1);
                continue;
            }
            // Bulge arc: b = tan(theta/4). Sample it as a sub-chain.
            const double chord = distance(p0, p1);
            if (chord < 1e-12) {
                chain.push_back(p1);
                continue;
            }
            const double theta = 4.0 * std::atan(b);
            const double radius = chord / (2.0 * std::sin(std::abs(theta) / 2.0));
            const Vec2 mid = (p0 + p1) * 0.5;
            const Vec2 dir = (p1 - p0) / chord;
            const Vec2 nrm{-dir.y, dir.x};
            const double h = radius * std::cos(std::abs(theta) / 2.0);
            const Vec2 center = mid + nrm * (b > 0.0 ? h : -h);
            const double a0 = std::atan2(p0.y - center.y, p0.x - center.x);
            const std::size_t m = curve_segments(radius, theta, tol);
            for (std::size_t k = 1; k <= m; ++k) {
                const double a = a0 + theta * (static_cast<double>(k) / static_cast<double>(m));
                chain.push_back({center.x + radius * std::cos(a), center.y + radius * std::sin(a)});
            }
            chain.back() = p1; // land exactly
        }
        emit_polyline_transformed(xform, chain, rp, out);
    }
    std::vector<Vec2> tseg;
    for (const BlockText& t : bc.texts) {
        const ResolvedProps rp = member_props(store, t.props, inherited);
        const auto j = static_cast<text::Justify>(t.justify <= 2 ? t.justify : 0);
        tseg.clear();
        // Expand control codes at render time, like the top-level TEXT path (block MTEXT goes
        // through layout_mtext, which already substitutes).
        text::append_text_segments(text::substitute_text(t.content), t.pos, t.height, t.rotation, j,
                                   tseg);
        emit_pairs_transformed(xform, tseg, rp, out);
    }
    for (const BlockMText& mt : bc.mtexts) {
        const ResolvedProps rp = member_props(store, mt.props, inherited);
        const text::MTextLayout lay = text::layout_mtext(mt.block, mt.content);
        emit_pairs_transformed(xform, lay.segments, rp, out);
    }
    if (depth + 1 >= kMaxBlockDepth) {
        return; // nesting guard: stop composing deeper
    }
    for (const InsertData& nested : bc.inserts) {
        const BlockDef* bd = store.block(nested.block);
        if (bd == nullptr) {
            continue; // dangling nested reference -- skip, stay consistent
        }
        const ResolvedProps inh = member_props(store, nested.props, inherited);
        emit_block(store, bd->content, xform * insert_matrix(nested), inh, tol, depth + 1, out);
    }
}

} // namespace

void resolve_insert(const GeometryStore& store, const InsertData& ins, double tolerance,
                    std::vector<InsertSeg>& out) {
    const BlockDef* bd = store.block(ins.block);
    if (bd == nullptr) {
        return; // dangling reference -- nothing to draw, store stays consistent
    }
    const Layer* il = store.layer(ins.props.layer);
    const ResolvedProps inherited = resolve(ins.props, il != nullptr ? *il : Layer{});
    emit_block(store, bd->content, insert_matrix(ins), inherited, tolerance, 0, out);
}

} // namespace musacad::core
