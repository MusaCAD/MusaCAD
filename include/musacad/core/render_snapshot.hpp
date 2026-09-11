// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <bit>
#include <array>
#include <cstdint>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "musacad/core/block_attdef_info.hpp"
#include "musacad/core/entity_handle.hpp"
#include "musacad/core/math/math.hpp"
#include "musacad/core/named_view.hpp"
#include "musacad/core/text_style.hpp"
#include "musacad/core/units.hpp"
#include "musacad/core/page_setup.hpp"
#include "musacad/core/properties.hpp"
#include "musacad/core/properties_palette.hpp"
#include "musacad/core/snap.hpp"

namespace musacad::core {

/// A contiguous range of segments (or points) sharing one resolved colour, so the
/// renderer can colour the scene with one small draw per distinct colour. Off and
/// frozen layers contribute no batches (they are skipped before batching).
/// An image definition as published to the renderer (see RenderSnapshot::image_defs).
struct ImageDefView {
    std::shared_ptr<const std::vector<std::uint8_t>> bytes; ///< embedded payload, or null
    std::string source;                                      ///< relative path when not embedded
    std::uint32_t version = 0;
};

/// A layout as the tab strip, the sheet drawing and PLOT need it.
struct LayoutInfo {
    std::uint8_t id = 1;
    std::string name;
    double paper_w_mm = 297.0;
    double paper_h_mm = 210.0;
    bool landscape = true;
    /// The sheet in paper-space units (millimetres): its corner is the origin.
    [[nodiscard]] double sheet_w() const noexcept { return landscape ? std::max(paper_w_mm, paper_h_mm) : std::min(paper_w_mm, paper_h_mm); }
    [[nodiscard]] double sheet_h() const noexcept { return landscape ? std::min(paper_w_mm, paper_h_mm) : std::max(paper_w_mm, paper_h_mm); }
};

/// MSPACE state for the UI: model space is being edited through `viewport`; the camera
/// hand-off uses the viewport's view and the sheet scale recorded on entry.
struct MspaceInfo {
    bool active = false;
    std::uint8_t layout = 0;
    EntityHandle viewport;
    Vec2 view_center;
    double scale = 1.0;           ///< paper mm per model unit
    double paper_px_per_mm = 1.0; ///< the sheet's on-screen scale when MSPACE began
};
/// A viewport's frame on the active layout (for the double-click hit test).
struct ViewportRect {
    EntityHandle handle;
    Vec2 center;
    double width = 0.0;
    double height = 0.0;
};

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
    /// True for batches of single-stroke TEXT segments (TEXT / MTEXT / dimension & leader
    /// labels). Derived at snapshot build (never baked, not in the checksum). On SCREEN
    /// these render at a heavier "text weight" so single-stroke glyphs read crisp and
    /// present instead of a dull 1px hairline; on PLOT they honour `lineweight` (0 =
    /// hairline), so the on-screen polish never reaches paper. Only ever set on
    /// `line_batches` -- filled TTF text lives in `fill_batches`, untouched (Ph29).
    bool is_text = false;
    /// World-space cap height of the text in this batch (0 for non-text). Text batches are
    /// keyed by quantised height so the renderer can taper the on-screen text weight by the
    /// glyph's on-screen size -- tiny text reads crisp, title-size text keeps full presence.
    /// Derived at snapshot build; not baked, not in the checksum; ignored by PLOT.
    float text_height = 0.0f;
    /// True for fill batches whose colour was COMPUTED (a gradient band) rather than
    /// taken from the entity: PLOT keeps such colours as they are, since its
    /// "near-white means the default pen, plot it black" rule is about the screen
    /// convention for entity colours and would punch a black band into a gradient.
    bool computed_color = false;
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
/// A placed raster image, as the RENDERER sees it. Deliberately tiny: the transform,
/// the definition index, the clip UVs and the definition's version -- and NOT the
/// pixels. The snapshot is copied through the triple buffer on every publish, so
/// megabytes of decoded raster here would wreck the geometry->render handoff. The
/// renderer/plotter keeps a texture cache keyed by `def`, invalidated when `def_version`
/// changes; this struct is only the instruction to draw one.
struct ImageInstance {
    std::array<Vec2, 4> quad{};  ///< world corners (already clipped), CCW from insertion
    std::array<float, 4> uv{};   ///< u0, v0, u1, v1 in image fractions (v down)
    std::uint16_t def = 0;
    std::uint32_t def_version = 0;
    EntityHandle handle;         ///< so pick/highlight can map back
};

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

/// One open document, surfaced for the multi-document tab strip. `name` is the display
/// name (a filename or "DrawingN"); `dirty` drives the per-tab "*" marker. The list is
/// the single source of truth for the tabs (the UI is a pure view). Not checksummed.
struct DocumentInfo {
    std::uint64_t id = 0;
    std::string name;  ///< tab display name (filename or "DrawingN")
    std::string path;  ///< native file path ("" = untitled); drives Ctrl+S vs Save As
    bool dirty = false;
};

/// A construction line (XLINE/RAY) as the renderer needs it: it is infinite, so it is
/// NOT baked into line_vertices (which would be wrong after a pan). The render thread
/// clips it to the viewport each frame. `ray` restricts drawing to base + t*dir, t>=0.
struct ConstructionLineView {
    Vec2 base;
    Vec2 dir;
    bool ray = false;
    Rgb color;
    std::uint8_t lineweight = 25;
};

struct RenderSnapshot {
    std::uint64_t version = 0;          ///< bumps every publish (snap/selection too)
    std::uint64_t geometry_version = 0; ///< bumps only when scene geometry changes
    /// Producer-side bookkeeping for the triple buffer: which geometry-cache build and
    /// which selection-highlight build this SLOT's arrays were last copied from. A
    /// publish happens on every cursor move; the scene changes only on edits, so a slot
    /// that already holds the current build is left alone instead of being rewritten
    /// with megabytes of identical vertices -- the difference between O(1) and O(scene)
    /// per mouse move on a large drawing.
    std::uint64_t copied_geometry_id = 0;
    std::uint64_t copied_selection_build = 0;
    std::vector<Vec2> points;
    std::vector<Vec2> line_vertices; // 2 entries per segment, ordered by colour batch
    std::vector<ConstructionLineView> construction_lines; // XLINE/RAY: clipped per-frame
    std::vector<Vec2> wipeout_vertices; // WIPEOUT masks: triangles drawn in the background colour
    std::uint8_t active_space = 0;      // 0 model space, else the active layout's id
    std::vector<LayoutInfo> layouts;    // the layout tabs (name, sheet size)
    MspaceInfo mspace;                  // MSPACE: editing model space through a viewport
    std::vector<ViewportRect> viewport_rects; // the active layout's viewports
    bool wipeout_frames = true;         // WIPEOUTFRAME
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
    std::vector<NamedView> named_views; // VIEW table (for VIEW Restore / ?)
    std::vector<std::string> group_names; // GROUP table names (for GROUP ?)
    std::vector<std::string> block_names; // block-definition names (INSERT ?)
    std::vector<std::vector<BlockAttDefInfo>> block_attdefs; // per block: its attributes (INSERT prompts)
    DrawingUnits units;                    // UNITS: display formats (readout, DYN, inquiry)
    std::vector<TextStyle> text_styles;    // STYLE table (TEXT, the palette)
    std::uint16_t current_text_style = 0;

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
    // Filled (SOLID hatch) triangles of the selection, overlaid in the highlight
    // colour so a picked fill reads as selected (the fill itself stays in the scene
    // `fill_*` channels -- this is a derived highlight, 3 Vec2 per triangle).
    std::vector<Vec2> selected_fill_vertices;

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
    double pending_dim_aux = 0.0;
    std::uint64_t pending_dim_version = 0;

