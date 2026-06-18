// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "gl/gl_objects.hpp"

#include <array>
#include <cstdio>

namespace musacad::render::gl {

namespace {

GLuint compile_shader(GlFns* gl, GLenum type, const char* src) {
    const GLuint shader = gl->glCreateShader(type);
    gl->glShaderSource(shader, 1, &src, nullptr);
    gl->glCompileShader(shader);
    GLint ok = GL_FALSE;
    gl->glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        std::array<char, 2048> log{};
        gl->glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
        std::fprintf(stderr, "[musacad_render] shader compile failed: %s\n", log.data());
    }
    return shader;
}

} // namespace

// ----------------------------------------------------------------------------
// GlBuffer
// ----------------------------------------------------------------------------

GlBuffer::GlBuffer(GlFns* gl, BufferUsage usage) : gl_(gl), usage_(to_gl_usage(usage)) {
    gl_->glCreateBuffers(1, &id_);
}

GlBuffer::~GlBuffer() {
    if (id_ != 0) {
        gl_->glDeleteBuffers(1, &id_);
    }
}

void GlBuffer::upload(const void* data, std::size_t bytes) {
    if (bytes > capacity_) {
        gl_->glNamedBufferData(id_, static_cast<GLsizeiptr>(bytes), data, usage_);
        capacity_ = bytes;
    } else if (bytes > 0) {
        gl_->glNamedBufferSubData(id_, 0, static_cast<GLsizeiptr>(bytes), data);
    }
    size_ = bytes;
}

// ----------------------------------------------------------------------------
// GlPipeline
// ----------------------------------------------------------------------------

GlPipeline::GlPipeline(GlFns* gl, const PipelineDesc& desc)
    : gl_(gl), topology_(to_gl_topology(desc.topology)) {
    const GLuint vs = compile_shader(gl_, GL_VERTEX_SHADER, desc.vertex_src.c_str());
    const GLuint fs = compile_shader(gl_, GL_FRAGMENT_SHADER, desc.fragment_src.c_str());

    program_ = gl_->glCreateProgram();
    gl_->glAttachShader(program_, vs);
    gl_->glAttachShader(program_, fs);
    gl_->glLinkProgram(program_);
    GLint linked = GL_FALSE;
    gl_->glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        std::array<char, 2048> log{};
        gl_->glGetProgramInfoLog(program_, static_cast<GLsizei>(log.size()), nullptr, log.data());
        std::fprintf(stderr, "[musacad_render] program link failed: %s\n", log.data());
    }
    gl_->glDetachShader(program_, vs);
    gl_->glDetachShader(program_, fs);
    gl_->glDeleteShader(vs);
    gl_->glDeleteShader(fs);

    gl_->glCreateVertexArrays(1, &vao_);
    for (const VertexAttribute& a : desc.attributes) {
        gl_->glEnableVertexArrayAttrib(vao_, a.location);
        gl_->glVertexArrayAttribFormat(vao_, a.location, static_cast<GLint>(a.components), GL_FLOAT,
                                       GL_FALSE, a.offset);
        gl_->glVertexArrayAttribBinding(vao_, a.location, a.binding);
        gl_->glVertexArrayBindingDivisor(vao_, a.binding, a.divisor);
    }
    for (const VertexBinding& b : desc.bindings) {
        strides_.emplace_back(b.binding, static_cast<GLsizei>(b.stride));
    }
}

GlPipeline::~GlPipeline() {
    if (vao_ != 0) {
        gl_->glDeleteVertexArrays(1, &vao_);
    }
    if (program_ != 0) {
        gl_->glDeleteProgram(program_);
    }
}

GLsizei GlPipeline::stride_for(std::uint32_t binding) const {
    for (const auto& [b, stride] : strides_) {
        if (b == binding) {
            return stride;
        }
    }
    return 0;
}

GLint GlPipeline::uniform(const char* name) const {
    auto it = uniforms_.find(name);
    if (it != uniforms_.end()) {
        return it->second;
    }
    const GLint loc = gl_->glGetUniformLocation(program_, name);
    uniforms_.emplace(name, loc);
    return loc;
}

// ----------------------------------------------------------------------------
// GlCommandBuffer (immediate-mode)
// ----------------------------------------------------------------------------

void GlCommandBuffer::set_viewport(int x, int y, int width, int height) {
    gl_->glViewport(x, y, width, height);
}

