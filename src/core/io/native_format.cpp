#include "musacad/core/io/native_format.hpp"

#include <charconv>
#include <fstream>
#include <sstream>

namespace musacad::core::io {

namespace {

void append_double(std::string& s, double v) {
    char buf[40];
    const auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v);
    s.append(buf, ptr);
}
void append_vec(std::string& s, Vec2 v) {
    append_double(s, v.x);
    s += ' ';
    append_double(s, v.y);
}
void append_uint(std::string& s, std::uint64_t v) { s += std::to_string(v); }

/// Splits a line into whitespace-separated tokens.
std::vector<std::string_view> tokenize(std::string_view line) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) {
            ++i;
        }
        const std::size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') {
            ++i;
        }
        if (i > start) {
            out.push_back(line.substr(start, i - start));
        }
    }
    return out;
}

bool to_double(std::string_view t, double& out) {
    const auto [ptr, ec] = std::from_chars(t.data(), t.data() + t.size(), out);
    return ec == std::errc{} && ptr == t.data() + t.size();
}
bool to_uint(std::string_view t, std::uint64_t& out) {
    const auto [ptr, ec] = std::from_chars(t.data(), t.data() + t.size(), out);
    return ec == std::errc{} && ptr == t.data() + t.size();
}

// Reads `count` doubles from tokens[start..]. Returns false on shortage/parse error.
bool read_vecs(const std::vector<std::string_view>& tok, std::size_t start, std::size_t doubles,
               std::vector<double>& out) {
    if (tok.size() - start != doubles) {
        return false;
    }
    out.clear();
    for (std::size_t i = 0; i < doubles; ++i) {
        double d = 0.0;
        if (!to_double(tok[start + i], d)) {
            return false;
        }
        out.push_back(d);
    }
    return true;
}

} // namespace

std::string serialize_native(const Document& doc) {
    std::string s;
    s.reserve(64 + doc.entity_count() * 24);
    s += "MUSACAD ";
    append_uint(s, doc.format_version);
    s += '\n';
    s += "UNITS ";
    s += doc.units.empty() ? "unitless" : doc.units;
    s += '\n';

    for (const Vec2& p : doc.points) {
        s += "POINT ";
        append_vec(s, p);
        s += '\n';
    }
    for (const DocLine& l : doc.lines) {
        s += "LINE ";
        append_vec(s, l.a);
        s += ' ';
        append_vec(s, l.b);
        s += '\n';
    }
    for (const DocCircle& c : doc.circles) {
        s += "CIRCLE ";
        append_vec(s, c.center);
        s += ' ';
        append_double(s, c.radius);
        s += '\n';
    }
    for (const DocArc& a : doc.arcs) {
        s += "ARC ";
        append_vec(s, a.center);
        s += ' ';
        append_double(s, a.radius);
        s += ' ';
        append_double(s, a.start_angle);
        s += ' ';
        append_double(s, a.end_angle);
        s += '\n';
    }
    for (const DocPolyline& p : doc.polylines) {
        s += "POLYLINE ";
        s += p.closed ? '1' : '0';
        s += ' ';
        append_uint(s, p.points.size());
        for (const Vec2& v : p.points) {
            s += ' ';
            append_vec(s, v);
        }
        s += '\n';
    }
    for (const DocSpline& sp : doc.splines) {
        s += "SPLINE ";
        append_uint(s, sp.degree);
        s += ' ';
        append_uint(s, sp.control_points.size());
        for (const Vec2& v : sp.control_points) {
            s += ' ';
            append_vec(s, v);
        }
        s += '\n';
    }
    s += "END\n";
    return s;
}

