// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

#include <bit>
#include <cstdint>
#include <string>
#include <vector>

#include "musacad/core/entity_handle.hpp"
#include "musacad/core/math/math.hpp"
#include "musacad/core/page_setup.hpp"
#include "musacad/core/properties.hpp"
#include "musacad/core/properties_palette.hpp"
#include "musacad/core/snap.hpp"

namespace musacad::core {

/// A contiguous range of segments (or points) sharing one resolved colour, so the
/// renderer can colour the scene with one small draw per distinct colour. Off and
/// frozen layers contribute no batches (they are skipped before batching).
struct ColorBatch {
    Rgb color;
    /// UNITS DIFFER BY ARRAY. line_batches: first/count are in SEGMENTS (segment s spans
    /// line_vertices[2*(first+s)] and [+1]). point_batches: vertices in `points`. fill_batches:
    /// vertices in `fill_vertices` (3 per triangle). For lines, always go through
    /// for_each_line_segment() so consumers can't mis-index (treating segment units as vertex
    /// indices drew phantom connectors across batches and dropped half the lines).
    std::uint32_t first = 0;
    std::uint32_t count = 0;
    std::uint8_t lineweight = 25; ///< resolved display lineweight (hundredths mm); 0 = points/fills
};

/// An immutable (from the renderer's perspective) view of the scene, produced
/// by the geometry thread and consumed lock-free by the render thread. Payload
/// is contiguous SoA-style: standalone points and a flat list of line-segment
/// endpoints (two Vec2 per segment) covering native lines plus tessellated
/// curves. The render thread only ever sees our own contiguous arrays.
///
/// `version` increases monotonically with each publish. `checksum` is computed
/// over the version and payload so a consumer can assert it observed a complete,
/// untorn snapshot (used by the concurrency test).
/// A grip handle published for the selected set: its world position plus the
/// (handle, index) the UI sends back to begin a drag, and its role for colouring.
struct GripInfo {
    Vec2 pos;
    EntityHandle handle;
    std::uint32_t index = 0;
    std::uint8_t kind = 0; ///< core::GripKind
};

/// An editable text entity (TEXT / MTEXT / QLEADER label) surfaced so the UI can
/// hit-test a double-click and pre-fill the editor from the live content -- without
/// touching the store. `anchor` is the insertion point (editor placement); `min/max`
/// is the world AABB for hit-testing; `multiline` allows newlines (MTEXT/QLEADER).
struct TextEditTarget {
    EntityHandle handle;
    Vec2 anchor;
    Vec2 min;
    Vec2 max;
    double height = 2.5;
    double rotation = 0.0;
    bool multiline = false;
    std::string content;
};

struct RenderSnapshot {
    std::uint64_t version = 0;          ///< bumps every publish (snap/selection too)
    std::uint64_t geometry_version = 0; ///< bumps only when scene geometry changes
    std::vector<Vec2> points;
    std::vector<Vec2> line_vertices; // 2 entries per segment, ordered by colour batch
    std::uint64_t checksum = 0;

    // Per-colour batches over `line_vertices` / `points` (after ByLayer
    // resolution; off/frozen layers excluded). The renderer draws one sub-range
    // per batch. Layer table + current layer for the UI. Not part of the checksum.
    std::vector<ColorBatch> line_batches;
    std::vector<ColorBatch> point_batches;
    std::vector<Layer> layers;
    std::uint16_t current_layer = 0;
    std::vector<DimStyle> dimstyles; // for the UI dimension-placement preview
    std::vector<PageSetup> page_setups; // saved PLOT page setups (for the PLOT dialog)

    // Filled triangles (3 Vec2 per triangle), batched by colour -- arrowheads and
    // any future hatching. `lineweight_display` is the LWDISPLAY toggle.
    std::vector<Vec2> fill_vertices;
    std::vector<ColorBatch> fill_batches;
    bool lineweight_display = true;

    // World-space AABB of live geometry (for ZOOM extents). `has_bounds` is
    // false when the scene is empty. Derived from the payload; not part of the
    // checksum.
    Vec2 bounds_min{};
    Vec2 bounds_max{};
    bool has_bounds = false;

    // Active object-snap (computed geometry-side from the current cursor) carried
    // through the existing handoff; the renderer draws a marker for it. Interaction
    // state, not part of the checksum.
    bool has_snap = false;
    Vec2 snap_point{};
    SnapType snap_type = SnapType::None;

    // Current selection (geometry-side). `selection` is the queryable handle set
    // (API for the command layer / future scripting); `selected_line_vertices`
    // are those entities' segments, for the highlight and move/mirror ghosts.
    // Interaction state, not part of the checksum.
    std::vector<EntityHandle> selection;
    std::vector<Vec2> selected_line_vertices;

