// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/image.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace musacad::core {

namespace {

/// Clip fractions, clamped and ordered so a malformed clip can never invert the quad.
std::array<double, 4> clip_rect(const ImageData& img) {
    if (!img.clipped) {
        return {0.0, 0.0, 1.0, 1.0};
    }
    double u0 = std::clamp(img.clip_u0, 0.0, 1.0);
    double u1 = std::clamp(img.clip_u1, 0.0, 1.0);
    double v0 = std::clamp(img.clip_v0, 0.0, 1.0);
    double v1 = std::clamp(img.clip_v1, 0.0, 1.0);
    if (u1 < u0) {
        std::swap(u0, u1);
    }
    if (v1 < v0) {
        std::swap(v0, v1);
    }
    return {u0, v0, u1, v1};
}

} // namespace

ImageQuad resolve_image_quad(const ImageData& img) {
    const auto [u0, v0, u1, v1] = clip_rect(img);
    // Image space has v growing DOWN from the top-left; the drawing has y growing up.
    // The visible band therefore runs from (1 - v1) to (1 - v0) in local y.
    const double x0 = u0 * img.width;
    const double x1 = u1 * img.width;
    const double y0 = (1.0 - v1) * img.height;
    const double y1 = (1.0 - v0) * img.height;

    const double cs = std::cos(img.rotation);
    const double sn = std::sin(img.rotation);
    const auto place = [&](double x, double y) -> Vec2 {
        return {img.pos.x + x * cs - y * sn, img.pos.y + x * sn + y * cs};
    };
    return {place(x0, y0), place(x1, y0), place(x1, y1), place(x0, y1)};
}

std::array<double, 4> resolve_image_uv(const ImageData& img) {
    return clip_rect(img);
}

bool point_in_image(const ImageData& img, Vec2 p) {
    // Point-in-convex-quad by consistent cross-product sign; the quad is a rotated
    // rectangle, so it is convex by construction.
    const ImageQuad q = resolve_image_quad(img);
    bool pos = false;
    bool neg = false;
    for (int i = 0; i < 4; ++i) {
        const Vec2 e = q[static_cast<std::size_t>((i + 1) % 4)] - q[static_cast<std::size_t>(i)];
        const Vec2 r = p - q[static_cast<std::size_t>(i)];
        const double c = e.x * r.y - e.y * r.x;
        pos = pos || c > 1e-12;
        neg = neg || c < -1e-12;
    }
    return !(pos && neg);
}

std::vector<Vec2> image_uv_to_world(const ImageData& img, std::span<const Vec2> uv) {
    std::vector<Vec2> out;
    out.reserve(uv.size());
    const double cs = std::cos(img.rotation);
    const double sn = std::sin(img.rotation);
    for (const Vec2& p : uv) {
        const double x = p.x * img.width;
        const double y = (1.0 - p.y) * img.height;
        out.push_back({img.pos.x + x * cs - y * sn, img.pos.y + x * sn + y * cs});
    }
    return out;
}

bool point_in_polygon(std::span<const Vec2> poly, Vec2 p) {
    bool inside = false;
    const std::size_t n = poly.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const Vec2& a = poly[i];
        const Vec2& b = poly[j];
        if ((a.y > p.y) != (b.y > p.y)) {
            const double x = a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y);
            if (p.x < x) {
                inside = !inside;
            }
        }
    }
    return inside;
}

bool resolve_image_path(std::string_view drawing_dir, std::string_view source, std::string& out) {
    if (source.empty()) {
        return false;
    }
    namespace fs = std::filesystem;
    const fs::path src(source);
    // An absolute source is refused outright: a drawing must not name a machine path.
    if (src.is_absolute()) {
        return false;
    }
    std::error_code ec;
    const fs::path base = fs::weakly_canonical(fs::path(drawing_dir), ec);
    if (ec) {
        return false;
    }
    const fs::path full = fs::weakly_canonical(base / src, ec);
    if (ec) {
        return false;
    }
    // The resolved path must still be INSIDE the drawing's directory. Comparing after
    // weakly_canonical is what makes "a/../../etc/passwd" fail: the traversal is already
    // collapsed, so this is a containment test, not a string test for "..".
    const auto b = base.begin();
    const auto be = base.end();
    auto f = full.begin();
    const auto fe = full.end();
    for (auto it = b; it != be; ++it, ++f) {
        if (f == fe || *f != *it) {
            return false;
        }
    }
    out = full.string();
    return true;
}

} // namespace musacad::core
