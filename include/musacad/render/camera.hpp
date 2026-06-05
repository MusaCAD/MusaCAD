#pragma once

#include "musacad/core/math/math.hpp"

namespace musacad::render {

using core::Mat3;
using core::Vec2;

/// 2D viewport camera. Pure CPU transform: it lives entirely on the render/UI
/// side and never needs a geometry-thread round-trip, so pan/zoom stay smooth
/// regardless of edit activity.
///
/// Conventions: world space is y-up. Screen space is pixels with origin at the
/// top-left and y-down (Qt convention). NDC (clip) space is y-up, [-1,1].
/// `scale` is pixels-per-world-unit.
class Camera2D {
public:
    Camera2D() = default;

    void set_viewport(int width_px, int height_px) noexcept;
    [[nodiscard]] int viewport_width() const noexcept { return width_; }
    [[nodiscard]] int viewport_height() const noexcept { return height_; }

    [[nodiscard]] Vec2 center() const noexcept { return center_; }
    void set_center(Vec2 c) noexcept { center_ = c; }

    /// Pixels per world unit (always > 0).
    [[nodiscard]] double scale() const noexcept { return scale_; }
    void set_scale(double s) noexcept;

    [[nodiscard]] Vec2 world_to_screen(Vec2 world) const noexcept;
    [[nodiscard]] Vec2 screen_to_world(Vec2 screen) const noexcept;

    /// Translate the view by a screen-pixel delta (e.g. a mouse drag), keeping
    /// the grabbed world point under the cursor.
    void pan_pixels(Vec2 screen_delta) noexcept;

    /// Multiply zoom by `factor` while keeping the world point under
    /// `screen_anchor` fixed on screen (zoom-about-cursor).
    void zoom_about(Vec2 screen_anchor, double factor) noexcept;

    /// Frame the given world-space AABB with a margin fraction (e.g. 0.1).
    void frame_bounds(Vec2 min_world, Vec2 max_world, double margin = 0.1) noexcept;

    /// World-space transform mapping world coordinates directly to NDC, for the
    /// vertex shader.
    [[nodiscard]] Mat3 view_matrix() const noexcept;

    /// Visible world-space AABB corners.
    [[nodiscard]] Vec2 visible_min() const noexcept;
    [[nodiscard]] Vec2 visible_max() const noexcept;

private:
    Vec2 center_{0.0, 0.0};
    double scale_ = 1.0;
    int width_ = 1;
    int height_ = 1;
};

} // namespace musacad::render
