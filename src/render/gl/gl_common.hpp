// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

// Private to the render lib. Pulls in the Qt-provided OpenGL 4.5 Core function
// set, which is how we load GL entry points without a separate GLAD/GLEW
// dependency. None of this leaks into a public musacad/ header.

#include <QOpenGLFunctions_4_5_Core>

#include "musacad/render/gpu/gpu_types.hpp"

namespace musacad::render::gl {

using GlFns = QOpenGLFunctions_4_5_Core;

inline GLenum to_gl_usage(BufferUsage usage) noexcept {
    switch (usage) {
    case BufferUsage::Static:
        return GL_STATIC_DRAW;
    case BufferUsage::Dynamic:
        return GL_DYNAMIC_DRAW;
    case BufferUsage::Stream:
        return GL_STREAM_DRAW;
    }
    return GL_STATIC_DRAW;
}

inline GLenum to_gl_topology(Topology topology) noexcept {
    switch (topology) {
    case Topology::Points:
        return GL_POINTS;
    case Topology::TriangleStrip:
        return GL_TRIANGLE_STRIP;
    case Topology::Triangles:
        return GL_TRIANGLES;
    case Topology::Lines:
        break;
    }
    return GL_LINES;
}

} // namespace musacad::render::gl
