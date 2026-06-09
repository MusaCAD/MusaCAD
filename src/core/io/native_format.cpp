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
// Reverse of the MTEXT/MLEADER content escaping (\n -> newline, \\ -> backslash).
std::string unescape(std::string_view in) {
    std::string o;
    o.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '\\' && i + 1 < in.size()) {
            const char c = in[i + 1];
            if (c == 'n') {
                o += '\n';
                ++i;
                continue;
            }
            if (c == '\\') {
                o += '\\';
                ++i;
                continue;
            }
        }
        o += in[i];
    }
    return o;
}

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
    s += "LTSCALE ";
    append_double(s, doc.ltscale);
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
    // DIMSTYLE th as at eo ee pr ta dlw  <4 element colours: by_layer r g b>  name...
    const auto append_ecolor = [](std::string& out, const ElementColor& ec) {
        append_uint(out, ec.by_layer ? 1 : 0);
        out += ' ';
        append_uint(out, ec.color.r);
        out += ' ';
        append_uint(out, ec.color.g);
        out += ' ';
        append_uint(out, ec.color.b);
        out += ' ';
    };
    for (const DimStyle& ds : doc.dimstyles) {
        s += "DIMSTYLE ";
        append_double(s, ds.text_height);
        s += ' ';
        append_double(s, ds.arrow_size);
        s += ' ';
        append_uint(s, ds.arrow_type);
        s += ' ';
        append_double(s, ds.ext_offset);
        s += ' ';
        append_double(s, ds.ext_extension);
        s += ' ';
        append_uint(s, ds.precision);
        s += ' ';
        append_uint(s, ds.text_above ? 1 : 0);
        s += ' ';
        append_uint(s, ds.dim_lineweight);
        s += ' ';
        append_ecolor(s, ds.dim_color);
        append_ecolor(s, ds.ext_color);
        append_ecolor(s, ds.text_color);
        append_ecolor(s, ds.arrow_color);
        s += ds.name;
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
        // v5: per-vertex arc bulges appended after props (only when any are present).
        if (p.bulges.size() == p.points.size()) {
            for (const double b : p.bulges) {
                s += ' ';
                append_double(s, b);
            }
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
        append_props(s, sp.props);
        s += '\n';
    }
    // TEXT params then the content on its own (raw) line, preserving spaces.
    for (const DocText& t : doc.texts) {
        s += "TEXT ";
        append_vec(s, t.pos);
        s += ' ';
        append_double(s, t.height);
        s += ' ';
        append_double(s, t.rotation);
        s += ' ';
        append_uint(s, t.justify);
        append_props(s, t.props);
        s += '\n';
        s += t.content;
        s += '\n';
    }
    for (const DocDim& d : doc.dims) {
        s += "DIM ";
        append_uint(s, d.type);
        s += ' ';
        append_vec(s, d.a);
        s += ' ';
        append_vec(s, d.b);
        s += ' ';
        append_vec(s, d.line_pt);
        s += ' ';
        append_uint(s, d.style);
        append_props(s, d.props);
        s += '\n';
    }
    // LEADER tipx tipy kneex kneey height style <props7>; content on next line.
    for (const DocLeader& l : doc.leaders) {
        s += "LEADER ";
        append_vec(s, l.tip);
        s += ' ';
        append_vec(s, l.knee);
        s += ' ';
        append_double(s, l.text_height);
        s += ' ';
        append_uint(s, l.style);
        append_props(s, l.props);
        s += '\n';
        s += l.content;
        s += '\n';
    }
    // v6: MTEXT and MLEADER. The MTextBlock numeric fields are written inline; the
    // content is on the following line (may contain spaces; \n stored as literal "\n").
    const auto append_block = [&](const MTextBlock& b) {
        append_vec(s, b.pos);
        s += ' ';
        append_double(s, b.width);
        s += ' ';
        append_double(s, b.height);
        s += ' ';
        append_double(s, b.rotation);
        s += ' ';
        append_double(s, b.width_factor);
        s += ' ';
        append_double(s, b.line_spacing);
        s += ' ';
        append_uint(s, b.attach);
    };
    // Content may contain newlines; escape them so each record stays one line + one
    // content line.
    const auto escape = [](std::string_view in) {
        std::string o;
        for (const char c : in) {
            if (c == '\n') {
                o += "\\n";
            } else if (c == '\\') {
                o += "\\\\";
            } else {
                o += c;
            }
        }
        return o;
    };
    for (const DocMText& m : doc.mtexts) {
        s += "MTEXT ";
        append_block(m.block);
        append_props(s, m.props);
        s += '\n';
        s += escape(m.content);
        s += '\n';
    }
    for (const DocMLeader& m : doc.mleaders) {
        s += "MLEADER ";
        append_uint(s, m.style);
        s += ' ';
        append_uint(s, m.vertices.size());
        for (const Vec2& v : m.vertices) {
            s += ' ';
            append_vec(s, v);
        }
        s += ' ';
        append_block(m.block);
        append_props(s, m.props);
        s += '\n';
        s += escape(m.content);
        s += '\n';
    }
    s += "END\n";
    return s;
}

