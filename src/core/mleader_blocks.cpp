// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/mleader_blocks.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "musacad/core/block_resolve.hpp"
#include "musacad/core/math/math.hpp"

namespace musacad::core {

namespace {

constexpr std::uint8_t kMiddleCenter = 10; // TextJustify::MiddleCenter

/// An attribute centred at `at`, `height` high.
BlockAttDef tag_attdef(const char* tag, const char* prompt, Vec2 at, double height) {
    BlockAttDef a;
    a.tag = tag;
    a.prompt = prompt;
    a.def = tag;
    a.text.pos = at;
    a.text.align = at;
    a.text.height = height;
    a.text.justify = kMiddleCenter;
    a.text.content = tag;
    return a;
}

/// A closed outline of straight sides.
BlockPolyline outline(std::vector<Vec2> pts) {
    BlockPolyline p;
    p.verts = std::move(pts);
    p.closed = true;
    return p;
}

} // namespace

std::optional<BlockContent> standard_tag_block(std::string_view name) {
    BlockContent c;
    if (same_table_name(name, "_TagCircle")) {
        c.circles.push_back(CircleData{{0.0, 0.0}, 1.25});
        c.attdefs.push_back(tag_attdef("TAGNUMBER", "Enter tag number", {0.0, 0.0}, 1.0));
    } else if (same_table_name(name, "_TagBox")) {
        c.polylines.push_back(outline({{-1.25, -1.25}, {1.25, -1.25}, {1.25, 1.25}, {-1.25, 1.25}}));
        c.attdefs.push_back(tag_attdef("TAGNUMBER", "Enter tag number", {0.0, 0.0}, 1.0));
    } else if (same_table_name(name, "_TagHexagon")) {
        std::vector<Vec2> hex;
        for (int i = 0; i < 6; ++i) {
            const double a = kPi / 3.0 * i;
            hex.push_back({1.45 * std::cos(a), 1.45 * std::sin(a)});
        }
        c.polylines.push_back(outline(std::move(hex)));
        c.attdefs.push_back(tag_attdef("TAGNUMBER", "Enter tag number", {0.0, 0.0}, 1.0));
    } else if (same_table_name(name, "_TagSlot")) {
        // Two half circles joined by straight sides: a slot 3.5 long and 2 high.
        BlockPolyline slot = outline({{-0.75, -1.0}, {0.75, -1.0}, {0.75, 1.0}, {-0.75, 1.0}});
        slot.bulges = {0.0, 1.0, 0.0, 1.0};
        c.polylines.push_back(std::move(slot));
        c.attdefs.push_back(tag_attdef("TAGNUMBER", "Enter tag number", {0.0, 0.0}, 1.0));
    } else if (same_table_name(name, "_TagTriangle")) {
        c.polylines.push_back(outline({{-1.7, -1.0}, {1.7, -1.0}, {0.0, 1.95}}));
        c.attdefs.push_back(tag_attdef("TAGNUMBER", "Enter tag number", {0.0, -0.15}, 1.0));
    } else if (same_table_name(name, "_DetailCallout")) {
        c.circles.push_back(CircleData{{0.0, 0.0}, 1.75});
        c.lines.push_back(LineData{{-1.75, 0.0}, {1.75, 0.0}});
        c.attdefs.push_back(tag_attdef("VIEWNUMBER", "Enter view number", {0.0, 0.75}, 0.8));
        c.attdefs.push_back(tag_attdef("SHEETNUMBER", "Enter sheet number", {0.0, -0.75}, 0.8));
    } else {
        return std::nullopt;
    }
    return c;
}

std::string_view standard_tag_block_name(std::string_view name) noexcept {
    for (const std::string_view n : kStandardTagBlocks) {
        if (same_table_name(n, name)) {
            return n;
        }
    }
    return {};
}

Vec2 collected_offset(std::size_t i, std::uint8_t collect, Vec2 size) noexcept {
    const auto k = static_cast<double>(i);
    if (collect == 0) {
        return {k * size.x, 0.0};
    }
    if (collect == 1) {
        return {0.0, -k * size.y};
    }
    return {static_cast<double>(i % collect) * size.x, -static_cast<double>(i / collect) * size.y};
}

std::optional<std::pair<Vec2, Vec2>> block_item_extents(const GeometryStore& store, std::uint16_t block, double scale) {
    // Its own lines only: an attribute's default value (TAGNUMBER) is not what it shows.
    std::vector<InsertSeg> segs;
    resolve_block_shape(store, block, scale, 0.01 * std::max(scale, 1e-9), segs);
    if (segs.empty()) {
        return std::nullopt;
    }
    Vec2 lo = segs.front().a;
    Vec2 hi = lo;
    for (const InsertSeg& s : segs) {
        for (const Vec2 p : {s.a, s.b}) {
            lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
            hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
        }
    }
    return std::pair{lo, hi};
}

std::vector<InsertData> mleader_block_items(const GeometryStore& store, const MLeaderData& m) {
    std::vector<InsertData> out;
    if (m.content != 1 || store.block(m.block) == nullptr) {
        return out;
    }
    const double scale = m.text.height > 0.0 ? m.text.height : 1.0;
    Vec2 size{scale, scale};
    if (const auto ext = block_item_extents(store, m.block, scale)) {
        size = ext->second - ext->first;
    }
    // The items: spans of the text between 0x1E.
    const std::string_view text = store.string_of(m.text);
    const double cs = std::cos(m.text.rotation);
    const double sn = std::sin(m.text.rotation);
    std::size_t start = 0;
    for (std::size_t i = 0;; ++i) {
        const std::size_t end = text.find('\x1E', start);
        const std::size_t len = (end == std::string_view::npos ? text.size() : end) - start;
        const Vec2 off = collected_offset(i, m.collect, size);
        InsertData in;
        in.block = m.block;
        in.pos = m.text.pos + Vec2{off.x * cs - off.y * sn, off.x * sn + off.y * cs};
        in.scale_x = scale;
        in.scale_y = scale;
        in.rotation = m.text.rotation;
        in.props = m.props;
        in.attr_offset = m.text.str_offset + static_cast<std::uint32_t>(start);
        in.attr_len = static_cast<std::uint32_t>(len);
        out.push_back(in);
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return out;
}

std::vector<BlockAttDefInfo> standard_tag_attdefs(std::string_view name) {
    std::vector<BlockAttDefInfo> out;
    if (const std::optional<BlockContent> c = standard_tag_block(name)) {
        for (const BlockAttDef& a : c->attdefs) {
            out.push_back(BlockAttDefInfo{a.tag, a.prompt, a.def, a.flags, a.text.height});
        }
    }
    return out;
}

} // namespace musacad::core