IoResult parse_native(std::string_view text, Document& out) {
    Document doc;
    bool header_seen = false;
    bool end_seen = false;
    std::size_t line_no = 0;

    const auto fail = [&](const std::string& why) {
        return IoResult::failure("Line " + std::to_string(line_no) + ": " + why);
    };

    std::istringstream in{std::string(text)};
    std::string raw;
    std::vector<double> vals;
    while (std::getline(in, raw)) {
        ++line_no;
        const std::vector<std::string_view> tok = tokenize(raw);
        if (tok.empty()) {
            continue; // blank line
        }
        const std::string_view key = tok[0];

        if (!header_seen) {
            if (key != "MUSACAD") {
                return fail("expected MUSACAD header");
            }
            std::uint64_t ver = 0;
            if (tok.size() != 2 || !to_uint(tok[1], ver)) {
                return fail("malformed MUSACAD header");
            }
            if (ver > kFormatVersion) {
                return IoResult::failure("File format version " + std::to_string(ver) +
                                         " is newer than supported (" +
                                         std::to_string(kFormatVersion) + ").");
            }
            doc.format_version = static_cast<std::uint32_t>(ver);
            header_seen = true;
            continue;
        }

        if (key == "END") {
            end_seen = true;
            break;
        }
        if (key == "UNITS") {
            doc.units = tok.size() >= 2 ? std::string(tok[1]) : "unitless";
        } else if (key == "EXTENTS") {
            // Informational metadata; recomputed on demand, ignored here.
        } else if (key == "POINT") {
            if (!read_vecs(tok, 1, 2, vals)) {
                return fail("POINT expects 2 numbers");
            }
            doc.points.push_back({vals[0], vals[1]});
        } else if (key == "LINE") {
            if (!read_vecs(tok, 1, 4, vals)) {
                return fail("LINE expects 4 numbers");
            }
            doc.lines.push_back(DocLine{{vals[0], vals[1]}, {vals[2], vals[3]}});
        } else if (key == "CIRCLE") {
            if (!read_vecs(tok, 1, 3, vals)) {
                return fail("CIRCLE expects 3 numbers");
            }
            doc.circles.push_back(DocCircle{{vals[0], vals[1]}, vals[2]});
        } else if (key == "ARC") {
            if (!read_vecs(tok, 1, 5, vals)) {
                return fail("ARC expects 5 numbers");
            }
            doc.arcs.push_back(DocArc{{vals[0], vals[1]}, vals[2], vals[3], vals[4]});
        } else if (key == "POLYLINE") {
            std::uint64_t closed = 0;
            std::uint64_t n = 0;
            if (tok.size() < 3 || !to_uint(tok[1], closed) || !to_uint(tok[2], n)) {
                return fail("malformed POLYLINE header");
            }
            if (!read_vecs(tok, 3, n * 2, vals)) {
                return fail("POLYLINE vertex count mismatch");
            }
            DocPolyline pl;
            pl.closed = closed != 0;
            pl.points.reserve(n);
            for (std::uint64_t i = 0; i < n; ++i) {
                pl.points.push_back({vals[i * 2], vals[i * 2 + 1]});
            }
            doc.polylines.push_back(std::move(pl));
        } else if (key == "SPLINE") {
            std::uint64_t degree = 0;
            std::uint64_t n = 0;
            if (tok.size() < 3 || !to_uint(tok[1], degree) || !to_uint(tok[2], n)) {
                return fail("malformed SPLINE header");
            }
            if (!read_vecs(tok, 3, n * 2, vals)) {
                return fail("SPLINE control-point count mismatch");
            }
            DocSpline sp;
            sp.degree = static_cast<std::uint32_t>(degree);
            sp.control_points.reserve(n);
            for (std::uint64_t i = 0; i < n; ++i) {
                sp.control_points.push_back({vals[i * 2], vals[i * 2 + 1]});
            }
            doc.splines.push_back(std::move(sp));
        } else {
            return fail("unknown record \"" + std::string(key) + "\"");
        }
    }

    if (!header_seen) {
        return IoResult::failure("Not a Musa CAD file (empty or missing header).");
    }
    if (!end_seen) {
        return IoResult::failure("Unexpected end of file (missing END).");
    }
    out = std::move(doc);
    return IoResult::success("Opened " + std::to_string(out.entity_count()) + " entities.");
}

IoResult save_native(const Document& doc, const std::string& path) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        return IoResult::failure("Cannot write file: " + path);
    }
    const std::string text = serialize_native(doc);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!f) {
        return IoResult::failure("Write failed: " + path);
    }
    return IoResult::success("Saved " + std::to_string(doc.entity_count()) + " entities.");
}

IoResult load_native(const std::string& path, Document& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return IoResult::failure("Cannot open file: " + path);
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return parse_native(ss.str(), out);
}

} // namespace musacad::core::io
