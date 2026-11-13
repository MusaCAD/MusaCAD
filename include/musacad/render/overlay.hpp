// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <string>
#include <vector>

#include "musacad/core/math/math.hpp"

namespace musacad::render {

/// A UI-font coverage triangle vertex. Rasterized once on the UI thread, then batched
/// on the GPU with coverage alpha; no textures or global scene multisampling required.
struct CanvasGlyphVertex {
    core::Vec2 position;
    float coverage = 1.0f;
};

/// One AutoCAD-style Dynamic Input value field drawn ON the canvas (not an OS
/// window), so it is always glued to the geometry it describes. The renderer
/// projects `anchor` (world) with the SAME camera as the rubber-band, then draws a
/// small boxed number there. `focused` marks the active type-into field (highlighted value or
/// caret). Canvas rendering replaces the floating tooltip windows, which drifted
/// off the geometry on multi-monitor / certain window managers.
struct DynLabel {
    core::Vec2 anchor{};  ///< world-space anchor on the live geometry (edge midpoint, etc.)
    core::Vec2 out{};     ///< world-space OUTWARD unit dir; the box is nudged this way (screen)
                          ///< so it sits just OUTSIDE the edge and fields never overlap
    std::string text;     ///< displayed value, shaped on the UI thread
    bool focused = false; ///< the active field: brighter border + a text caret
    /// Informational tracking tooltip, without a dimension caption or editable state.
    bool prose = false;
    bool locked = false;  ///< a valid typed value constrains the preview
    bool editing = false; ///< show a caret only when there is typed text
    double value_width = 0.0;
    double caption_width = 0.0;
    /// Antialiased UI-font coverage in local device pixels, y down. No font calls on the render thread.
    std::vector<CanvasGlyphVertex> value_glyphs;
    std::vector<CanvasGlyphVertex> caption_glyphs;
};

/// The on-canvas command-input surface (idle command entry + autocomplete dropdown,
/// and mid-command sub-prompts), pre-laid-out by the UI thread into SCREEN-space
/// primitives so the renderer just draws them -- the antialiased glyph coverage comes from
/// the UI thread, the boxes/borders are plain quads/lines. Bounded draw
/// calls: one batch each for fills, glyphs and lines regardless of suggestion count.
struct CanvasCommandUI {
    bool active = false;
    std::vector<core::Vec2> box_fills;    ///< panel/box backgrounds (triangles, screen px)
    std::vector<core::Vec2> hi_fills;     ///< highlighted suggestion row background (triangles)
    std::vector<CanvasGlyphVertex> glyph_fills;  ///< TTF glyph triangles for ALL text (one batch)
    std::vector<core::Vec2> lines;        ///< box borders + caret (line segments)
    std::vector<CanvasGlyphVertex> muted_glyphs;
    std::vector<core::Vec2> shadow_fills;
    std::vector<core::Vec2> accent_lines;
    /// Occupied rectangles (top-left, bottom-right pairs), used to keep dimension fields clear.
    std::vector<core::Vec2> bounds;
};

/// Transient, render-side interaction visuals: the live command preview
/// (rubber-banding), the selection rubber-band rectangle, and the move/mirror
/// "ghost" transform applied to the currently-selected geometry. None of this is
/// real geometry -- it is composed on the UI thread from the active command's
/// state + the live cursor and handed to the renderer like the camera/crosshair,
/// with no geometry-thread round-trip.
struct RenderOverlay {
    /// Preview line segments (2 points per segment), world space.
    std::vector<core::Vec2> preview_segments;

    /// Selection rubber-band: 0 = none, 1 = window (blue), 2 = crossing (green).
    int rect_mode = 0;
    core::Vec2 rect_a{};
    core::Vec2 rect_b{};
    /// Lasso (press-and-drag) selection: the polygon so far; `lasso_mode` 1 window, 2
    /// crossing, 3 fence (Space cycles them while dragging).
    std::vector<core::Vec2> lasso;
    int lasso_mode = 0;
    /// HIGHLIGHT 0: the selection is drawn without its highlight.
    bool hide_selection = false;
    /// Grips made hot with Shift + click, drawn in the hot colour.
    std::vector<core::Vec2> hot_grips;

    /// Ghost transform applied by the renderer to the snapshot's selected
    /// geometry: 0 = none, 1 = move (translate by ghost_b - ghost_a),
    /// 2 = mirror (reflect across ghost_a..ghost_b), 3 = rotate about ghost_a by
    /// ghost_param radians, 4 = scale about ghost_a by ghost_param.
    int ghost_mode = 0;
    core::Vec2 ghost_a{};
    core::Vec2 ghost_b{};
    double ghost_param = 0.0;

    /// On-canvas Dynamic Input value fields, anchored to the live geometry.
    std::vector<DynLabel> dyn_labels;

    /// On-canvas command-input surface (idle entry + autocomplete + sub-prompts).
    CanvasCommandUI command_ui;

    void clear() noexcept {
        preview_segments.clear();
        rect_mode = 0;
        lasso.clear();
        lasso_mode = 0;
        hide_selection = false;
        hot_grips.clear();
        ghost_mode = 0;
        dyn_labels.clear();
        command_ui = CanvasCommandUI{};
    }
};

} // namespace musacad::render
