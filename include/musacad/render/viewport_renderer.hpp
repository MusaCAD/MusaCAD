#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "musacad/core/render_snapshot.hpp"
#include "musacad/render/camera.hpp"
#include "musacad/render/overlay.hpp"
#include "musacad/render/gpu/buffer.hpp"
#include "musacad/render/gpu/command_buffer.hpp"
#include "musacad/render/gpu/device.hpp"
#include "musacad/render/gpu/pipeline.hpp"
#include "musacad/render/gpu/swapchain.hpp"

namespace musacad::render {

/// Per-frame rendering metrics. `scene_uploaded_bytes` is the bytes pushed to
/// GPU for scene geometry this frame -- it is 0 on camera-only frames, which is
/// how constraint B (zoom/pan independent of edit load) is measured.
struct RenderStats {
    std::uint32_t draw_calls = 0;
    std::size_t line_instances = 0;
    std::size_t point_instances = 0;
    std::size_t grid_lines = 0;
    std::size_t scene_uploaded_bytes = 0;
    std::size_t grid_uploaded_bytes = 0;
    std::uint64_t uploaded_version = 0;
};

/// Draws a RenderSnapshot through a backend-agnostic GpuDevice. Takes a snapshot
/// and a camera ONLY -- it never sees the GeometryStore or the geometry thread.
/// Scene instance buffers are re-uploaded only when the snapshot version
/// changes; pan/zoom merely updates a uniform, so camera motion costs no
/// re-upload.
class ViewportRenderer {
public:
    explicit ViewportRenderer(GpuDevice& device);

    void render(GpuRenderTarget& target, const core::RenderSnapshot& snapshot,
                const Camera2D& camera);

    /// Overlay text (e.g. "FPS 144") drawn in screen space; set per frame.
    void set_overlay_text(std::string text) { overlay_text_ = std::move(text); }

    /// Crosshair state (render-side; from the raw cursor, every frame).
    void set_cursor(bool visible, float screen_x, float screen_y) {
        cursor_visible_ = visible;
        cursor_x_ = screen_x;
        cursor_y_ = screen_y;
    }
    void set_grid_visible(bool visible) { grid_visible_ = visible; }

    /// Transient interaction overlay (preview, selection rect, ghost), composed
    /// on the UI thread and handed in each frame.
    void set_overlay(RenderOverlay overlay) { overlay_ = std::move(overlay); }

    [[nodiscard]] const RenderStats& stats() const noexcept { return stats_; }

private:
    void upload_scene(const core::RenderSnapshot& snapshot);
    void draw_overlay(GpuCommandBuffer& cmd, int width, int height);
    void draw_crosshair_and_snap(GpuCommandBuffer& cmd, int width, int height,
                                 const core::RenderSnapshot& snapshot, const Camera2D& camera);
    void draw_selection_and_interaction(GpuCommandBuffer& cmd, const core::RenderSnapshot& snapshot,
                                        const core::Mat3& view);

    GpuDevice& device_;
    std::unique_ptr<GpuPipeline> line_pipeline_;
    std::unique_ptr<GpuPipeline> point_pipeline_;
    std::unique_ptr<GpuPipeline> thick_pipeline_; // scene lines (screen-space width)
    std::unique_ptr<GpuPipeline> fill_pipeline_;  // filled triangles (arrowheads)
    std::unique_ptr<GpuBuffer> line_instances_;
    std::unique_ptr<GpuBuffer> point_instances_;
    std::unique_ptr<GpuBuffer> fill_buffer_;
    std::unique_ptr<GpuBuffer> grid_minor_;
    std::unique_ptr<GpuBuffer> grid_major_;
    std::unique_ptr<GpuBuffer> overlay_buffer_;
    std::unique_ptr<GpuBuffer> aux_buffer_;
    std::unique_ptr<GpuCommandBuffer> cmd_;

    std::vector<float> scratch_;
    std::string overlay_text_;

    bool cursor_visible_ = false;
    float cursor_x_ = 0.0f;
    float cursor_y_ = 0.0f;
    bool grid_visible_ = true;
    RenderOverlay overlay_;

    std::uint64_t uploaded_version_ = ~0ull;
    std::size_t line_count_ = 0;  ///< scene line instances currently on GPU
    std::size_t point_count_ = 0; ///< scene point instances currently on GPU
    std::size_t fill_count_ = 0;  ///< scene fill vertices currently on GPU
    // Per-colour(+weight) batches over the uploaded scene buffers (cached at upload
    // time; one small draw per batch resolves per-entity colour/lineweight).
    std::vector<core::ColorBatch> line_batches_;
    std::vector<core::ColorBatch> point_batches_;
    std::vector<core::ColorBatch> fill_batches_;

    RenderStats stats_;
};

} // namespace musacad::render
