// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/image.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "musacad/core/units.hpp"

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

namespace {
// Big- and little-endian reads, refusing to run past the end.
std::uint32_t rd(std::span<const std::uint8_t> b, std::size_t at, int n, bool big) {
    if (at + static_cast<std::size_t>(n) > b.size()) {
        return 0;
    }
    std::uint32_t v = 0;
    for (int i = 0; i < n; ++i) {
        const std::uint32_t byte = b[at + static_cast<std::size_t>(big ? i : n - 1 - i)];
        v = (v << 8) | byte;
    }
    return v;
}

// A TIFF structure's resolution (a TIFF file, or the TIFF inside JPEG's EXIF): IFD0's
// XResolution (282) and YResolution (283), RATIONAL, in the ResolutionUnit (296: 2 inch,
// 3 centimetre; 1 none).
std::optional<Vec2> tiff_resolution(std::span<const std::uint8_t> b) {
    if (b.size() < 8) {
        return std::nullopt;
    }
    bool big = false;
    if (b[0] == 'M' && b[1] == 'M') {
        big = true;
    } else if (!(b[0] == 'I' && b[1] == 'I')) {
        return std::nullopt;
    }
    if (rd(b, 2, 2, big) != 42) {
        return std::nullopt;
    }
    const std::size_t ifd = rd(b, 4, 4, big);
    const std::uint32_t entries = rd(b, ifd, 2, big);
    double x = 0.0;
    double y = 0.0;
    std::uint32_t unit = 2; // inches when not said
    for (std::uint32_t e = 0; e < entries && e < 512; ++e) {
        const std::size_t at = ifd + 2 + static_cast<std::size_t>(e) * 12;
        const std::uint32_t tag = rd(b, at, 2, big);
        if (tag == 282 || tag == 283) {
            const std::size_t off = rd(b, at + 8, 4, big);
            const std::uint32_t num = rd(b, off, 4, big);
            const std::uint32_t den = rd(b, off + 4, 4, big);
            (tag == 282 ? x : y) = den != 0 ? static_cast<double>(num) / den : 0.0;
        } else if (tag == 296) {
            unit = rd(b, at + 8, 2, big);
        }
    }
    if (!(x > 0.0) || unit == 1) {
        return std::nullopt;
    }
    const double per_inch = unit == 3 ? 2.54 : 1.0;
    return Vec2{x * per_inch, (y > 0.0 ? y : x) * per_inch};
}
} // namespace

