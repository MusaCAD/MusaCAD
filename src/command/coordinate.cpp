#include "musacad/command/coordinate.hpp"

#include <charconv>
#include <cstdio>

namespace musacad::command {

namespace {

std::string_view trim(std::string_view s) {
    const auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && is_space(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && is_space(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

bool to_double(std::string_view s, double& out) {
    s = trim(s);
    if (s.empty()) {
        return false;
    }
    const char* begin = s.data();
    const char* end = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc{} && ptr == end;
}

std::string fmt_point(core::Vec2 p) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4g, %.4g", p.x, p.y);
    return std::string(buf);
}

} // namespace

bool parse_number(std::string_view text, double& out) { return to_double(text, out); }

CoordParse parse_coordinate(std::string_view text, std::optional<core::Vec2> last) {
    CoordParse r;
    const std::string_view t = trim(text);
    if (t.empty()) {
        r.error = "Empty coordinate.";
        return r;
    }

    const bool relative = t.front() == '@';
    const std::string_view body = relative ? trim(t.substr(1)) : t;

    if (const auto lt = body.find('<'); lt != std::string_view::npos) {
        // Polar: dist<angle (only meaningful as relative).
        if (!relative) {
            r.error = "Polar coordinates must be relative (use @dist<angle).";
            return r;
        }
        if (!last) {
            r.error = "No previous point for a relative coordinate.";
            return r;
        }
        double dist = 0.0;
        double angle_deg = 0.0;
        if (!to_double(body.substr(0, lt), dist) || !to_double(body.substr(lt + 1), angle_deg)) {
            r.error = "Malformed polar coordinate (expected @dist<angle).";
            return r;
        }
        const double a = core::to_radians(angle_deg);
        r.point = *last + core::Vec2{dist * std::cos(a), dist * std::sin(a)};
        r.ok = true;
        r.interpretation = fmt_point(r.point) + "  (polar @" + std::to_string(dist) + "<" +
                           std::to_string(angle_deg) + ")";
        return r;
    }

    const auto comma = body.find(',');
    if (comma == std::string_view::npos) {
        r.error = "Malformed coordinate (expected x,y).";
        return r;
    }
    double a = 0.0;
    double b = 0.0;
    if (!to_double(body.substr(0, comma), a) || !to_double(body.substr(comma + 1), b)) {
        r.error = "Malformed coordinate (expected numeric x,y).";
        return r;
    }

    if (relative) {
        if (!last) {
            r.error = "No previous point for a relative coordinate.";
            return r;
        }
        r.point = *last + core::Vec2{a, b};
        r.interpretation = fmt_point(r.point) + "  (relative @" + fmt_point({a, b}) + ")";
    } else {
        r.point = core::Vec2{a, b};
        r.interpretation = fmt_point(r.point);
    }
    r.ok = true;
    return r;
}

} // namespace musacad::command
