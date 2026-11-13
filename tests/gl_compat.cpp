// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
// Pixel-level contract for GL 4.1: offsets, multiple bindings, per-attribute divisors,
// buffer replacement, textures, depth, alpha, scissor, and framebuffer resize.
#include <array>
#include <cstdio>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#include "musacad/render/gl_backend.hpp"
#include "musacad/render/gpu/buffer.hpp"
#include "musacad/render/gpu/command_buffer.hpp"
#include "musacad/render/gpu/pipeline.hpp"
#include "musacad/render/gpu/texture.hpp"

using namespace musacad::render;

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setVersion(4, 1);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(fmt);
    QGuiApplication app(argc, argv);
    QOffscreenSurface surface;
    surface.setFormat(fmt);
    surface.create();
    QOpenGLContext context;
    context.setFormat(fmt);
    if (!surface.isValid() || !context.create() || !context.makeCurrent(&surface)) return 77;
    auto* gl = context.functions();
    std::printf("OpenGL: %s\n", gl->glGetString(GL_VERSION));
    auto device = create_gl_device();
    auto target = create_gl_offscreen_target(32, 32);
    if (!device || !target) return 1;
    int failures = 0;
    const auto check = [&](bool ok, const char* name) {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };
    PipelineDesc desc;
    desc.vertex_src = R"(#version 410 core
layout(location=0) in vec2 position;
layout(location=1) in vec2 shift;
layout(location=2) in float coverage;
uniform float depth;
out float alpha;
void main() { gl_Position=vec4(position+shift, depth, 1); alpha=coverage; }
)";
    desc.fragment_src = R"(#version 410 core
uniform sampler2D image;
uniform vec4 tint;
in float alpha;
out vec4 color;
void main() { color=texture(image, vec2(0.5))*tint; color.a*=alpha; }
)";
    desc.topology = Topology::Triangles;
    desc.attributes = {{0, 0, 2, 0, 0}, {1, 1, 2, 0, 1}, {2, 1, 1, 8, 1}};
    desc.bindings = {{0, 8}, {1, 12}};
    auto pipeline = device->create_pipeline(desc);
    auto vertices = device->create_buffer(BufferUsage::Static);
    auto instances = device->create_buffer(BufferUsage::Dynamic);
    // Prefix is deliberately offscreen: bind_vertex_buffer must respect the byte offset.
    const std::array<float, 14> positions = {99, 99, -1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
    vertices->upload(positions.data(), sizeof(positions));
    const std::array<float, 3> instance = {0, 0, 1};
    instances->upload(instance.data(), sizeof(instance));
    const std::array<std::uint8_t, 4> white = {255, 255, 255, 255};
    auto texture = device->create_texture(1, 1, white.data());
    auto cmd = device->create_command_buffer();
    target->bind();
    cmd->set_viewport(0, 0, 32, 32);
    cmd->clear({0, 0, 0, 1});
    cmd->bind_pipeline(*pipeline);
    cmd->bind_vertex_buffer(0, *vertices, 2 * sizeof(float));
    cmd->bind_vertex_buffer(1, *instances, 0);
    cmd->bind_texture(3, *texture);
    cmd->set_uniform_int("image", 3);
    cmd->set_uniform_float("depth", 0);
    cmd->set_uniform_vec4("tint", 1, 0, 0, 1);
    cmd->draw_instanced(6, 1);
    const auto pixel = [&](int x, int y, int r, int g, int b) {
        const auto pixels = read_offscreen_rgba(*target);
        const auto i = static_cast<std::size_t>((y * target->width() + x) * 4);
        const auto near = [](int a, int v) { return a >= v - 2 && a <= v + 2; };
        return near(pixels[i], r) && near(pixels[i + 1], g) && near(pixels[i + 2], b);
    };
    check(pixel(16, 16, 255, 0, 0), "texture unit, vertex offset and multiple attribute bindings");

    // Creating a texture while another is bound must not change the draw's sampler.
    const std::array<std::uint8_t, 4> blue = {0, 0, 255, 255};
    auto other_texture = device->create_texture(1, 1, blue.data());
    const std::array<float, 3> half = {0, 0, 0.5f};
    instances->upload(half.data(), sizeof(half)); // same-size subdata, VAO remains valid
    cmd->set_uniform_vec4("tint", 0, 1, 0, 1);
    cmd->draw_instanced(6, 1);
    check(pixel(16, 16, 127, 128, 0), "buffer subdata, per-instance alpha and texture binding preservation");

    const std::array<float, 6> two = {3, 0, 1, 0, 0, 1}; // only second instance is visible
    instances->upload(two.data(), sizeof(two)); // grow the existing buffer
    cmd->set_uniform_vec4("tint", 0, 0, 1, 1);
    cmd->draw_instanced(6, 2);
    check(pixel(16, 16, 0, 0, 255), "buffer growth and all per-instance attribute divisors");

    cmd->set_depth(true, true);
    cmd->set_uniform_float("depth", -0.5f);
    cmd->set_uniform_vec4("tint", 1, 0, 0, 1);
    cmd->draw_instanced(6, 2);
    cmd->set_uniform_float("depth", 0.5f);
    cmd->set_uniform_vec4("tint", 0, 1, 0, 1);
    cmd->draw_instanced(6, 2);
    check(pixel(16, 16, 255, 0, 0), "depth attachment preserves front geometry");
    cmd->set_depth(false, false);
    cmd->set_scissor(0, 0, 8, 32);
    cmd->draw_instanced(6, 2);
    check(pixel(3, 16, 0, 255, 0) && pixel(16, 16, 255, 0, 0), "scissor clips drawing");
    cmd->clear_scissor();

    target->resize(48, 24);
    target->bind();
    cmd->set_viewport(0, 0, 48, 24);
    cmd->clear({0, 0, 0, 1});
    cmd->bind_texture(3, *other_texture);
    cmd->set_uniform_vec4("tint", 1, 1, 1, 1);
    cmd->draw_instanced(6, 2);
    check(pixel(24, 12, 0, 0, 255), "resized framebuffer and texture replacement");
    check(gl->glGetError() == GL_NO_ERROR, "no OpenGL errors");
    std::printf("%s\n", failures == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED");
    // Resource destructors run before context destruction while it is still current.
    return failures == 0 ? 0 : 1;
}
