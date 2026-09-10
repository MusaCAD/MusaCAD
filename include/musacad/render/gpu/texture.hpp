// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>

namespace musacad::render {

/// An immutable 2D RGBA8 texture on the GPU (a decoded raster image). Created with its
/// pixels by GpuDevice::create_texture; sampled by the image pipeline.
class GpuTexture {
public:
    virtual ~GpuTexture() = default;

    GpuTexture() = default;
    GpuTexture(const GpuTexture&) = delete;
    GpuTexture& operator=(const GpuTexture&) = delete;
    GpuTexture(GpuTexture&&) = delete;
    GpuTexture& operator=(GpuTexture&&) = delete;

    [[nodiscard]] virtual std::uint32_t width() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t height() const noexcept = 0;
};

} // namespace musacad::render
