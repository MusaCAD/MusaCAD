// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

#include <cstddef>

namespace musacad::render {

/// RAII handle to a GPU buffer. Backend-agnostic; the GL backend owns a buffer
/// object, a Vulkan backend would own a VkBuffer + allocation.
class GpuBuffer {
public:
    virtual ~GpuBuffer() = default;

    GpuBuffer() = default;
    GpuBuffer(const GpuBuffer&) = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;
    GpuBuffer(GpuBuffer&&) = delete;
    GpuBuffer& operator=(GpuBuffer&&) = delete;

    /// Replaces the buffer contents, growing the allocation if needed.
    virtual void upload(const void* data, std::size_t bytes) = 0;

    [[nodiscard]] virtual std::size_t size_bytes() const noexcept = 0;
};

} // namespace musacad::render
