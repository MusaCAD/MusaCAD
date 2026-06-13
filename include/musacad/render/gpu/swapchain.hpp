// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

namespace musacad::render {

/// A render target the renderer draws into (the swapchain image for an onscreen
/// window, or an offscreen framebuffer for headless rendering/tests). Binding
/// makes it the active framebuffer; presentation/readback is target-specific.
class GpuRenderTarget {
public:
    virtual ~GpuRenderTarget() = default;

    GpuRenderTarget() = default;
    GpuRenderTarget(const GpuRenderTarget&) = delete;
    GpuRenderTarget& operator=(const GpuRenderTarget&) = delete;
    GpuRenderTarget(GpuRenderTarget&&) = delete;
    GpuRenderTarget& operator=(GpuRenderTarget&&) = delete;

    virtual void bind() = 0;
    virtual void resize(int width, int height) = 0;
    [[nodiscard]] virtual int width() const noexcept = 0;
    [[nodiscard]] virtual int height() const noexcept = 0;
};

} // namespace musacad::render
