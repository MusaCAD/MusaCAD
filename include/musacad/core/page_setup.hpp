// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <string>

#include "musacad/core/math/math.hpp"

namespace musacad::core {

/// A saved PLOT configuration (AutoCAD "page setup"): paper, area, scale, orientation,
/// offset, plot style + target. Qt-free so it lives in the store + snapshot + native
/// format alike (the UI converts to/from its PlotSpec). Persisted in the native format
/// (v11); older files simply have none.
struct PageSetup {
    std::string name;
    std::string paper = "ISO A4";
    std::string target = "PDF";
    double paper_w_mm = 297.0;
    double paper_h_mm = 210.0;
    bool landscape = true;
    std::uint8_t area = 1; ///< 0 Display, 1 Extents, 2 Window
    Vec2 win_min{};
    Vec2 win_max{};
    bool fit = true;
    double scale_num = 1.0;
    double scale_den = 1.0;
    bool center = true;
    double off_x_mm = 0.0;
    double off_y_mm = 0.0;
    bool plot_lineweights = true;
    std::uint8_t style = 0; ///< 0 None, 1 Monochrome, 2 Grayscale
    friend bool operator==(const PageSetup&, const PageSetup&) = default;
};

} // namespace musacad::core
