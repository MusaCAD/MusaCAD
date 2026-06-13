// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

#include <string_view>

namespace musacad::core {

/// Application name.
[[nodiscard]] std::string_view app_name() noexcept;

/// Semantic version string, e.g. "0.1.0", sourced from the build system.
[[nodiscard]] std::string_view version_string() noexcept;

} // namespace musacad::core
