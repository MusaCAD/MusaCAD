// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/tiled_viewport.hpp"

#include <cmath>

namespace musacad::core {

namespace {
TiledViewport part(const TiledViewport& w, double fx0, double fy0, double fx1, double fy1) {
    TiledViewport t = w;
    const double dx = w.x1 - w.x0;
    const double dy = w.y1 - w.y0;
    t.x0 = w.x0 + dx * fx0;
    t.x1 = w.x0 + dx * fx1;
    t.y0 = w.y0 + dy * fy0;
    t.y1 = w.y0 + dy * fy1;
    return t;
}
bool near(double a, double b) { return std::abs(a - b) < 1e-9; }
} // namespace

std::vector<TiledViewport> split_vport(const TiledViewport& w, std::string_view kind) {
    if (kind == "2v") {
        return {part(w, 0, 0, 0.5, 1), part(w, 0.5, 0, 1, 1)};
    }
    if (kind == "2h") {
        return {part(w, 0, 0.5, 1, 1), part(w, 0, 0, 1, 0.5)};
    }
    if (kind == "3r") { // the large one on the right, two stacked on the left
        return {part(w, 0.5, 0, 1, 1), part(w, 0, 0.5, 0.5, 1), part(w, 0, 0, 0.5, 0.5)};
    }
    if (kind == "3l") {
        return {part(w, 0, 0, 0.5, 1), part(w, 0.5, 0.5, 1, 1), part(w, 0.5, 0, 1, 0.5)};
    }
    if (kind == "3a") { // the large one above, two side by side below
        return {part(w, 0, 0.5, 1, 1), part(w, 0, 0, 0.5, 0.5), part(w, 0.5, 0, 1, 0.5)};
    }
    if (kind == "3b") {
        return {part(w, 0, 0, 1, 0.5), part(w, 0, 0.5, 0.5, 1), part(w, 0.5, 0.5, 1, 1)};
    }
    if (kind == "3v") {
        return {part(w, 0, 0, 1.0 / 3.0, 1), part(w, 1.0 / 3.0, 0, 2.0 / 3.0, 1),
                part(w, 2.0 / 3.0, 0, 1, 1)};
    }
    if (kind == "3h") {
        return {part(w, 0, 2.0 / 3.0, 1, 1), part(w, 0, 1.0 / 3.0, 1, 2.0 / 3.0),
                part(w, 0, 0, 1, 1.0 / 3.0)};
    }
    if (kind == "4") {
        return {part(w, 0, 0.5, 0.5, 1), part(w, 0.5, 0.5, 1, 1), part(w, 0, 0, 0.5, 0.5),
                part(w, 0.5, 0, 1, 0.5)};
    }
    return {w}; // "single" and anything unknown
}

bool join_vports(std::vector<TiledViewport>& tiles, std::size_t dominant, std::size_t other) {
    if (dominant >= tiles.size() || other >= tiles.size() || dominant == other) {
        return false;
    }
    const TiledViewport& a = tiles[dominant];
    const TiledViewport& b = tiles[other];
    TiledViewport u = a;
    // Side by side with the same vertical extent, or stacked with the same horizontal one.
    if (near(a.y0, b.y0) && near(a.y1, b.y1) && (near(a.x1, b.x0) || near(b.x1, a.x0))) {
        u.x0 = std::min(a.x0, b.x0);
        u.x1 = std::max(a.x1, b.x1);
    } else if (near(a.x0, b.x0) && near(a.x1, b.x1) && (near(a.y1, b.y0) || near(b.y1, a.y0))) {
        u.y0 = std::min(a.y0, b.y0);
        u.y1 = std::max(a.y1, b.y1);
    } else {
        return false;
    }
    tiles[dominant] = u;
    tiles.erase(tiles.begin() + static_cast<std::ptrdiff_t>(other));
    return true;
}

} // namespace musacad::core
