#pragma once

#include <memory>

#include "musacad/render/gpu/gpu_types.hpp"

namespace musacad::render {

class GpuBuffer;
class GpuPipeline;
class GpuCommandBuffer;

/// Backend entry point: creates GPU resources and submits command buffers. The
/// GL backend implements this over a current OpenGL context; a Vulkan backend
/// would implement the same interface over a VkDevice.
class GpuDevice {
public:
    virtual ~GpuDevice() = default;

    GpuDevice() = default;
    GpuDevice(const GpuDevice&) = delete;
    GpuDevice& operator=(const GpuDevice&) = delete;
    GpuDevice(GpuDevice&&) = delete;
    GpuDevice& operator=(GpuDevice&&) = delete;

    [[nodiscard]] virtual std::unique_ptr<GpuBuffer> create_buffer(BufferUsage usage) = 0;
    [[nodiscard]] virtual std::unique_ptr<GpuPipeline> create_pipeline(const PipelineDesc& desc) = 0;
    [[nodiscard]] virtual std::unique_ptr<GpuCommandBuffer> create_command_buffer() = 0;

    virtual void submit(GpuCommandBuffer& commands) = 0;

    [[nodiscard]] virtual const char* backend_name() const noexcept = 0;
};

} // namespace musacad::render
