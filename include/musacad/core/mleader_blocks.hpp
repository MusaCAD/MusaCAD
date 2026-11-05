// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <array>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "musacad/core/block_attdef_info.hpp"
#include "musacad/core/geometry_store.hpp"

namespace musacad::core {

/// The blocks a multileader's content can be without the drawing having them (#61), as
/// AutoCAD offers them: a balloon of each shape with its number, and the detail callout.
/// One is added to a drawing the first time a multileader names it. Each is drawn for a text
/// height of 1, its insertion point at its centre; a multileader scales it by its own text
/// height.
inline constexpr std::array<std::string_view, 6> kStandardTagBlocks = {
    "_TagCircle", "_TagBox", "_TagHexagon", "_TagSlot", "_TagTriangle", "_DetailCallout",
};

/// The standard block of that name (compared without case), or nothing.
[[nodiscard]] std::optional<BlockContent> standard_tag_block(std::string_view name);

/// Its attributes, as the command asks for them -- empty for any other name.
[[nodiscard]] std::vector<BlockAttDefInfo> standard_tag_attdefs(std::string_view name);
/// The name it has in kStandardTagBlocks (as written there), or empty.
[[nodiscard]] std::string_view standard_tag_block_name(std::string_view name) noexcept;

/// Where collected item `i` sits from the first: items `size` apart, in a row (`collect` 0),
/// a column (1) or rows of `collect` items (MLeaderData::collect).
[[nodiscard]] Vec2 collected_offset(std::size_t i, std::uint8_t collect, Vec2 size) noexcept;

/// What a multileader with block content draws (#61): its block once per item, each item's
/// attribute values its span of the multileader's text (in the store's string pool, so the
/// block resolves as an INSERT does). Empty for a multileader with text.
[[nodiscard]] std::vector<InsertData> mleader_block_items(const GeometryStore& store, const MLeaderData& m);

/// The extents of block `block` drawn at the origin at `scale` -- its geometry, without its
/// attributes' text -- or nothing for an empty one.
[[nodiscard]] std::optional<std::pair<Vec2, Vec2>> block_item_extents(const GeometryStore& store,
                                                                     std::uint16_t block, double scale);

} // namespace musacad::core
