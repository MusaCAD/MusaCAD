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

// " layer flags cr cg cb linetype lineweight" -- the 7-int property tail (v2).
void append_props(std::string& s, const EntityProps& p) {
    const std::uint64_t fields[7] = {p.layer,           p.flags,
                                     p.color.r,         p.color.g,
                                     p.color.b,         static_cast<std::uint64_t>(p.linetype),
                                     p.lineweight};
    for (std::uint64_t f : fields) {
        s += ' ';
        append_uint(s, f);
    }
}

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

// Parses `n` doubles starting at token `start` into `out` (appended).
bool parse_doubles(const std::vector<std::string_view>& tok, std::size_t start, std::size_t n,
                   std::vector<double>& out) {
    if (start + n > tok.size()) {
        return false;
    }
    for (std::size_t i = 0; i < n; ++i) {
        double d = 0.0;
        if (!to_double(tok[start + i], d)) {
            return false;
        }
        out.push_back(d);
    }
    return true;
}

// Parses the 7-int property tail at token `start`. Returns false on shortage.
bool parse_props(const std::vector<std::string_view>& tok, std::size_t start, EntityProps& out) {
    std::uint64_t v[7];
    if (start + 7 > tok.size()) {
        return false;
    }
    for (std::size_t i = 0; i < 7; ++i) {
        if (!to_uint(tok[start + i], v[i])) {
            return false;
        }
    }
    out.layer = static_cast<std::uint16_t>(v[0]);
    out.flags = static_cast<std::uint8_t>(v[1]);
    out.color = {static_cast<std::uint8_t>(v[2]), static_cast<std::uint8_t>(v[3]),
                 static_cast<std::uint8_t>(v[4])};
    out.linetype = static_cast<Linetype>(v[5]);
    out.lineweight = static_cast<std::uint8_t>(v[6]);
    return true;
}

} // namespace

std::string serialize_native(const Document& doc) {
    std::string s;
    s.reserve(128 + doc.entity_count() * 40);
    s += "MUSACAD ";
    append_uint(s, doc.format_version);
    s += '\n';
    s += "UNITS ";
    s += doc.units.empty() ? "unitless" : doc.units;
    s += '\n';
    s += "CURRENT ";
    append_uint(s, doc.current_layer);
    s += '\n';
    // LAYER <r> <g> <b> <linetype> <lineweight> <on> <frozen> <locked> <name...>
    for (const Layer& l : doc.layers) {
        s += "LAYER ";
        append_uint(s, l.color.r);
        s += ' ';
        append_uint(s, l.color.g);
        s += ' ';
        append_uint(s, l.color.b);
        s += ' ';
        append_uint(s, static_cast<std::uint64_t>(l.linetype));
        s += ' ';
        append_uint(s, l.lineweight);
        s += ' ';
        append_uint(s, l.on ? 1 : 0);
        s += ' ';
        append_uint(s, l.frozen ? 1 : 0);
        s += ' ';
        append_uint(s, l.locked ? 1 : 0);
        s += ' ';
        s += l.name;
        s += '\n';
    }

    for (const DocPoint& p : doc.points) {
        s += "POINT ";
        append_vec(s, p.p);
        append_props(s, p.props);
        s += '\n';
    }
    for (const DocLine& l : doc.lines) {
        s += "LINE ";
        append_vec(s, l.a);
        s += ' ';
        append_vec(s, l.b);
        append_props(s, l.props);
        s += '\n';
    }
    for (const DocCircle& c : doc.circles) {
        s += "CIRCLE ";
        append_vec(s, c.center);
        s += ' ';
        append_double(s, c.radius);
        append_props(s, c.props);
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
        append_props(s, a.props);
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
        append_props(s, p.props);
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
        append_props(s, sp.props);
        s += '\n';
    }
    s += "END\n";
    return s;
}

