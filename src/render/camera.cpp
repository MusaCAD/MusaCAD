#include "musacad/render/camera.hpp"

#include <algorithm>

namespace musacad::render {

namespace {
constexpr double kMinScale = 1e-9;
constexpr double kMaxScale = 1e12;
} // namespace

void Camera2D::set_viewport(int width_px, int height_px) noexcept {
    width_ = std::max(1, width_px);
    height_ = std::max(1, height_px);
}

void Camera2D::set_scale(double s) noexcept {
    scale_ = std::clamp(s, kMinScale, kMaxScale);
}

Vec2 Camera2D::world_to_screen(Vec2 world) const noexcept {
    const double dx = (world.x - center_.x) * scale_;
    const double dy = (world.y - center_.y) * scale_;
    return {static_cast<double>(width_) * 0.5 + dx, static_cast<double>(height_) * 0.5 - dy};
}

Vec2 Camera2D::screen_to_world(Vec2 screen) const noexcept {
    const double dx = screen.x - static_cast<double>(width_) * 0.5;
    const double dy = static_cast<double>(height_) * 0.5 - screen.y;
    return {center_.x + dx / scale_, center_.y + dy / scale_};
}

void Camera2D::pan_pixels(Vec2 screen_delta) noexcept {
    // Keep the world point under the cursor as the cursor moves by screen_delta.
    center_.x -= screen_delta.x / scale_;
    center_.y += screen_delta.y / scale_;
}

void Camera2D::zoom_about(Vec2 screen_anchor, double factor) noexcept {
    const Vec2 world_before = screen_to_world(screen_anchor);
    set_scale(scale_ * factor);
    // Solve for the center that keeps world_before under screen_anchor.
    const double ax = screen_anchor.x - static_cast<double>(width_) * 0.5;
    const double ay = static_cast<double>(height_) * 0.5 - screen_anchor.y;
    center_.x = world_before.x - ax / scale_;
    center_.y = world_before.y - ay / scale_;
}

void Camera2D::frame_bounds(Vec2 min_world, Vec2 max_world, double margin) noexcept {
    center_ = (min_world + max_world) * 0.5;
    const double w = std::max(max_world.x - min_world.x, 1e-9);
    const double h = std::max(max_world.y - min_world.y, 1e-9);
    const double sx = static_cast<double>(width_) / (w * (1.0 + 2.0 * margin));
    const double sy = static_cast<double>(height_) / (h * (1.0 + 2.0 * margin));
    set_scale(std::min(sx, sy));
}

Mat3 Camera2D::view_matrix() const noexcept {
    const double sx = scale_ * 2.0 / static_cast<double>(width_);
    const double sy = scale_ * 2.0 / static_cast<double>(height_);
    Mat3 m = Mat3::identity();
    m.m[0] = sx;
    m.m[4] = sy;
    m.m[6] = -sx * center_.x;
    m.m[7] = -sy * center_.y;
    return m;
}

Vec2 Camera2D::visible_min() const noexcept {
    const Vec2 a = screen_to_world({0.0, static_cast<double>(height_)});
    const Vec2 b = screen_to_world({static_cast<double>(width_), 0.0});
    return {std::min(a.x, b.x), std::min(a.y, b.y)};
}

Vec2 Camera2D::visible_max() const noexcept {
    const Vec2 a = screen_to_world({0.0, static_cast<double>(height_)});
    const Vec2 b = screen_to_world({static_cast<double>(width_), 0.0});
    return {std::max(a.x, b.x), std::max(a.y, b.y)};
}

} // namespace musacad::render
