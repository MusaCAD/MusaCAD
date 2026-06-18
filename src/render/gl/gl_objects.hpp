// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "musacad/render/gpu/buffer.hpp"
#include "musacad/render/gpu/command_buffer.hpp"
#include "musacad/render/gpu/device.hpp"
#include "musacad/render/gpu/pipeline.hpp"
#include "musacad/render/gpu/swapchain.hpp"

#include "gl/gl_common.hpp"

namespace musacad::render::gl {

class GlBuffer final : public GpuBuffer {
public:
    GlBuffer(GlFns* gl, BufferUsage usage);
    ~GlBuffer() override;

    void upload(const void* data, std::size_t bytes) override;
    [[nodiscard]] std::size_t size_bytes() const noexcept override { return size_; }
    [[nodiscard]] GLuint id() const noexcept { return id_; }

private:
    GlFns* gl_;
    GLuint id_ = 0;
    GLenum usage_;
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;
};

class GlPipeline final : public GpuPipeline {
public:
    GlPipeline(GlFns* gl, const PipelineDesc& desc);
    ~GlPipeline() override;

    [[nodiscard]] GLuint program() const noexcept { return program_; }
    [[nodiscard]] GLuint vao() const noexcept { return vao_; }
    [[nodiscard]] GLenum topology() const noexcept { return topology_; }
    [[nodiscard]] GLsizei stride_for(std::uint32_t binding) const;
    [[nodiscard]] GLint uniform(const char* name) const;

private:
    GlFns* gl_;
    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLenum topology_ = GL_LINES;
    std::vector<std::pair<std::uint32_t, GLsizei>> strides_;
    mutable std::unordered_map<std::string, GLint> uniforms_;
};

class GlCommandBuffer final : public GpuCommandBuffer {
public:
    explicit GlCommandBuffer(GlFns* gl) : gl_(gl) {}

    void begin() override {}
    void end() override {}
    void set_viewport(int x, int y, int width, int height) override;
    void clear(ClearColor color) override;
    void bind_pipeline(const GpuPipeline& pipeline) override;
    void bind_vertex_buffer(std::uint32_t binding, const GpuBuffer& buffer,
                            std::size_t offset) override;
    void set_uniform_mat3(const char* name, const core::Mat3& m) override;
    void set_uniform_float(const char* name, float value) override;
    void set_uniform_vec2(const char* name, float x, float y) override;
    void set_uniform_vec4(const char* name, float r, float g, float b, float a) override;
    void draw_instanced(std::uint32_t vertex_count, std::uint32_t instance_count) override;

private:
    GlFns* gl_;
    const GlPipeline* pipeline_ = nullptr;
};

class GlDevice final : public GpuDevice {
public:
    explicit GlDevice(GlFns* gl) : gl_(gl) {}

    std::unique_ptr<GpuBuffer> create_buffer(BufferUsage usage) override;
    std::unique_ptr<GpuPipeline> create_pipeline(const PipelineDesc& desc) override;
    std::unique_ptr<GpuCommandBuffer> create_command_buffer() override;
    void submit(GpuCommandBuffer& commands) override;
    [[nodiscard]] const char* backend_name() const noexcept override {
        return "OpenGL 4.5 Core (DSA)";
    }

private:
    GlFns* gl_;
};

/// Render target wrapping the default framebuffer (an onscreen window surface).
class GlDefaultTarget final : public GpuRenderTarget {
public:
    GlDefaultTarget(GlFns* gl, int width, int height) : gl_(gl), w_(width), h_(height) {}
    void bind() override { gl_->glBindFramebuffer(GL_FRAMEBUFFER, 0); }
    void resize(int width, int height) override {
        w_ = width;
        h_ = height;
    }
    [[nodiscard]] int width() const noexcept override { return w_; }
    [[nodiscard]] int height() const noexcept override { return h_; }

private:
    GlFns* gl_;
    int w_;
    int h_;
};

/// Offscreen framebuffer (RGBA8 color + depth) with pixel readback, for
/// headless rendering and tests.
class GlOffscreenTarget final : public GpuRenderTarget {
public:
    GlOffscreenTarget(GlFns* gl, int width, int height);
    ~GlOffscreenTarget() override;

    void bind() override;
    void resize(int width, int height) override;
    [[nodiscard]] int width() const noexcept override { return w_; }
    [[nodiscard]] int height() const noexcept override { return h_; }

    /// Reads back the color attachment as RGBA8, rows top-to-bottom.
    [[nodiscard]] std::vector<std::uint8_t> read_rgba();

private:
    void create();
    void destroy();

    GlFns* gl_;
    GLuint fbo_ = 0;
    GLuint color_ = 0;
    GLuint depth_ = 0;
    int w_;
    int h_;
};

} // namespace musacad::render::gl
