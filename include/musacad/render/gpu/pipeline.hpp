// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

namespace musacad::render {

/// RAII handle to a compiled/linked pipeline (shader program + vertex layout).
/// Opaque at the interface; the backend owns the concrete objects.
class GpuPipeline {
public:
    virtual ~GpuPipeline() = default;

    GpuPipeline() = default;
    GpuPipeline(const GpuPipeline&) = delete;
    GpuPipeline& operator=(const GpuPipeline&) = delete;
    GpuPipeline(GpuPipeline&&) = delete;
    GpuPipeline& operator=(GpuPipeline&&) = delete;
};

} // namespace musacad::render