void GlCommandBuffer::clear(ClearColor color) {
    gl_->glClearColor(color.r, color.g, color.b, color.a);
    gl_->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void GlCommandBuffer::bind_pipeline(const GpuPipeline& pipeline) {
    pipeline_ = static_cast<const GlPipeline*>(&pipeline);
    gl_->glUseProgram(pipeline_->program());
    gl_->glBindVertexArray(pipeline_->vao());
}

void GlCommandBuffer::bind_vertex_buffer(std::uint32_t binding, const GpuBuffer& buffer,
                                         std::size_t offset) {
    const auto& buf = static_cast<const GlBuffer&>(buffer);
    gl_->glVertexArrayVertexBuffer(pipeline_->vao(), binding, buf.id(),
                                   static_cast<GLintptr>(offset), pipeline_->stride_for(binding));
}

void GlCommandBuffer::set_uniform_mat3(const char* name, const core::Mat3& m) {
    std::array<float, 9> f{};
    for (std::size_t i = 0; i < 9; ++i) {
        f[i] = static_cast<float>(m.m[i]);
    }
    gl_->glProgramUniformMatrix3fv(pipeline_->program(), pipeline_->uniform(name), 1, GL_FALSE,
                                   f.data());
}

void GlCommandBuffer::set_uniform_float(const char* name, float value) {
    gl_->glProgramUniform1f(pipeline_->program(), pipeline_->uniform(name), value);
}

void GlCommandBuffer::set_uniform_vec2(const char* name, float x, float y) {
    gl_->glProgramUniform2f(pipeline_->program(), pipeline_->uniform(name), x, y);
}

void GlCommandBuffer::set_uniform_vec4(const char* name, float r, float g, float b, float a) {
    gl_->glProgramUniform4f(pipeline_->program(), pipeline_->uniform(name), r, g, b, a);
}

void GlCommandBuffer::draw_instanced(std::uint32_t vertex_count, std::uint32_t instance_count) {
    if (instance_count == 0) {
        return;
    }
    gl_->glDrawArraysInstanced(pipeline_->topology(), 0, static_cast<GLsizei>(vertex_count),
                               static_cast<GLsizei>(instance_count));
}

// ----------------------------------------------------------------------------
// GlDevice
// ----------------------------------------------------------------------------

std::unique_ptr<GpuBuffer> GlDevice::create_buffer(BufferUsage usage) {
    return std::make_unique<GlBuffer>(gl_, usage);
}

std::unique_ptr<GpuPipeline> GlDevice::create_pipeline(const PipelineDesc& desc) {
    return std::make_unique<GlPipeline>(gl_, desc);
}

std::unique_ptr<GpuCommandBuffer> GlDevice::create_command_buffer() {
    return std::make_unique<GlCommandBuffer>(gl_);
}

void GlDevice::submit(GpuCommandBuffer& /*commands*/) {
    // Immediate-mode backend: work has already been issued. Flush to the driver.
    gl_->glFlush();
}

// ----------------------------------------------------------------------------
// GlOffscreenTarget
// ----------------------------------------------------------------------------

GlOffscreenTarget::GlOffscreenTarget(GlFns* gl, int width, int height)
    : gl_(gl), w_(width), h_(height) {
    create();
}

GlOffscreenTarget::~GlOffscreenTarget() { destroy(); }

void GlOffscreenTarget::create() {
    gl_->glCreateFramebuffers(1, &fbo_);
    gl_->glCreateTextures(GL_TEXTURE_2D, 1, &color_);
    gl_->glTextureStorage2D(color_, 1, GL_RGBA8, w_, h_);
    gl_->glTextureParameteri(color_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl_->glTextureParameteri(color_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl_->glCreateRenderbuffers(1, &depth_);
    gl_->glNamedRenderbufferStorage(depth_, GL_DEPTH_COMPONENT24, w_, h_);
    gl_->glNamedFramebufferTexture(fbo_, GL_COLOR_ATTACHMENT0, color_, 0);
    gl_->glNamedFramebufferRenderbuffer(fbo_, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_);
    if (gl_->glCheckNamedFramebufferStatus(fbo_, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "[musacad_render] offscreen framebuffer incomplete\n");
    }
}

void GlOffscreenTarget::destroy() {
    if (depth_ != 0) {
        gl_->glDeleteRenderbuffers(1, &depth_);
    }
    if (color_ != 0) {
        gl_->glDeleteTextures(1, &color_);
    }
    if (fbo_ != 0) {
        gl_->glDeleteFramebuffers(1, &fbo_);
    }
    fbo_ = color_ = depth_ = 0;
}

void GlOffscreenTarget::bind() { gl_->glBindFramebuffer(GL_FRAMEBUFFER, fbo_); }

void GlOffscreenTarget::resize(int width, int height) {
    if (width == w_ && height == h_) {
        return;
    }
    destroy();
    w_ = width;
    h_ = height;
    create();
}

std::vector<std::uint8_t> GlOffscreenTarget::read_rgba() {
    bind();
    gl_->glPixelStorei(GL_PACK_ALIGNMENT, 1);
    const auto row = static_cast<std::size_t>(w_) * 4;
    std::vector<std::uint8_t> pixels(row * static_cast<std::size_t>(h_));
    gl_->glReadPixels(0, 0, w_, h_, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    // glReadPixels returns rows bottom-to-top; flip to top-to-bottom.
    std::vector<std::uint8_t> flipped(pixels.size());
    for (int y = 0; y < h_; ++y) {
        const auto src = static_cast<std::size_t>(h_ - 1 - y) * row;
        const auto dst = static_cast<std::size_t>(y) * row;
        std::copy_n(pixels.begin() + static_cast<std::ptrdiff_t>(src), row,
                    flipped.begin() + static_cast<std::ptrdiff_t>(dst));
    }
    return flipped;
}

} // namespace musacad::render::gl
