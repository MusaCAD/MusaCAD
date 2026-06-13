// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

#include <cstdint>
#include <vector>

#include "musacad/core/math/vec2.hpp"
#include "musacad/core/properties.hpp"

namespace musacad::core {

class GeometryStore;
struct InsertData;

/// A resolved world-space segment of a block instance, tagged with its display
/// properties (ByLayer/ByBlock already resolved).
struct InsertSeg {
    Vec2 a;
    Vec2 b;
    Rgb color{255, 255, 255};
    std::uint8_t lineweight = 25;
    Linetype linetype = Linetype::Continuous;
};

/// Maximum block-nesting depth resolved (guards cyclic / pathological definitions).
inline constexpr int kMaxBlockDepth = 16;

/// Resolve a model-space INSERT to world-space segments: the block definition's
/// geometry transformed by the insert's (insertion point x scale x rotation), with
/// nested inserts composed (parent x child) and depth-guarded. Each primitive
/// contributes its own connected run -- there are NO phantom connectors between
/// distinct primitives. This is THE single transformed-geometry path: the render
/// snapshot, entity bounds, pick, window/crossing, hover, and snap all go through it,
/// so the displayed, picked, and bounded geometry can never diverge.
void resolve_insert(const GeometryStore& store, const InsertData& ins, double tolerance,
                    std::vector<InsertSeg>& out);

} // namespace musacad::core
