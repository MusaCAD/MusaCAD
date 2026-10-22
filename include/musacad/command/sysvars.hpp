// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

// System variables AutoCAD keeps in the registry rather than in a drawing: the commands
// read and set them, and the UI keeps them in Musa CAD's settings between runs.

namespace musacad::command {

/// HPSEPARATE: one HATCH makes one hatch of the areas picked or selected (false), or one each.
[[nodiscard]] inline bool& hpseparate() noexcept {
    static bool value = false;
    return value;
}

/// MEASUREINIT: the MEASUREMENT a new drawing starts with -- 1 metric (acadiso.pat and
/// acadiso.lin), 0 imperial (acad.pat and acad.lin).
[[nodiscard]] inline int& measureinit() noexcept {
    static int value = 1;
    return value;
}

/// 3DOSMODE: the 3D object snaps -- 2 vertex, 4 midpoint on an edge, 8 centre of a face, 16
/// knot, 32 perpendicular to a face, 64 nearest to a face; 1 turns them all off (F4). A 2D
/// drawing has no faces or edges of solids for them to find.
[[nodiscard]] inline int& threedosmode() noexcept {
    static int value = 11;
    return value;
}

/// UCSDETECT: Dynamic UCS (F6) on (1) or off (0) -- the UCS following the face of a solid
/// under the cursor, which a 2D drawing has none of.
[[nodiscard]] inline int& ucsdetect() noexcept {
    static int value = 1;
    return value;
}

} // namespace musacad::command
