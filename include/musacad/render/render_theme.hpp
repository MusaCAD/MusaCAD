// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

namespace musacad::render::theme {

// Centralized render-side palette (GL colors, RGBA in 0..1) and marker sizing.
// The viewport's look lives here, not as scattered literals in the renderer.
//
// Entity visual states are intentionally distinct:
//   * normal   -> kScene     (off-white)
//   * hover     -> kHover     (light blue rollover preview; does not select)
//   * selected -> kSelected  (orange)

inline constexpr float kGridMinor[4] = {0.17f, 0.18f, 0.21f, 1.0f};
inline constexpr float kGridMajor[4] = {0.30f, 0.32f, 0.37f, 1.0f};
inline constexpr float kScene[4] = {0.85f, 0.90f, 0.96f, 1.0f};
inline constexpr float kPoint[4] = {1.0f, 0.60f, 0.20f, 1.0f};
inline constexpr float kOverlay[4] = {1.0f, 0.85f, 0.20f, 1.0f};
inline constexpr float kCrosshair[4] = {0.55f, 0.60f, 0.66f, 1.0f};

// Object-snap marker: bright lime, AutoCAD-style, high contrast on the dark bg.
inline constexpr float kSnapMarker[4] = {0.62f, 1.0f, 0.10f, 1.0f};

inline constexpr float kHover[4] = {0.45f, 0.78f, 1.0f, 1.0f};    // rollover highlight
inline constexpr float kSelected[4] = {1.0f, 0.55f, 0.10f, 1.0f}; // selection highlight
inline constexpr float kPreview[4] = {0.90f, 0.90f, 0.40f, 1.0f}; // rubber-band preview
inline constexpr float kGhost[4] = {0.55f, 0.80f, 1.0f, 1.0f};    // move/mirror ghost
inline constexpr float kWindow[4] = {0.35f, 0.55f, 1.0f, 1.0f};   // window select box
inline constexpr float kCrossing[4] = {0.35f, 0.90f, 0.45f, 1.0f}; // crossing select box
inline constexpr float kGrip[4] = {0.30f, 0.55f, 1.0f, 1.0f};     // grip square (AutoCAD blue)
inline constexpr float kHotGrip[4] = {1.0f, 0.25f, 0.20f, 1.0f};  // grabbed/hovered grip (hot red)

inline constexpr double kSnapMarkerHalfPx = 9.0; // half-extent of a snap glyph
inline constexpr double kPickBoxHalfPx = 6.0;    // cursor pick-box half-extent
inline constexpr double kMarkerStrokePx = 1.4;   // bold-stroke offset (faked thickness)
inline constexpr double kGripHalfPx = 4.0;       // half-extent of a grip square (screen px)

} // namespace musacad::render::theme
