// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
#pragma once
#include "musacad/render/camera.hpp"
#include "musacad/render/overlay.hpp"

namespace musacad::render {
/// Lay out measured fields using the render camera; font shaping stays on the UI thread.
/// Screen-edge clamping and collision avoidance apply to fields and the command prompt.
CanvasCommandUI layout_dyn_labels(const std::vector<DynLabel>& labels, const Camera2D& camera,
                                  double dpr, const std::vector<core::Vec2>& obstacles = {});
} // namespace musacad::render