IoResult parse_native(std::string_view text, Document& out) {
    Document doc;
    doc.layers.clear(); // built from the file (or defaulted below for v1)
    bool header_seen = false;
    bool end_seen = false;
    std::uint32_t version = 0;
    std::size_t line_no = 0;

    const auto fail = [&](const std::string& why) {
        return IoResult::failure("Line " + std::to_string(line_no) + ": " + why);
    };

    std::istringstream in{std::string(text)};
    std::string raw;
    std::vector<double> vals;
    // Reads a fixed-geometry entity: `geom` doubles, then (v2) 7 prop ints.
    const auto read_fixed = [&](const std::vector<std::string_view>& tok, std::size_t geom,
                                EntityProps& props) -> bool {
        vals.clear();
        if (!parse_doubles(tok, 1, geom, vals)) {
            return false;
        }
        if (version >= 2) {
            if (!parse_props(tok, 1 + geom, props)) {
                return false;
            }
            return tok.size() == 1 + geom + 7;
        }
        return tok.size() == 1 + geom;
    };

    while (std::getline(in, raw)) {
        ++line_no;
        const std::vector<std::string_view> tok = tokenize(raw);
        if (tok.empty()) {
            continue;
        }
        const std::string_view key = tok[0];

        if (!header_seen) {
            std::uint64_t ver = 0;
            if (key != "MUSACAD" || tok.size() != 2 || !to_uint(tok[1], ver)) {
                return fail("expected MUSACAD header");
            }
            if (ver == 0 || ver > kFormatVersion) {
                return IoResult::failure("Unsupported file format version " + std::to_string(ver) +
                                         ".");
            }
            version = static_cast<std::uint32_t>(ver);
            doc.format_version = version;
            header_seen = true;
            continue;
        }

        if (key == "END") {
            end_seen = true;
            break;
        }
        if (key == "UNITS") {
            doc.units = tok.size() >= 2 ? std::string(tok[1]) : "unitless";
        } else if (key == "CURRENT") {
            std::uint64_t idx = 0;
            if (tok.size() != 2 || !to_uint(tok[1], idx)) {
                return fail("malformed CURRENT");
            }
            doc.current_layer = static_cast<std::uint16_t>(idx);
        } else if (key == "LAYER") {
            // LAYER r g b linetype lineweight on frozen locked name...
            std::uint64_t f[8];
            if (tok.size() < 10) {
                return fail("malformed LAYER");
            }
            for (std::size_t i = 0; i < 8; ++i) {
                if (!to_uint(tok[1 + i], f[i])) {
                    return fail("malformed LAYER field");
                }
            }
            Layer l;
            l.color = {static_cast<std::uint8_t>(f[0]), static_cast<std::uint8_t>(f[1]),
                       static_cast<std::uint8_t>(f[2])};
            l.linetype = static_cast<Linetype>(f[3]);
            l.lineweight = static_cast<std::uint8_t>(f[4]);
            l.on = f[5] != 0;
            l.frozen = f[6] != 0;
            l.locked = f[7] != 0;
            // Name is the remainder of the line (may contain spaces).
            std::string name(tok[9]);
            for (std::size_t i = 10; i < tok.size(); ++i) {
                name += ' ';
                name += std::string(tok[i]);
            }
            l.name = name;
            doc.layers.push_back(std::move(l));
        } else if (key == "POINT") {
            EntityProps p;
            if (!read_fixed(tok, 2, p)) {
                return fail("POINT record malformed");
            }
            doc.points.push_back(DocPoint{{vals[0], vals[1]}, p});
        } else if (key == "LINE") {
            EntityProps p;
            if (!read_fixed(tok, 4, p)) {
                return fail("LINE record malformed");
            }
            doc.lines.push_back(DocLine{{vals[0], vals[1]}, {vals[2], vals[3]}, p});
        } else if (key == "CIRCLE") {
            EntityProps p;
            if (!read_fixed(tok, 3, p)) {
                return fail("CIRCLE record malformed");
            }
            doc.circles.push_back(DocCircle{{vals[0], vals[1]}, vals[2], p});
        } else if (key == "ARC") {
            EntityProps p;
            if (!read_fixed(tok, 5, p)) {
                return fail("ARC record malformed");
            }
            doc.arcs.push_back(DocArc{{vals[0], vals[1]}, vals[2], vals[3], vals[4], p});
        } else if (key == "POLYLINE") {
            std::uint64_t closed = 0;
            std::uint64_t n = 0;
            if (tok.size() < 3 || !to_uint(tok[1], closed) || !to_uint(tok[2], n)) {
                return fail("malformed POLYLINE header");
            }
            vals.clear();
            if (!parse_doubles(tok, 3, n * 2, vals)) {
                return fail("POLYLINE vertex count mismatch");
            }
            DocPolyline pl;
            pl.closed = closed != 0;
            pl.points.reserve(n);
            for (std::uint64_t i = 0; i < n; ++i) {
                pl.points.push_back({vals[i * 2], vals[i * 2 + 1]});
            }
            if (version >= 2) {
                if (!parse_props(tok, 3 + n * 2, pl.props) || tok.size() != 3 + n * 2 + 7) {
                    return fail("POLYLINE properties malformed");
                }
            } else if (tok.size() != 3 + n * 2) {
                return fail("POLYLINE record malformed");
            }
            doc.polylines.push_back(std::move(pl));
        } else if (key == "SPLINE") {
            std::uint64_t degree = 0;
            std::uint64_t n = 0;
            if (tok.size() < 3 || !to_uint(tok[1], degree) || !to_uint(tok[2], n)) {
                return fail("malformed SPLINE header");
            }
            vals.clear();
            if (!parse_doubles(tok, 3, n * 2, vals)) {
                return fail("SPLINE control-point count mismatch");
            }
            DocSpline sp;
            sp.degree = static_cast<std::uint32_t>(degree);
            sp.control_points.reserve(n);
            for (std::uint64_t i = 0; i < n; ++i) {
                sp.control_points.push_back({vals[i * 2], vals[i * 2 + 1]});
            }
            if (version >= 2) {
                if (!parse_props(tok, 3 + n * 2, sp.props) || tok.size() != 3 + n * 2 + 7) {
                    return fail("SPLINE properties malformed");
                }
            } else if (tok.size() != 3 + n * 2) {
                return fail("SPLINE record malformed");
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
    // v1 files (and any file without a layer table) load onto layer 0.
    if (doc.layers.empty()) {
        doc.layers.push_back(Layer{"0"});
    }
    doc.layers[0].name = "0";
    if (doc.current_layer >= doc.layers.size()) {
        doc.current_layer = 0;
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
