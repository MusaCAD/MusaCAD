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

} // namespace musacad::command
