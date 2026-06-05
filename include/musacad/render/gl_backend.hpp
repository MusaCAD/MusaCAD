#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "musacad/render/gpu/device.hpp"
#include "musacad/render/gpu/swapchain.hpp"

namespace musacad::render {

/// Creates the OpenGL GpuDevice from the QOpenGLContext that is current on the
/// calling thread. Returns nullptr if no suitable 4.5 context is current.
[[nodiscard]] std::unique_ptr<GpuDevice> create_gl_device();

/// Creates an onscreen render target (default framebuffer) of the given size.
[[nodiscard]] std::unique_ptr<GpuRenderTarget> create_gl_default_target(int width, int height);

/// Creates an offscreen render target (RGBA8 + depth) for headless rendering.
[[nodiscard]] std::unique_ptr<GpuRenderTarget> create_gl_offscreen_target(int width, int height);

/// Reads an offscreen target's color buffer as RGBA8 (rows top-to-bottom).
/// Returns empty for a non-offscreen target.
[[nodiscard]] std::vector<std::uint8_t> read_offscreen_rgba(GpuRenderTarget& target);

} // namespace musacad::render