    // Rollover (hover) candidate: the entity under the cursor's pick-box. Visual
    // only -- it does not change `selection` until the user clicks. Interaction
    // state, not part of the checksum.
    EntityHandle hover;
    bool has_hover = false;
    std::vector<Vec2> hover_line_vertices;

    // Pending object-dimension def points, resolved once (geometry-side) when the
    // user selects the object during an object-based dimension command. The UI
    // reads these to rubber-band the full dimension at the cursor during placement,
    // with no per-move geometry round-trip. `pending_dim_type` matches DimType.
    // Interaction state, not part of the checksum.
    bool has_pending_dim = false;
    Vec2 pending_dim_a{};
    Vec2 pending_dim_b{};
    Vec2 pending_dim_line_pt{};
    std::uint8_t pending_dim_type = 0;
    std::uint64_t pending_dim_version = 0;

    // Grips of the selected set (display + hit-test), the hot grip (grabbed or
    // hovered, index into `grips`, or -1), and the transient drag preview (the
    // edited entity computed on a temporary store -- the real store is untouched).
    // Interaction state, not part of the checksum.
    std::vector<GripInfo> grips;
    int hot_grip = -1;
    std::vector<Vec2> grip_preview_segments;
    std::vector<Vec2> grip_preview_fills;

    // Editable text entities (for double-click-to-edit hit-testing + pre-fill).
    // Interaction state, not part of the checksum.
    std::vector<TextEditTarget> text_edit_targets;

    // Aggregated property view of the current selection (for the PR palette):
    // values + per-field "varies" flags. Computed on the geometry thread so the
    // UI never queries the store. Interaction state, not part of the checksum.
    SelectionSummary selection_summary;

    // Last command-result message from the geometry thread (e.g. "Filleted." or
    // "Nothing to fillet."). `status_version` bumps on each new message so the UI
    // can echo it once. Honest feedback: set by the op that actually ran, so the
    // command line reflects what the engine did -- not what the UI hoped. Not part
    // of the checksum.
    std::string status;
    std::uint64_t status_version = 0;

    // Document state for the title bar / dirty prompts. `dirty` is true when there
    // are unsaved changes; `document_version` bumps on save/open/new. Not part of
    // the checksum.
    bool dirty = false;
    std::uint64_t document_version = 0;

    void clear() noexcept {
        version = 0;
        geometry_version = 0;
        points.clear();
        line_vertices.clear();
        line_batches.clear();
        point_batches.clear();
        fill_vertices.clear();
        fill_batches.clear();
        lineweight_display = true;
        layers.clear();
        dimstyles.clear();
        page_setups.clear();
        current_layer = 0;
        has_pending_dim = false;
        pending_dim_version = 0;
        grips.clear();
        hot_grip = -1;
        grip_preview_segments.clear();
        grip_preview_fills.clear();
        text_edit_targets.clear();
        selection_summary = SelectionSummary{};
        checksum = 0;
        bounds_min = {};
        bounds_max = {};
        has_bounds = false;
        has_snap = false;
        snap_point = {};
        snap_type = SnapType::None;
        selection.clear();
        selected_line_vertices.clear();
        hover = EntityHandle{};
        has_hover = false;
        hover_line_vertices.clear();
        status.clear();
        status_version = 0;
        dirty = false;
        document_version = 0;
    }

    /// FNV-1a over the version and payload. Cheap and order-sensitive; enough to
    /// detect a torn read (mismatch between guard and guarded data).
    [[nodiscard]] std::uint64_t compute_checksum() const noexcept {
        std::uint64_t h = 1469598103934665603ull;
        const auto mix = [&h](std::uint64_t v) noexcept {
            h ^= v;
            h *= 1099511628211ull;
        };
        const auto mix_d = [&mix](double d) noexcept { mix(std::bit_cast<std::uint64_t>(d)); };
        mix(version);
        mix(static_cast<std::uint64_t>(points.size()));
        for (const Vec2& p : points) {
            mix_d(p.x);
            mix_d(p.y);
        }
        mix(static_cast<std::uint64_t>(line_vertices.size()));
        for (const Vec2& p : line_vertices) {
            mix_d(p.x);
            mix_d(p.y);
        }
        return h;
    }

    [[nodiscard]] bool consistent() const noexcept { return checksum == compute_checksum(); }
};

/// THE one way to read a line batch's segments. `line_batches` store first/count in
/// SEGMENT units; segment s is the vertex pair [2*(first+s), 2*(first+s)+1]. Every line
/// consumer (plot + viewport) must agree on this -- mis-indexing it drew phantom connector
/// lines across batches and dropped half the geometry in plots.
template <class Fn>
inline void for_each_line_segment(const RenderSnapshot& snap, const ColorBatch& b, Fn fn) {
    for (std::uint32_t s = 0; s < b.count; ++s) {
        const std::uint32_t base = 2u * (b.first + s);
        fn(snap.line_vertices[base], snap.line_vertices[base + 1]);
    }
}

} // namespace musacad::core
