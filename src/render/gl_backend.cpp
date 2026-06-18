// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/render/gl_backend.hpp"

#include <QOpenGLContext>
#include <QOpenGLVersionFunctionsFactory>

#include "gl/gl_objects.hpp"

namespace musacad::render {

namespace {

gl::GlFns* current_functions() {
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (ctx == nullptr) {
        return nullptr;
    }
    auto* fns = QOpenGLVersionFunctionsFactory::get<gl::GlFns>(ctx);
    if (fns != nullptr) {
        fns->initializeOpenGLFunctions();
    }
    return fns;
}

} // namespace

std::unique_ptr<GpuDevice> create_gl_device() {
    gl::GlFns* fns = current_functions();
    if (fns == nullptr) {
        return nullptr;
    }
    // 2D rendering: no depth test; allow the vertex shader to set point size.
    fns->glDisable(GL_DEPTH_TEST);
    fns->glEnable(GL_PROGRAM_POINT_SIZE);
    // Alpha blending for antialiased stroke edges (thickline.frag emits ~1px coverage
    // on the capsule boundary). All opaque geometry writes alpha 1, so interiors are
    // unaffected; only stroke edges feather against the background. Standard
    // src-alpha / one-minus-src-alpha.
    fns->glEnable(GL_BLEND);
    fns->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    return std::make_unique<gl::GlDevice>(fns);
}

std::unique_ptr<GpuRenderTarget> create_gl_default_target(int width, int height) {
    gl::GlFns* fns = current_functions();
    if (fns == nullptr) {
        return nullptr;
    }
    return std::make_unique<gl::GlDefaultTarget>(fns, width, height);
}

std::unique_ptr<GpuRenderTarget> create_gl_offscreen_target(int width, int height) {
    gl::GlFns* fns = current_functions();
    if (fns == nullptr) {
        return nullptr;
    }
    return std::make_unique<gl::GlOffscreenTarget>(fns, width, height);
}

std::vector<std::uint8_t> read_offscreen_rgba(GpuRenderTarget& target) {
    if (auto* off = dynamic_cast<gl::GlOffscreenTarget*>(&target)) {
        return off->read_rgba();
    }
    return {};
}

} // namespace musacad::render
