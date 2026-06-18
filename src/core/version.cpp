// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/version.hpp"

#ifndef MUSACAD_VERSION
#define MUSACAD_VERSION "0.0.0"
#endif

namespace musacad::core {

std::string_view app_name() noexcept {
    return "Musa CAD";
}

std::string_view version_string() noexcept {
    return MUSACAD_VERSION;
}

} // namespace musacad::core
