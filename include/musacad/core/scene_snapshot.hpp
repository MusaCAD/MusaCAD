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
void build_render_snapshot(const GeometryStore& store, const IGeometryKernel& kernel,
                           RenderSnapshot& out, double tolerance = kDefaultTessTolerance);

} // namespace musacad::core
