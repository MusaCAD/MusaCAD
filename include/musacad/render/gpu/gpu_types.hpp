#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace musacad::render {

/// How a GPU buffer's contents change over time (maps to GL usage hints / a
/// Vulkan memory type).
enum class BufferUsage : std::uint8_t { Static, Dynamic, Stream };

/// Primitive topology for a draw.
enum class Topology : std::uint8_t { Points, Lines, TriangleStrip, Triangles };

/// One vertex attribute, sourced from a buffer bound at `binding`. `divisor`
/// of 1 makes it per-instance (instanced rendering); 0 is per-vertex.
struct VertexAttribute {
    std::uint32_t location = 0;
    std::uint32_t binding = 0;
    std::uint32_t components = 4; ///< number of floats (1..4)
    std::uint32_t offset = 0;     ///< byte offset within the binding's stride
    std::uint32_t divisor = 0;
};

/// A vertex buffer binding point and its per-element stride in bytes.
struct VertexBinding {
    std::uint32_t binding = 0;
    std::uint32_t stride = 0;
};

/// Everything needed to build a pipeline (shader program + vertex layout).
struct PipelineDesc {
    std::string vertex_src;
    std::string fragment_src;
    std::vector<VertexAttribute> attributes;
    std::vector<VertexBinding> bindings;
    Topology topology = Topology::Lines;
};

struct ClearColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

} // namespace musacad::render