    // Grips of the selected set (display + hit-test), the hot grip (grabbed or
    // hovered, index into `grips`, or -1), and the transient drag preview (the
    // edited entity computed on a temporary store -- the real store is untouched).
    // Interaction state, not part of the checksum.
    /// Placed raster images (transform + def reference only -- never pixels).
    std::vector<ImageInstance> images;
    /// The image definitions those refer to, as the renderer's texture cache needs them:
    /// the embedded payload (shared by pointer: never copied per publish) or the path
    /// relative to `image_dir`, and the version that invalidates a cached texture.
    std::vector<ImageDefView> image_defs;
    std::string image_dir;   ///< the drawing's directory; "" for an unsaved drawing
    std::uint8_t image_frame = 1; ///< IMAGEFRAME: 0 hidden, 1 shown (and plotted), 2 shown
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

    // Multi-document: every open document (in tab order) + which one this snapshot is
    // from. The tab strip is rendered purely from this. `dirty` above is the ACTIVE
    // document's flag (mirrors the active entry here). Not part of the checksum.
    std::vector<DocumentInfo> documents;
    std::uint64_t active_document_id = 0;

    void clear() noexcept {
        version = 0;
        geometry_version = 0;
        copied_geometry_id = 0;
        copied_selection_build = 0;
        points.clear();
        line_vertices.clear();
        construction_lines.clear();
        wipeout_vertices.clear();
        active_space = 0;
        layouts.clear();
        mspace = MspaceInfo{};
        viewport_rects.clear();
        line_batches.clear();
        point_batches.clear();
        fill_vertices.clear();
        fill_batches.clear();
        lineweight_display = true;
        layers.clear();
        dimstyles.clear();
        page_setups.clear();
        named_views.clear();
        group_names.clear();
        block_names.clear();
        block_attdefs.clear();
        current_layer = 0;
        has_pending_dim = false;
        pending_dim_version = 0;
        images.clear();
        image_defs.clear();
        image_dir.clear();
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
        selected_fill_vertices.clear();
        hover = EntityHandle{};
        has_hover = false;
        hover_line_vertices.clear();
        status.clear();
        status_version = 0;
        dirty = false;
        document_version = 0;
        documents.clear();
        active_document_id = 0;
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
