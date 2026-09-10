// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <string>

#include "musacad/core/page_setup.hpp"

namespace musacad::core {

/// A layout (a sheet of paper space): a stable id that entities refer to through
/// EntityProps::space, a name, and its page setup (paper size and plot settings). Paper
/// space units are millimetres on the sheet, the sheet's corner at the origin.
struct Layout {
    std::uint8_t id = 1;
    std::string name;
    PageSetup page;
    friend bool operator==(const Layout&, const Layout&) = default;
};

} // namespace musacad::core