IoResult parse_native(std::string_view text, Document& out) {
    Document doc;
    doc.layers.clear();     // built from the file (or defaulted below for v1)
    doc.dimstyles.clear();  // built from DIMSTYLE records (defaulted below if none)
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
        } else if (key == "LTSCALE") {
            double ls = 1.0;
            if (tok.size() != 2 || !to_double(tok[1], ls)) {
                return fail("malformed LTSCALE");
            }
            doc.ltscale = ls;
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
        } else if (key == "DIMSTYLE") {
            // DIMSTYLE th as at eo ee pr ta name...
            if (tok.size() < 9) {
                return fail("malformed DIMSTYLE");
            }
            double th = 0;
            double as = 0;
            double eo = 0;
            double ee = 0;
            std::uint64_t at = 0;
            std::uint64_t pr = 0;
            std::uint64_t ta = 0;
            if (!to_double(tok[1], th) || !to_double(tok[2], as) || !to_uint(tok[3], at) ||
                !to_double(tok[4], eo) || !to_double(tok[5], ee) || !to_uint(tok[6], pr) ||
                !to_uint(tok[7], ta)) {
                return fail("malformed DIMSTYLE field");
            }
            DimStyle ds;
            ds.text_height = th;
            ds.arrow_size = as;
            ds.arrow_type = static_cast<std::uint8_t>(at);
            ds.ext_offset = eo;
            ds.ext_extension = ee;
            ds.precision = static_cast<std::uint8_t>(pr);
            ds.text_above = ta != 0;
            // v4 appends: dim_lineweight + 4 element colours (by_layer r g b) before name.
            std::size_t name_at = 8;
            if (version >= 4 && tok.size() >= 25) {
                std::uint64_t dlw = 0;
                to_uint(tok[8], dlw);
                ds.dim_lineweight = static_cast<std::uint8_t>(dlw);
                const auto read_ec = [&](std::size_t i, ElementColor& ec) {
                    std::uint64_t by = 1;
                    std::uint64_t r = 0;
                    std::uint64_t g = 0;
                    std::uint64_t b = 0;
                    to_uint(tok[i], by);
                    to_uint(tok[i + 1], r);
                    to_uint(tok[i + 2], g);
                    to_uint(tok[i + 3], b);
                    ec.by_layer = by != 0;
                    ec.color = {static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                                static_cast<std::uint8_t>(b)};
                };
                read_ec(9, ds.dim_color);
                read_ec(13, ds.ext_color);
                read_ec(17, ds.text_color);
                read_ec(21, ds.arrow_color);
                name_at = 25;
            }
            std::string name(tok[name_at]);
            for (std::size_t i = name_at + 1; i < tok.size(); ++i) {
                name += ' ';
                name += std::string(tok[i]);
            }
            ds.name = name;
            doc.dimstyles.push_back(std::move(ds));
        } else if (key == "TEXT") {
            // params: px py height rotation justify <props7>; content on next line.
            vals.clear();
            EntityProps props;
            if (!parse_doubles(tok, 1, 4, vals)) {
                return fail("TEXT params malformed");
            }
            std::uint64_t justify = 0;
            if (tok.size() < 13 || !to_uint(tok[5], justify) || !parse_props(tok, 6, props)) {
                return fail("TEXT params malformed");
            }
            std::string content;
            if (!std::getline(in, content)) {
                return fail("TEXT missing content line");
            }
            ++line_no;
            if (!content.empty() && content.back() == '\r') {
                content.pop_back();
            }
            doc.texts.push_back(DocText{{vals[0], vals[1]},
                                        vals[2],
                                        vals[3],
                                        static_cast<std::uint8_t>(justify),
                                        std::move(content),
                                        props});
        } else if (key == "DIM") {
            // DIM type ax ay bx by lx ly style <props7>
            std::uint64_t dtype = 0;
            vals.clear();
            if (tok.size() != 16 || !to_uint(tok[1], dtype) || !parse_doubles(tok, 2, 6, vals)) {
                return fail("DIM record malformed");
            }
            std::uint64_t style = 0;
            EntityProps props;
            if (!to_uint(tok[8], style) || !parse_props(tok, 9, props)) {
                return fail("DIM record malformed");
            }
            doc.dims.push_back(DocDim{static_cast<std::uint8_t>(dtype),
                                      {vals[0], vals[1]},
                                      {vals[2], vals[3]},
                                      {vals[4], vals[5]},
                                      static_cast<std::uint16_t>(style),
                                      props});
        } else if (key == "LEADER") {
            // LEADER tipx tipy kneex kneey height style <props7>; content next line.
            vals.clear();
            EntityProps props;
            std::uint64_t style = 0;
            if (tok.size() != 14 || !parse_doubles(tok, 1, 5, vals) || !to_uint(tok[6], style) ||
                !parse_props(tok, 7, props)) {
                return fail("LEADER record malformed");
            }
            std::string content;
            if (!std::getline(in, content)) {
                return fail("LEADER missing content line");
            }
            ++line_no;
            if (!content.empty() && content.back() == '\r') {
                content.pop_back();
            }
            doc.leaders.push_back(DocLeader{{vals[0], vals[1]},
                                            {vals[2], vals[3]},
                                            vals[4],
                                            static_cast<std::uint16_t>(style),
                                            std::move(content),
                                            props});
        } else if (key == "MTEXT") {
            // MTEXT px py width height rot wf ls attach <props7>; content next line.
            vals.clear();
            EntityProps props;
            std::uint64_t attach = 0;
            if (tok.size() != 16 || !parse_doubles(tok, 1, 7, vals) || !to_uint(tok[8], attach) ||
                !parse_props(tok, 9, props)) {
                return fail("MTEXT record malformed");
            }
            std::string content;
            if (!std::getline(in, content)) {
                return fail("MTEXT missing content line");
            }
            ++line_no;
            if (!content.empty() && content.back() == '\r') {
                content.pop_back();
            }
            MTextBlock b;
            b.pos = {vals[0], vals[1]};
            b.width = vals[2];
            b.height = vals[3];
            b.rotation = vals[4];
            b.width_factor = vals[5];
            b.line_spacing = vals[6];
            b.attach = static_cast<std::uint8_t>(attach);
            doc.mtexts.push_back(DocMText{b, unescape(content), props});
        } else if (key == "MLEADER") {
            // MLEADER style nverts <x y...> px py width height rot wf ls attach <props7>.
            std::uint64_t style = 0;
            std::uint64_t nv = 0;
            if (tok.size() < 3 || !to_uint(tok[1], style) || !to_uint(tok[2], nv)) {
                return fail("MLEADER header malformed");
            }
            const std::size_t vbase = 3;
            const std::size_t bbase = vbase + nv * 2; // block fields start
            std::uint64_t attach = 0;
            vals.clear();
            std::vector<double> bvals;
            EntityProps props;
            if (tok.size() != bbase + 7 + 8 || !parse_doubles(tok, vbase, nv * 2, vals) ||
                !parse_doubles(tok, bbase, 7, bvals) || !to_uint(tok[bbase + 7], attach) ||
                !parse_props(tok, bbase + 8, props)) {
                return fail("MLEADER record malformed");
            }
            std::string content;
            if (!std::getline(in, content)) {
                return fail("MLEADER missing content line");
            }
            ++line_no;
            if (!content.empty() && content.back() == '\r') {
                content.pop_back();
            }
            std::vector<Vec2> verts;
            verts.reserve(nv);
            for (std::uint64_t i = 0; i < nv; ++i) {
                verts.push_back({vals[i * 2], vals[i * 2 + 1]});
            }
            MTextBlock b;
            b.pos = {bvals[0], bvals[1]};
            b.width = bvals[2];
            b.height = bvals[3];
            b.rotation = bvals[4];
            b.width_factor = bvals[5];
            b.line_spacing = bvals[6];
            b.attach = static_cast<std::uint8_t>(attach);
            doc.mleaders.push_back(DocMLeader{std::move(verts), static_cast<std::uint16_t>(style), b,
                                              unescape(content), props});
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
                if (!parse_props(tok, 3 + n * 2, pl.props)) {
                    return fail("POLYLINE properties malformed");
                }
                const std::size_t after = 3 + n * 2 + 7;
                if (tok.size() == after + n) {
                    // v5: per-vertex bulges follow the properties.
                    std::vector<double> bv;
                    if (!parse_doubles(tok, after, n, bv)) {
                        return fail("POLYLINE bulge count mismatch");
                    }
                    pl.bulges = std::move(bv);
                } else if (tok.size() != after) {
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
    if (doc.dimstyles.empty()) {
        doc.dimstyles.push_back(DimStyle{"Standard"});
    }
    doc.dimstyles[0].name = "Standard";
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
