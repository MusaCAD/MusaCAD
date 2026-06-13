// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

#include <cstddef>
#include <cstdint>

#include "musacad/core/math/math.hpp"
#include "musacad/render/gpu/gpu_types.hpp"

namespace musacad::render {

class GpuBuffer;
class GpuPipeline;

/// Records (or, for the immediate-mode GL backend, issues) draw work for one
/// frame. The record/begin/submit shape matches a deferred backend (Vulkan)
/// while the GL backend executes calls immediately with the context current.
class GpuCommandBuffer {
public:
    virtual ~GpuCommandBuffer() = default;

    GpuCommandBuffer() = default;
    GpuCommandBuffer(const GpuCommandBuffer&) = delete;
    GpuCommandBuffer& operator=(const GpuCommandBuffer&) = delete;
    GpuCommandBuffer(GpuCommandBuffer&&) = delete;
    GpuCommandBuffer& operator=(GpuCommandBuffer&&) = delete;

    virtual void begin() = 0;
    virtual void end() = 0;

    virtual void set_viewport(int x, int y, int width, int height) = 0;
    virtual void clear(ClearColor color) = 0;

    virtual void bind_pipeline(const GpuPipeline& pipeline) = 0;
    virtual void bind_vertex_buffer(std::uint32_t binding, const GpuBuffer& buffer,
                                    std::size_t offset = 0) = 0;

    virtual void set_uniform_mat3(const char* name, const core::Mat3& m) = 0;
    virtual void set_uniform_float(const char* name, float value) = 0;
    virtual void set_uniform_vec2(const char* name, float x, float y) = 0;
    virtual void set_uniform_vec4(const char* name, float r, float g, float b, float a) = 0;

    /// Instanced draw: `vertex_count` base vertices, `instance_count` instances.
    virtual void draw_instanced(std::uint32_t vertex_count, std::uint32_t instance_count) = 0;
};

} // namespace musacad::render
