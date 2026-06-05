#pragma once

#include <string_view>

namespace musacad::render {

/// Identifies the render module. The GPU abstraction (Vulkan/OpenGL) and the
/// snapshot-consuming viewport renderer arrive in Phase 3; this is a seam.
[[nodiscard]] std::string_view module_name() noexcept;

} // namespace musacad::render
