// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

#include "musacad/core/geometry_kernel.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/render_snapshot.hpp"

namespace musacad::core {

/// Rebuilds the renderable payload of `out` (points + line-segment endpoints)
/// from the LIVE entities in `store`, tessellating curves via `kernel`.
///
/// Only live arena slots and their live vertex spans are read. The append-only
/// polyline/spline vertex pools may retain vertices of deleted entities, but
/// those are never visited here: iteration goes through live slots and their
/// (offset,count) views, so deleted geometry leaves no residue in the snapshot.
/// The caller is responsible for setting `out.version` and `out.checksum`.
/// `ltscale` is the global linetype scale (AutoCAD LTSCALE): dash patterns are
/// derived here (never stored) and their lengths multiplied by it.
/// Outline-font text is resolved to filled glyph geometry via the store's injected
/// IFontEngine (store.font_engine()); when none is set, all text renders with the
/// built-in stroke font.
void build_render_snapshot(const GeometryStore& store, const IGeometryKernel& kernel,
                           RenderSnapshot& out, double tolerance = kDefaultTessTolerance,
                           double ltscale = 1.0);

} // namespace musacad::core