std::optional<Vec2> image_resolution(std::span<const std::uint8_t> b) {
    // PNG: the pHYs chunk (pixels per unit; unit 1 = metre), before the image data.
    static constexpr std::uint8_t kPng[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (b.size() >= 8 && std::memcmp(b.data(), kPng, 8) == 0) {
        std::size_t at = 8;
        while (at + 8 <= b.size()) {
            const std::uint32_t len = rd(b, at, 4, true);
            const std::string_view type(reinterpret_cast<const char*>(b.data() + at + 4), 4);
            if (type == "pHYs" && len >= 9 && at + 8 + 9 <= b.size()) {
                const std::uint32_t px = rd(b, at + 8, 4, true);
                const std::uint32_t py = rd(b, at + 12, 4, true);
                if (b[at + 16] != 1 || px == 0) {
                    return std::nullopt; // an aspect ratio only
                }
                return Vec2{px * 0.0254, (py != 0 ? py : px) * 0.0254};
            }
            if (type == "IDAT" || type == "IEND") {
                break;
            }
            at += 12 + static_cast<std::size_t>(len);
        }
        return std::nullopt;
    }
    // JPEG: JFIF's density (APP0), else the EXIF resolution (APP1).
    if (b.size() >= 4 && b[0] == 0xFF && b[1] == 0xD8) {
        std::size_t at = 2;
        std::optional<Vec2> exif;
        while (at + 4 <= b.size() && b[at] == 0xFF) {
            const std::uint8_t marker = b[at + 1];
            if (marker == 0xDA || marker == 0xD9) {
                break; // the scan (or the end): no more headers
            }
            const std::size_t len = rd(b, at + 2, 2, true);
            const std::size_t data = at + 4;
            if (marker == 0xE0 && len >= 14 && data + 12 <= b.size() &&
                std::memcmp(b.data() + data, "JFIF\0", 5) == 0) {
                const std::uint8_t units = b[data + 7];
                const std::uint32_t dx = rd(b, data + 8, 2, true);
                const std::uint32_t dy = rd(b, data + 10, 2, true);
                if ((units == 1 || units == 2) && dx > 0) {
                    const double f = units == 2 ? 2.54 : 1.0;
                    return Vec2{dx * f, (dy > 0 ? dy : dx) * f};
                }
            } else if (marker == 0xE1 && len >= 14 && data + 6 <= b.size() &&
                       std::memcmp(b.data() + data, "Exif\0\0", 6) == 0) {
                const std::size_t end = std::min(b.size(), at + 2 + len);
                exif = tiff_resolution(b.subspan(data + 6, end - (data + 6)));
            }
            at += 2 + len;
        }
        return exif;
    }
    // BMP: pixels per metre in the info header.
    if (b.size() >= 46 && b[0] == 'B' && b[1] == 'M') {
        const std::uint32_t px = rd(b, 38, 4, false);
        const std::uint32_t py = rd(b, 42, 4, false);
        if (px == 0) {
            return std::nullopt;
        }
        return Vec2{px * 0.0254, (py != 0 ? py : px) * 0.0254};
    }
    return tiff_resolution(b);
}

std::optional<std::array<std::uint32_t, 2>> image_pixel_size(std::span<const std::uint8_t> b) {
    using Size = std::array<std::uint32_t, 2>;
    static constexpr std::uint8_t kPng[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (b.size() >= 24 && std::memcmp(b.data(), kPng, 8) == 0) {
        return Size{rd(b, 16, 4, true), rd(b, 20, 4, true)}; // IHDR
    }
    if (b.size() >= 4 && b[0] == 0xFF && b[1] == 0xD8) {
        std::size_t at = 2;
        while (at + 9 <= b.size() && b[at] == 0xFF) {
            const std::uint8_t m = b[at + 1];
            // SOF0..SOF15 but DHT (C4), JPG (C8) and DAC (CC): precision, height, width.
            if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
                return Size{rd(b, at + 7, 2, true), rd(b, at + 5, 2, true)};
            }
            if (m == 0xDA || m == 0xD9) {
                break;
            }
            at += 2 + rd(b, at + 2, 2, true);
        }
        return std::nullopt;
    }
    if (b.size() >= 26 && b[0] == 'B' && b[1] == 'M') {
        const auto w = static_cast<std::int32_t>(rd(b, 18, 4, false));
        const auto h = static_cast<std::int32_t>(rd(b, 22, 4, false));
        return Size{static_cast<std::uint32_t>(std::abs(w)), static_cast<std::uint32_t>(std::abs(h))};
    }
    if (b.size() >= 10 && std::memcmp(b.data(), "GIF8", 4) == 0) {
        return Size{rd(b, 6, 2, false), rd(b, 8, 2, false)};
    }
    if (b.size() >= 8 && ((b[0] == 'I' && b[1] == 'I') || (b[0] == 'M' && b[1] == 'M'))) {
        const bool big = b[0] == 'M';
        const std::size_t ifd = rd(b, 4, 4, big);
        const std::uint32_t entries = rd(b, ifd, 2, big);
        std::uint32_t w = 0;
        std::uint32_t h = 0;
        for (std::uint32_t e = 0; e < entries && e < 512; ++e) {
            const std::size_t at = ifd + 2 + static_cast<std::size_t>(e) * 12;
            const std::uint32_t tag = rd(b, at, 2, big);
            const std::uint32_t type = rd(b, at + 2, 2, big);
            const std::uint32_t v = type == 3 ? rd(b, at + 8, 2, big) : rd(b, at + 8, 4, big); // SHORT or LONG
            w = tag == 256 ? v : w;
            h = tag == 257 ? v : h;
        }
        if (w > 0 && h > 0) {
            return Size{w, h};
        }
    }
    return std::nullopt;
}

Vec2 image_base_size(std::uint32_t pixel_w, std::uint32_t pixel_h, std::optional<Vec2> dpi, std::uint8_t insunits) {
    if (pixel_w == 0 || pixel_h == 0) {
        return {1.0, 1.0};
    }
    if (insunits != 0 && dpi && dpi->x > 0.0 && dpi->y > 0.0) {
        const double per_inch = units::insunits_factor(1, insunits); // drawing units in an inch
        return {pixel_w / dpi->x * per_inch, pixel_h / dpi->y * per_inch};
    }
    return {1.0, static_cast<double>(pixel_h) / static_cast<double>(pixel_w)};
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
