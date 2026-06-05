#pragma once

#include <string_view>

namespace musacad::command {

/// Identifies the command module. The AutoCAD-style command-line parser and
/// coordinate input arrive in Phase 4; this is a seam.
[[nodiscard]] std::string_view module_name() noexcept;

} // namespace musacad::command
