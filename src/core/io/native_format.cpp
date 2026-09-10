// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/io/native_format.hpp"

#include <algorithm>
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

// The DimOverrides block (mask + all values => lossless). Appended to DIM and (v13+)
// LEADER/MLEADER records. Its PRESENCE is detected by token count; its WIDTH is keyed
// to the format version -- 15 fields through v15, 16 from v16 (text_fit). Widening the
// shared block rather than bolting text_fit onto the DIM record alone keeps
// append_overrides/parse_overrides the single definition of what an override block is.
void append_overrides(std::string& s, const DimOverrides& o) {
    s += ' ';
    append_uint(s, o.mask);
    s += ' ';
    append_uint(s, o.arrow_type);
    s += ' ';
    append_uint(s, o.precision);
    s += ' ';
    append_uint(s, o.text_above ? 1 : 0);
    s += ' ';
    append_double(s, o.text_height);
    s += ' ';
    append_double(s, o.arrow_size);
    for (const Rgb& c : {o.dim_color, o.ext_color, o.text_color}) {
        s += ' ';
        append_uint(s, c.r);
        s += ' ';
        append_uint(s, c.g);
        s += ' ';
        append_uint(s, c.b);
    }
    s += ' ';
    append_uint(s, o.text_fit); // v16
}

// Base64, for the embedded-image payload. The format is line-oriented, so the encoded
// text is chunked across lines and reassembled on read.
constexpr char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const std::vector<std::uint8_t>& in) {
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < in.size(); i += 3) {
        const unsigned b0 = in[i];
        const unsigned b1 = i + 1 < in.size() ? in[i + 1] : 0u;
        const unsigned b2 = i + 2 < in.size() ? in[i + 2] : 0u;
        const unsigned v = (b0 << 16) | (b1 << 8) | b2;
        out += kB64[(v >> 18) & 0x3F];
        out += kB64[(v >> 12) & 0x3F];
        out += i + 1 < in.size() ? kB64[(v >> 6) & 0x3F] : '=';
        out += i + 2 < in.size() ? kB64[v & 0x3F] : '=';
    }
    return out;
}

int b64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/// Decodes base64, rejecting any stray character rather than silently skipping it --
/// a corrupt payload must fail the load, not produce a garbled image.
bool base64_decode(std::string_view in, std::vector<std::uint8_t>& out) {
    unsigned acc = 0;
    int bits = 0;
    for (const char c : in) {
        if (c == '=') {
            break;
        }
        const int v = b64_value(c);
        if (v < 0) {
            return false;
        }
        acc = (acc << 6) | static_cast<unsigned>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((acc >> bits) & 0xFF));
        }
    }
    return true;
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

// Parse the DimOverrides block written by append_overrides, starting at tok[base].
// `with_text_fit` selects the v16 16-field form; a v13-v15 file has 15 fields and its
// text_fit stays ByStyle (which is Auto -- so older drawings simply start fitting).
bool parse_overrides(const std::vector<std::string_view>& tok, std::size_t base, DimOverrides& ov,
                     bool with_text_fit) {
    const std::size_t width = with_text_fit ? 16u : 15u;
    if (base + width > tok.size()) {
        return false;
    }
    std::uint64_t mask = 0, atype = 0, prec = 0, above = 0;
    double th = 0.0, as = 0.0;
    std::array<std::uint64_t, 9> rgb{};
    bool ok = to_uint(tok[base], mask) && to_uint(tok[base + 1], atype) &&
              to_uint(tok[base + 2], prec) && to_uint(tok[base + 3], above) &&
              to_double(tok[base + 4], th) && to_double(tok[base + 5], as);
    for (int k = 0; ok && k < 9; ++k) {
        ok = to_uint(tok[base + 6 + static_cast<std::size_t>(k)], rgb[static_cast<std::size_t>(k)]);
    }
    if (!ok) {
        return false;
    }
    ov.mask = static_cast<std::uint16_t>(mask);
    ov.arrow_type = static_cast<std::uint8_t>(atype);
    ov.precision = static_cast<std::uint8_t>(prec);
    ov.text_above = above != 0;
    ov.text_height = th;
    ov.arrow_size = as;
    const auto b = [&](int i) { return static_cast<std::uint8_t>(rgb[static_cast<std::size_t>(i)]); };
    ov.dim_color = {b(0), b(1), b(2)};
    ov.ext_color = {b(3), b(4), b(5)};
    ov.text_color = {b(6), b(7), b(8)};
    if (with_text_fit) {
        std::uint64_t tf = 0;
        if (!to_uint(tok[base + 15], tf)) {
            return false;
        }
        ov.text_fit = static_cast<std::uint8_t>(std::min<std::uint64_t>(tf, 2));
    }
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
        // v16: text_fit sits BEFORE the name, and its presence is keyed to the file
        // version rather than to a token count -- the name absorbs every remaining
        // token (it may contain spaces), so a trailing field would be unreadable.
        append_uint(s, ds.text_fit);
        s += ' ';
        s += ds.name;
        s += '\n';
    }

    // PAGESETUP <name> <paper> <target> pw ph land area wminx wminy wmaxx wmaxy fit
    //           snum sden center offx offy plw style  -- the three strings have their
    //           spaces escaped to 0x1f so every field is one whitespace-free token.
    const auto enc = [](const std::string& in) -> std::string {
        std::string out = in.empty() ? std::string("-") : in;
        for (char& c : out) {
            if (c == ' ') {
                c = '\x1f';
            }
        }
        return out;
    };
    // v30: layouts -- id, then the same fields a PAGESETUP record carries.
    for (const Layout& l : doc.layouts) {
        const PageSetup& ps = l.page;
        s += "LAYOUT ";
        append_uint(s, l.id);
        s += ' ';
        s += enc(l.name);
        s += ' ';
        s += enc(ps.paper);
        s += ' ';
        s += enc(ps.target);
        s += ' ';
        append_double(s, ps.paper_w_mm);
        s += ' ';
        append_double(s, ps.paper_h_mm);
        s += ' ';
        append_uint(s, ps.landscape ? 1 : 0);
        s += ' ';
        append_uint(s, ps.area);
        s += ' ';
        append_double(s, ps.win_min.x);
        s += ' ';
        append_double(s, ps.win_min.y);
        s += ' ';
        append_double(s, ps.win_max.x);
        s += ' ';
        append_double(s, ps.win_max.y);
        s += ' ';
        append_uint(s, ps.fit ? 1 : 0);
        s += ' ';
        append_double(s, ps.scale_num);
        s += ' ';
        append_double(s, ps.scale_den);
        s += ' ';
        append_uint(s, ps.center ? 1 : 0);
        s += ' ';
        append_double(s, ps.off_x_mm);
        s += ' ';
        append_double(s, ps.off_y_mm);
        s += ' ';
        append_uint(s, ps.plot_lineweights ? 1 : 0);
        s += ' ';
        append_uint(s, ps.style);
        s += '\n';
    }
    s += "ACTIVESPACE ";
    append_uint(s, doc.active_space);
    s += '\n';
    for (const PageSetup& ps : doc.page_setups) {
        s += "PAGESETUP ";
        s += enc(ps.name);
        s += ' ';
        s += enc(ps.paper);
        s += ' ';
        s += enc(ps.target);
        s += ' ';
        append_double(s, ps.paper_w_mm);
        s += ' ';
        append_double(s, ps.paper_h_mm);
        s += ' ';
        append_uint(s, ps.landscape ? 1 : 0);
        s += ' ';
        append_uint(s, ps.area);
        s += ' ';
        append_double(s, ps.win_min.x);
        s += ' ';
        append_double(s, ps.win_min.y);
        s += ' ';
        append_double(s, ps.win_max.x);
        s += ' ';
        append_double(s, ps.win_max.y);
        s += ' ';
        append_uint(s, ps.fit ? 1 : 0);
        s += ' ';
        append_double(s, ps.scale_num);
        s += ' ';
        append_double(s, ps.scale_den);
        s += ' ';
        append_uint(s, ps.center ? 1 : 0);
        s += ' ';
        append_double(s, ps.off_x_mm);
        s += ' ';
        append_double(s, ps.off_y_mm);
        s += ' ';
        append_uint(s, ps.plot_lineweights ? 1 : 0);
        s += ' ';
        append_uint(s, ps.style);
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
    // ELLIPSE cx cy mx my ratio start end <props7>  (v22).
    for (const DocEllipse& e : doc.ellipses) {
        s += "ELLIPSE ";
        append_vec(s, e.center);
        s += ' ';
        append_vec(s, e.major);
        s += ' ';
        append_double(s, e.ratio);
        s += ' ';
        append_double(s, e.start);
        s += ' ';
        append_double(s, e.end);
        append_props(s, e.props);
        s += '\n';
    }
    // XLINE basex basey dirx diry ray <props7>  (v21). Construction line / ray.
    for (const DocXline& x : doc.xlines) {
        s += "XLINE ";
        append_vec(s, x.base);
        s += ' ';
        append_vec(s, x.dir);
        s += ' ';
        append_uint(s, x.ray ? 1 : 0);
        append_props(s, x.props);
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
        s += ' ';
        append_uint(s, t.style); // v26: text style index (14th token)
        s += '\n';
        s += t.content;
        s += '\n';
        s += t.font; // v10: font name on its own line (single-line; "" = stroke)
        s += '\n';
    }
    for (const DocAttDef& a : doc.attdefs) {
        // v28: ATTDEF = a TEXT record (+ modes as a 15th token) whose content line is the
        // tag, then the font line, then the prompt and default lines.
        s += "ATTDEF ";
        append_vec(s, a.text.pos);
        s += ' ';
        append_double(s, a.text.height);
        s += ' ';
        append_double(s, a.text.rotation);
        s += ' ';
        append_uint(s, a.text.justify);
        append_props(s, a.text.props);
        s += ' ';
        append_uint(s, a.text.style);
        s += ' ';
        append_uint(s, a.flags);
        s += '\n';
        s += a.text.content;
        s += '\n';
        s += a.text.font;
        s += '\n';
        s += a.prompt;
        s += '\n';
        s += a.def;
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
        append_overrides(s, d.overrides); // per-dimension overrides (v8): full block => lossless
        // v15 decoration: mode + the two deviations inline, then the raw prefix/suffix
        // on their own lines (they may contain spaces, so they cannot be tokens).
        s += ' ';
        append_uint(s, static_cast<std::uint64_t>(d.tol.mode));
        s += ' ';
        append_double(s, d.tol.upper);
        s += ' ';
        append_double(s, d.tol.lower);
        // v19: the label displacement inline, the raw override on its own line (it may
        // contain spaces, so it cannot be a token -- the same reason prefix/suffix are lines).
        s += ' ';
        append_vec(s, d.text_offset);
        // v23: the extra datum (ordinate axis / jog position / arc end angle).
        s += ' ';
        append_double(s, d.aux);
        s += '\n';
        s += d.prefix;
        s += '\n';
        s += d.suffix;
        s += '\n';
        s += d.text_override;
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
        append_overrides(s, l.overrides); // v13: per-leader arrow override block
        s += '\n';
        s += l.content;
        s += '\n';
        s += l.font; // v10
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
        s += m.font; // v10
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
        append_overrides(s, m.overrides); // v13: per-leader arrow override block
        s += '\n';
        s += escape(m.content);
        s += '\n';
        s += m.font; // v10
        s += '\n';
    }
    // v14: HATCH. One line: loop count, then each loop (vertex count + x y pairs), then
    // scale, angle(radians), origin, props. The pattern name is on the next line.
    for (const DocHatch& h : doc.hatches) {
        s += "HATCH ";
        append_uint(s, h.loops.size());
        for (const std::vector<Vec2>& loop : h.loops) {
            s += ' ';
            append_uint(s, loop.size());
            for (const Vec2& v : loop) {
                s += ' ';
                append_vec(s, v);
            }
        }
        s += ' ';
        append_double(s, h.pattern_scale);
        s += ' ';
        append_double(s, h.pattern_angle);
        s += ' ';
        append_vec(s, h.pattern_origin);
        append_props(s, h.props);
        // v27: the GRADIENT second colour.
        s += ' ';
        append_uint(s, h.color2.r);
        s += ' ';
        append_uint(s, h.color2.g);
        s += ' ';
        append_uint(s, h.color2.b);
        s += '\n';
        s += escape(h.pattern_name);
        s += '\n';
    }
    // v9: model-space block references, then the block-definition table. A block's
    // content reuses the same per-entity record formats, bracketed BLOCKDEF..ENDBLOCKDEF.
    const auto emit_insert_rec = [&](const DocInsert& ins) {
        s += "INSERT ";
        append_vec(s, ins.pos);
        s += ' ';
        append_double(s, ins.scale_x);
        s += ' ';
        append_double(s, ins.scale_y);
        s += ' ';
        append_double(s, ins.rotation);
        append_props(s, ins.props);
        s += ' ';
        append_uint(s, ins.attribs.size()); // v28: attribute lines that follow the name
        s += '\n';
        s += escape(ins.block_name); // own line: may contain spaces
        s += '\n';
        for (const DocAttrib& a : ins.attribs) {
            s += a.tag; // a tag has no spaces (AutoCAD forbids them); the value may
            s += ' ';
            s += a.value;
            s += '\n';
        }
    };
    const auto emit_block_content = [&](const DocBlockDef& b) {
        for (const DocLine& l : b.lines) {
            s += "LINE ";
            append_vec(s, l.a);
            s += ' ';
            append_vec(s, l.b);
            append_props(s, l.props);
            s += '\n';
        }
        for (const DocCircle& c : b.circles) {
            s += "CIRCLE ";
            append_vec(s, c.center);
            s += ' ';
            append_double(s, c.radius);
            append_props(s, c.props);
            s += '\n';
        }
        for (const DocArc& a : b.arcs) {
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
        for (const DocPolyline& p : b.polylines) {
            s += "POLYLINE ";
            s += p.closed ? '1' : '0';
            s += ' ';
            append_uint(s, p.points.size());
            for (const Vec2& v : p.points) {
                s += ' ';
                append_vec(s, v);
            }
            append_props(s, p.props);
            if (p.bulges.size() == p.points.size()) {
                for (const double bg : p.bulges) {
                    s += ' ';
                    append_double(s, bg);
                }
            }
            s += '\n';
        }
        for (const DocText& t : b.texts) {
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
            s += t.font; // v10
            s += '\n';
        }
        for (const DocAttDef& a : b.attdefs) {
            // v28: ATTDEF = a TEXT record (+ modes as a 15th token) whose content line is the
            // tag, then the font line, then the prompt and default lines.
            s += "ATTDEF ";
            append_vec(s, a.text.pos);
            s += ' ';
            append_double(s, a.text.height);
            s += ' ';
            append_double(s, a.text.rotation);
            s += ' ';
            append_uint(s, a.text.justify);
            append_props(s, a.text.props);
            s += ' ';
            append_uint(s, a.text.style);
            s += ' ';
            append_uint(s, a.flags);
            s += '\n';
            s += a.text.content;
            s += '\n';
            s += a.text.font;
            s += '\n';
            s += a.prompt;
            s += '\n';
            s += a.def;
            s += '\n';
        }
        for (const DocMText& m : b.mtexts) {
            s += "MTEXT ";
            append_block(m.block);
            append_props(s, m.props);
            s += '\n';
            s += escape(m.content);
            s += '\n';
            s += m.font; // v10
            s += '\n';
        }
        for (const DocInsert& ni : b.inserts) {
            emit_insert_rec(ni);
        }
    };
    for (const DocInsert& ins : doc.inserts) {
        emit_insert_rec(ins);
    }
    for (const DocBlockDef& b : doc.block_defs) {
        s += "BLOCKDEF ";
        append_vec(s, b.base);
        s += '\n';
        s += escape(b.name);
        s += '\n';
        emit_block_content(b);
        s += "ENDBLOCKDEF\n";
    }
    // v12: per-entity CELTSCALE (linetype scale) for the dashing kinds. Decoupled trailing
    // records (only non-default values), keyed by kindcode + ordinal into the doc vectors,
    // so the entity record formats are unchanged and older readers never see these.
    // kindcode: 0=line, 1=circle, 2=arc, 3=polyline.
    const auto emit_celt = [&](int kindcode, std::size_t i, double cs) {
        if (cs == 1.0) {
            return;
        }
        s += "CELTSCALE ";
        append_uint(s, static_cast<std::uint64_t>(kindcode));
        s += ' ';
        append_uint(s, i);
        s += ' ';
        append_double(s, cs);
        s += '\n';
    };
    for (std::size_t i = 0; i < doc.lines.size(); ++i) {
        emit_celt(0, i, doc.lines[i].celtscale);
    }
    for (std::size_t i = 0; i < doc.circles.size(); ++i) {
        emit_celt(1, i, doc.circles[i].celtscale);
    }
    for (std::size_t i = 0; i < doc.arcs.size(); ++i) {
        emit_celt(2, i, doc.arcs[i].celtscale);
    }
    for (std::size_t i = 0; i < doc.polylines.size(); ++i) {
        emit_celt(3, i, doc.polylines[i].celtscale);
    }
    // v26: TEXTSTYLE height width_factor oblique name font  (strings space-escaped), and
    // CURTEXTSTYLE index. Written before entities so TEXT can reference them.
    for (const TextStyle& ts : doc.text_styles) {
        s += "TEXTSTYLE ";
        append_double(s, ts.height);
        s += ' ';
        append_double(s, ts.width_factor);
        s += ' ';
        append_double(s, ts.oblique);
        s += ' ';
        s += enc(ts.name);
        s += ' ';
        s += enc(ts.font);
        s += '\n';
    }
    s += "CURTEXTSTYLE ";
    append_uint(s, doc.current_text_style);
    s += '\n';
    s += "WIPEOUTFRAME ";
    append_uint(s, doc.wipeout_frames ? 1 : 0);
    s += '\n';
    s += "ATTDISP ";
    append_uint(s, doc.attdisp);
    s += '\n';
    s += "IMAGEFRAME ";
    append_uint(s, doc.image_frame);
    s += '\n';
    // v25: UNITSFMT linear precision angular aprecision clockwise base_angle
    s += "UNITSFMT ";
    append_uint(s, static_cast<std::uint64_t>(doc.display_units.linear));
    s += ' ';
    append_uint(s, doc.display_units.linear_precision);
    s += ' ';
    append_uint(s, static_cast<std::uint64_t>(doc.display_units.angular));
    s += ' ';
    append_uint(s, doc.display_units.angular_precision);
    s += ' ';
    append_uint(s, doc.display_units.clockwise ? 1 : 0);
    s += ' ';
    append_double(s, doc.display_units.base_angle);
    s += '\n';
    // v24: VIEW cx cy scale name  and  GROUP selectable n k1 i1 ... kn in name description
    // (names/descriptions space-escaped like PAGESETUP's strings; "-" = empty).
    for (const NamedView& v : doc.views) {
        s += "VIEW ";
        append_vec(s, v.center);
        s += ' ';
        append_double(s, v.scale);
        s += ' ';
        s += enc(v.name);
        s += '\n';
    }
    for (const DocGroup& g : doc.groups) {
        s += "GROUP ";
        append_uint(s, g.selectable ? 1 : 0);
        s += ' ';
        append_uint(s, g.members.size());
        for (const auto& [k, i] : g.members) {
            s += ' ';
            append_uint(s, k);
            s += ' ';
            append_uint(s, i);
        }
        s += ' ';
        s += enc(g.name);
        s += ' ';
        s += enc(g.description);
        s += '\n';
    }
    // v20: TABLESTYLE + TABLE. The style table first, so a TABLE can reference it.
    // TABLESTYLE <title_h> <header_h> <data_h> <margin> <lw> <line ec4> <text ec4> name...
    for (const TableStyle& ts : doc.table_styles) {
        s += "TABLESTYLE ";
        append_double(s, ts.title_height);
        s += ' ';
        append_double(s, ts.header_height);
        s += ' ';
        append_double(s, ts.data_height);
        s += ' ';
        append_double(s, ts.margin);
        s += ' ';
        append_uint(s, ts.lineweight);
        s += ' ';
        append_ecolor(s, ts.line_color);
        append_ecolor(s, ts.text_color);
        s += ts.name; // absorbs the rest of the line, so it may contain spaces
        s += '\n';
    }
    // TABLE rows cols has_title has_header px py rot style <props7>; then one line of
    // column widths, one of row heights, then per cell: "span_cols span_rows align" and
    // the raw text on the following line (it may contain spaces, so it cannot be a token).
    for (const DocTable& t : doc.tables) {
        s += "TABLE ";
        append_uint(s, t.rows);
        s += ' ';
        append_uint(s, t.cols);
        s += ' ';
        append_uint(s, t.has_title ? 1 : 0);
        s += ' ';
        append_uint(s, t.has_header ? 1 : 0);
        s += ' ';
        append_vec(s, t.pos);
        s += ' ';
        append_double(s, t.rotation);
        s += ' ';
        append_uint(s, t.style);
        append_props(s, t.props);
        s += '\n';
        for (std::size_t k = 0; k < t.col_widths.size(); ++k) {
            if (k != 0) {
                s += ' ';
            }
            append_double(s, t.col_widths[k]);
        }
        s += '\n';
        for (std::size_t k = 0; k < t.row_heights.size(); ++k) {
            if (k != 0) {
                s += ' ';
            }
            append_double(s, t.row_heights[k]);
        }
        s += '\n';
        for (const DocTableCell& c : t.cells) {
            append_uint(s, c.span_cols);
            s += ' ';
            append_uint(s, c.span_rows);
            s += ' ';
            append_uint(s, c.align);
            s += '\n';
            s += c.text;
            s += '\n';
        }
    }
    // v18: raster images. IMAGEDEF <pixel_w> <pixel_h> <b64chunks> <sourcelen>; then the
    // source line, then `b64chunks` lines of base64. Chunking is forced by the format
    // being line-oriented -- an embedded PNG is far too long for one record.
    for (const DocImageDef& d : doc.image_defs) {
        const std::string b64 = base64_encode(d.bytes);
        constexpr std::size_t kChunk = 76; // classic base64 line length
        const std::size_t chunks = (b64.size() + kChunk - 1) / kChunk;
        s += "IMAGEDEF ";
        append_uint(s, d.pixel_w);
        s += ' ';
        append_uint(s, d.pixel_h);
        s += ' ';
        append_uint(s, chunks);
        s += '\n';
        s += d.source; // may be empty (embedded-only) and may contain spaces
        s += '\n';
        for (std::size_t k = 0; k < chunks; ++k) {
            s += b64.substr(k * kChunk, kChunk);
            s += '\n';
        }
    }
    // IMAGE def px py w h rot clipped u0 v0 u1 v1 <props7>
    for (const DocImage& im : doc.images) {
        s += "IMAGE ";
        append_uint(s, im.def);
        s += ' ';
        append_vec(s, im.pos);
        s += ' ';
        append_double(s, im.width);
        s += ' ';
        append_double(s, im.height);
        s += ' ';
        append_double(s, im.rotation);
        s += ' ';
        append_uint(s, im.clipped ? 1 : 0);
        s += ' ';
        append_double(s, im.clip_u0);
        s += ' ';
        append_double(s, im.clip_v0);
        s += ' ';
        append_double(s, im.clip_u1);
        s += ' ';
        append_double(s, im.clip_v1);
        append_props(s, im.props);
        s += '\n';
    }
    // v17: GD&T. FCF <cellcount> px py rot style <props7> <override16>; then one RAW cell
    // string per following line (cells may contain spaces, so they cannot be tokens --
    // the same reason TEXT puts its content on its own line).
    for (const DocFcf& f : doc.fcfs) {
        s += "FCF ";
        append_uint(s, f.cells.size());
        s += ' ';
        append_vec(s, f.pos);
        s += ' ';
        append_double(s, f.rotation);
        s += ' ';
        append_uint(s, f.style);
        append_props(s, f.props);
        append_overrides(s, f.overrides);
        s += '\n';
        for (const std::string& c : f.cells) {
            s += c;
            s += '\n';
        }
    }
    // DATUM tipx tipy px py rot style <props7> <override16>; the letter on the next line.
    for (const DocDatum& d : doc.datums) {
        s += "DATUM ";
        append_vec(s, d.tip);
        s += ' ';
        append_vec(s, d.pos);
        s += ' ';
        append_double(s, d.rotation);
        s += ' ';
        append_uint(s, d.style);
        append_props(s, d.props);
        append_overrides(s, d.overrides);
        s += '\n';
        s += d.letter;
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

    // Entity records route to model space by default, or to the block currently being
    // read between BLOCKDEF and ENDBLOCKDEF (v9). The DocBlockDef is a stable local, so
    // the target pointers stay valid as it is filled then moved into the doc.
    DocBlockDef cur_block;
    bool in_block = false;
    std::vector<DocLine>* t_lines = &doc.lines;
    std::vector<DocCircle>* t_circles = &doc.circles;
    std::vector<DocArc>* t_arcs = &doc.arcs;
    std::vector<DocPolyline>* t_polylines = &doc.polylines;
    std::vector<DocText>* t_texts = &doc.texts;
    std::vector<DocAttDef>* t_attdefs = &doc.attdefs;
    std::vector<DocMText>* t_mtexts = &doc.mtexts;
    std::vector<DocInsert>* t_inserts = &doc.inserts;
    const auto target_model = [&] {
        t_lines = &doc.lines;
        t_circles = &doc.circles;
        t_arcs = &doc.arcs;
        t_polylines = &doc.polylines;
        t_texts = &doc.texts;
        t_attdefs = &doc.attdefs;
        t_mtexts = &doc.mtexts;
        t_inserts = &doc.inserts;
    };
    const auto target_block = [&] {
        t_lines = &cur_block.lines;
        t_circles = &cur_block.circles;
        t_arcs = &cur_block.arcs;
        t_polylines = &cur_block.polylines;
        t_texts = &cur_block.texts;
        t_attdefs = &cur_block.attdefs;
        t_mtexts = &cur_block.mtexts;
        t_inserts = &cur_block.inserts;
    };

    const auto fail = [&](const std::string& why) {
        return IoResult::failure("Line " + std::to_string(line_no) + ": " + why);
    };

    std::istringstream in{std::string(text)};
    // v10: a font-name line follows the content of TEXT/MTEXT/LEADER/MLEADER. Older files
    // have none -> the stroke font (""). Returns false only on a truncated v10 record.
    // Reads one raw following line (content that may be empty or contain spaces, so it
    // cannot be tokenised). Shared by the v15 DIM prefix/suffix.
    const auto read_line = [&](std::string& out_line) -> bool {
        out_line.clear();
        if (!std::getline(in, out_line)) {
            return false;
        }
        ++line_no;
        if (!out_line.empty() && out_line.back() == '\r') {
            out_line.pop_back();
        }
        return true;
    };
    const auto read_font = [&](std::string& font) -> bool {
        font.clear();
        if (version < 10) {
            return true;
        }
        if (!std::getline(in, font)) {
            return false;
        }
        ++line_no;
        if (!font.empty() && font.back() == '\r') {
            font.pop_back();
        }
        return true;
    };
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
        } else if (key == "TEXTSTYLE") {
            if (tok.size() != 6) {
                return fail("malformed TEXTSTYLE");
            }
            const auto dec = [](std::string_view t) -> std::string {
                std::string r(t);
                for (char& c : r) {
                    if (c == '\x1f') {
                        c = ' ';
                    }
                }
                return r == "-" ? std::string{} : r;
            };
            TextStyle ts;
            if (!to_double(tok[1], ts.height) || !to_double(tok[2], ts.width_factor) ||
                !to_double(tok[3], ts.oblique)) {
                return fail("malformed TEXTSTYLE");
            }
            ts.name = dec(tok[4]);
            ts.font = dec(tok[5]);
            doc.text_styles.push_back(std::move(ts));
        } else if (key == "IMAGEFRAME") {
            std::uint64_t mode = 1;
            if (tok.size() != 2 || !to_uint(tok[1], mode) || mode > 2) {
                return fail("malformed IMAGEFRAME");
            }
            doc.image_frame = static_cast<std::uint8_t>(mode);
        } else if (key == "ATTDISP") {
            std::uint64_t mode = 0;
            if (tok.size() != 2 || !to_uint(tok[1], mode) || mode > 2) {
                return fail("malformed ATTDISP");
            }
            doc.attdisp = static_cast<std::uint8_t>(mode);
        } else if (key == "WIPEOUTFRAME") {
            std::uint64_t on = 1;
            if (tok.size() != 2 || !to_uint(tok[1], on)) {
                return fail("malformed WIPEOUTFRAME");
            }
            doc.wipeout_frames = on != 0;
        } else if (key == "CURTEXTSTYLE") {
            std::uint64_t i = 0;
            if (tok.size() != 2 || !to_uint(tok[1], i)) {
                return fail("malformed CURTEXTSTYLE");
            }
            doc.current_text_style = static_cast<std::uint16_t>(i);
        } else if (key == "UNITSFMT") {
            std::uint64_t v[5] = {2, 4, 0, 0, 0};
            if (tok.size() != 7 || !to_uint(tok[1], v[0]) || !to_uint(tok[2], v[1]) ||
                !to_uint(tok[3], v[2]) || !to_uint(tok[4], v[3]) || !to_uint(tok[5], v[4]) ||
                !to_double(tok[6], doc.display_units.base_angle)) {
                return fail("malformed UNITSFMT");
            }
            doc.display_units.linear = static_cast<LinearFormat>(std::clamp<std::uint64_t>(v[0], 1, 5));
            doc.display_units.linear_precision = static_cast<std::uint8_t>(std::min<std::uint64_t>(v[1], 8));
            doc.display_units.angular = static_cast<AngleFormat>(std::min<std::uint64_t>(v[2], 4));
            doc.display_units.angular_precision = static_cast<std::uint8_t>(std::min<std::uint64_t>(v[3], 8));
            doc.display_units.clockwise = v[4] != 0;
        } else if (key == "VIEW") {
            if (tok.size() != 5) {
                return fail("malformed VIEW");
            }
            const auto dec = [](std::string_view t) -> std::string {
                std::string r(t);
                for (char& c : r) {
                    if (c == '\x1f') {
                        c = ' ';
                    }
                }
                return r == "-" ? std::string{} : r;
            };
            NamedView v;
            if (!to_double(tok[1], v.center.x) || !to_double(tok[2], v.center.y) ||
                !to_double(tok[3], v.scale)) {
                return fail("malformed VIEW");
            }
            v.name = dec(tok[4]);
            doc.views.push_back(std::move(v));
        } else if (key == "GROUP") {
            std::uint64_t sel = 0;
            std::uint64_t n = 0;
            if (tok.size() < 5 || !to_uint(tok[1], sel) || !to_uint(tok[2], n) ||
                tok.size() != 5 + 2 * n) {
                return fail("malformed GROUP");
            }
            const auto dec = [](std::string_view t) -> std::string {
                std::string r(t);
                for (char& c : r) {
                    if (c == '\x1f') {
                        c = ' ';
                    }
                }
                return r == "-" ? std::string{} : r;
            };
            DocGroup g;
            g.selectable = sel != 0;
            for (std::uint64_t i = 0; i < n; ++i) {
                std::uint64_t k = 0;
                std::uint64_t idx = 0;
                if (!to_uint(tok[3 + 2 * i], k) || !to_uint(tok[4 + 2 * i], idx)) {
                    return fail("malformed GROUP member");
                }
                g.members.emplace_back(static_cast<std::uint8_t>(k), static_cast<std::uint32_t>(idx));
            }
            g.name = dec(tok[3 + 2 * n]);
            g.description = dec(tok[4 + 2 * n]);
            doc.groups.push_back(std::move(g));
        } else if (key == "LAYOUT") {
            // LAYOUT id name paper target pw ph land area wminx wminy wmaxx wmaxy fit snum sden
            //        center offx offy plw style   (the PAGESETUP fields after the id)
            if (tok.size() != 21) {
                return fail("malformed LAYOUT");
            }
            const auto dec = [](std::string_view t) -> std::string {
                std::string r(t);
                for (char& c : r) {
                    if (c == '\x1f') {
                        c = ' ';
                    }
                }
                return r == "-" ? std::string{} : r;
            };
            Layout l;
            std::uint64_t id = 0;
            double d[8];
            std::uint64_t u[6];
            const bool ok = to_uint(tok[1], id) && to_double(tok[5], l.page.paper_w_mm) &&
                            to_double(tok[6], l.page.paper_h_mm) && to_uint(tok[7], u[0]) &&
                            to_uint(tok[8], u[1]) && to_double(tok[9], d[0]) &&
                            to_double(tok[10], d[1]) && to_double(tok[11], d[2]) &&
                            to_double(tok[12], d[3]) && to_uint(tok[13], u[2]) &&
                            to_double(tok[14], d[4]) && to_double(tok[15], d[5]) &&
                            to_uint(tok[16], u[3]) && to_double(tok[17], d[6]) &&
                            to_double(tok[18], d[7]) && to_uint(tok[19], u[4]) &&
                            to_uint(tok[20], u[5]);
            if (!ok || id == 0 || id > 15) {
                return fail("malformed LAYOUT");
            }
            l.page.center = u[3] != 0;
            l.page.off_x_mm = d[6];
            l.page.off_y_mm = d[7];
            l.page.plot_lineweights = u[4] != 0;
            l.page.style = static_cast<std::uint8_t>(u[5]);
            l.id = static_cast<std::uint8_t>(id);
            l.name = dec(tok[2]);
            l.page.paper = dec(tok[3]);
            l.page.target = dec(tok[4]);
            l.page.landscape = u[0] != 0;
            l.page.area = static_cast<std::uint8_t>(u[1]);
            l.page.win_min = {d[0], d[1]};
            l.page.win_max = {d[2], d[3]};
            l.page.fit = u[2] != 0;
            l.page.scale_num = d[4];
            l.page.scale_den = d[5];
            doc.layouts.push_back(std::move(l));
        } else if (key == "ACTIVESPACE") {
            std::uint64_t sp = 0;
            if (tok.size() != 2 || !to_uint(tok[1], sp) || sp > 15) {
                return fail("malformed ACTIVESPACE");
            }
            doc.active_space = static_cast<std::uint8_t>(sp);
        } else if (key == "PAGESETUP") {
            // PAGESETUP name paper target pw ph land area wminx wminy wmaxx wmaxy fit
            //           snum sden center offx offy plw style  (3 strings space-escaped)
            if (tok.size() != 20) {
                return fail("malformed PAGESETUP");
            }
            const auto dec = [](std::string_view t) -> std::string {
                std::string r(t);
                for (char& c : r) {
                    if (c == '\x1f') {
                        c = ' ';
                    }
                }
                return r == "-" ? std::string{} : r;
            };
            PageSetup ps;
            ps.name = dec(tok[1]);
            ps.paper = dec(tok[2]);
            ps.target = dec(tok[3]);
            double d[8];
            std::uint64_t u[6];
            const bool ok = to_double(tok[4], ps.paper_w_mm) && to_double(tok[5], ps.paper_h_mm) &&
                            to_uint(tok[6], u[0]) && to_uint(tok[7], u[1]) &&
                            to_double(tok[8], d[0]) && to_double(tok[9], d[1]) &&
                            to_double(tok[10], d[2]) && to_double(tok[11], d[3]) &&
                            to_uint(tok[12], u[2]) && to_double(tok[13], d[4]) &&
                            to_double(tok[14], d[5]) && to_uint(tok[15], u[3]) &&
                            to_double(tok[16], d[6]) && to_double(tok[17], d[7]) &&
                            to_uint(tok[18], u[4]) && to_uint(tok[19], u[5]);
            if (!ok) {
                return fail("malformed PAGESETUP");
            }
            ps.landscape = u[0] != 0;
            ps.area = static_cast<std::uint8_t>(u[1]);
            ps.win_min = {d[0], d[1]};
            ps.win_max = {d[2], d[3]};
            ps.fit = u[2] != 0;
            ps.scale_num = d[4];
            ps.scale_den = d[5];
            ps.center = u[3] != 0;
            ps.off_x_mm = d[6];
            ps.off_y_mm = d[7];
            ps.plot_lineweights = u[4] != 0;
            ps.style = static_cast<std::uint8_t>(u[5]);
            doc.page_setups.push_back(std::move(ps));
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
                // v16 inserts text_fit before the name (see the writer: a trailing field
                // could not be told apart from the first word of a multi-word name).
                if (version >= 16 && tok.size() >= 27) {
                    std::uint64_t tf = 0;
                    to_uint(tok[25], tf);
                    ds.text_fit = static_cast<std::uint8_t>(std::min<std::uint64_t>(tf, 2));
                    name_at = 26;
                }
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
            std::uint64_t tstyle = 0;
            if (tok.size() >= 14 && !to_uint(tok[13], tstyle)) {
                return fail("TEXT style malformed");
            }
            std::string content;
            if (!std::getline(in, content)) {
                return fail("TEXT missing content line");
            }
            ++line_no;
            if (!content.empty() && content.back() == '\r') {
                content.pop_back();
            }
            std::string tfont;
            if (!read_font(tfont)) {
                return fail("TEXT missing font line");
            }
            t_texts->push_back(DocText{{vals[0], vals[1]},
                                        vals[2],
                                        vals[3],
                                        static_cast<std::uint8_t>(justify),
                                        std::move(content),
                                        props,
                                        std::move(tfont),
                                        static_cast<std::uint16_t>(tstyle)});
        } else if (key == "ATTDEF") {
            // ATTDEF px py height rot justify <props7> style flags; then the tag line, the
            // font line, the prompt line and the default line (v28).
            vals.clear();
            EntityProps props;
            std::uint64_t justify = 0;
            std::uint64_t astyle = 0;
            std::uint64_t aflags = 0;
            if (tok.size() != 15 || !parse_doubles(tok, 1, 4, vals) || !to_uint(tok[5], justify) ||
                !parse_props(tok, 6, props) || !to_uint(tok[13], astyle) || !to_uint(tok[14], aflags)) {
                return fail("ATTDEF record malformed");
            }
            std::string lines[4];
            for (std::string& l : lines) {
                if (!std::getline(in, l)) {
                    return fail("ATTDEF missing a line (tag, font, prompt, default)");
                }
                ++line_no;
                if (!l.empty() && l.back() == '\r') {
                    l.pop_back();
                }
            }
            t_attdefs->push_back(DocAttDef{DocText{{vals[0], vals[1]},
                                                   vals[2],
                                                   vals[3],
                                                   static_cast<std::uint8_t>(justify),
                                                   std::move(lines[0]),
                                                   props,
                                                   std::move(lines[1]),
                                                   static_cast<std::uint16_t>(astyle)},
                                           std::move(lines[2]),
                                           std::move(lines[3]),
                                           static_cast<std::uint8_t>(aflags)});
        } else if (key == "DIM") {
            // DIM type ax ay bx by lx ly style <props7> [override] [tolmode up lo]
            // Token count is the version discriminator, as everywhere else in this
            // format:  16 = pre-v8 (no override block)
            //          31 = v8   override block (15 fields)
            //          34 = v15  + decoration (and a prefix + suffix line after the record)
            //          35 = v16  the override block gained text_fit (16 fields)
            std::uint64_t dtype = 0;
            vals.clear();
            // 37 = v19 (the override block's 16 fields + decoration + the 2-token offset)
            // 38 = v23 (+ the aux datum)
            const bool has_aux = tok.size() == 38;
            const bool has_move = tok.size() == 37 || has_aux;
            const bool has_fit = tok.size() == 35 || has_move;
            const bool has_decor = tok.size() == 34 || has_fit;
            const bool has_ov = tok.size() == 31 || has_decor;
            if ((tok.size() != 16 && !has_ov) || !to_uint(tok[1], dtype) ||
                !parse_doubles(tok, 2, 6, vals)) {
                return fail("DIM record malformed");
            }
            std::uint64_t style = 0;
            EntityProps props;
            if (!to_uint(tok[8], style) || !parse_props(tok, 9, props)) {
                return fail("DIM record malformed");
            }
            DimOverrides ov{};
            if (has_ov && !parse_overrides(tok, 16, ov, has_fit)) {
                return fail("DIM override block malformed");
            }
            DimTolerance tol{};
            std::string dprefix;
            std::string dsuffix;
            std::string doverride;
            Vec2 dtext_offset{};
            double daux = 0.0;
            if (has_decor) {
                // The decoration follows the override block, which is one field wider
                // from v16 -- so its offset is keyed to the same discriminator.
                const std::size_t db = has_fit ? 32u : 31u;
                std::uint64_t mode = 0;
                if (!to_uint(tok[db], mode) || !to_double(tok[db + 1], tol.upper) ||
                    !to_double(tok[db + 2], tol.lower)) {
                    return fail("DIM decoration malformed");
                }
                tol.mode = static_cast<TolMode>(std::min<std::uint64_t>(mode, 4));
                // The two raw strings follow on their own lines (they may be empty and
                // may contain spaces, so they cannot be tokens).
                if (!read_line(dprefix) || !read_line(dsuffix)) {
                    return fail("DIM missing prefix/suffix line");
                }
                if (has_move) {
                    if (!to_double(tok[db + 3], dtext_offset.x) ||
                        !to_double(tok[db + 4], dtext_offset.y)) {
                        return fail("DIM text offset malformed");
                    }
                    if (!read_line(doverride)) {
                        return fail("DIM missing text-override line");
                    }
                    if (has_aux && !to_double(tok[db + 5], daux)) {
                        return fail("DIM aux datum malformed");
                    }
                }
            }
            doc.dims.push_back(DocDim{static_cast<std::uint8_t>(dtype),
                                      {vals[0], vals[1]},
                                      {vals[2], vals[3]},
                                      {vals[4], vals[5]},
                                      static_cast<std::uint16_t>(style),
                                      props,
                                      ov,
                                      dprefix,
                                      dsuffix,
                                      tol,
                                      doverride,
                                      dtext_offset,
                                      daux});
        } else if (key == "TABLESTYLE") {
            if (tok.size() < 15) {
                return fail("malformed TABLESTYLE");
            }
            TableStyle ts;
            std::uint64_t lw = 0;
            if (!to_double(tok[1], ts.title_height) || !to_double(tok[2], ts.header_height) ||
                !to_double(tok[3], ts.data_height) || !to_double(tok[4], ts.margin) ||
                !to_uint(tok[5], lw)) {
                return fail("malformed TABLESTYLE field");
            }
            ts.lineweight = static_cast<std::uint8_t>(lw);
            const auto read_ec = [&](std::size_t i2, ElementColor& ec) {
                std::uint64_t by = 1, r = 0, g2 = 0, b = 0;
                to_uint(tok[i2], by);
                to_uint(tok[i2 + 1], r);
                to_uint(tok[i2 + 2], g2);
                to_uint(tok[i2 + 3], b);
                ec.by_layer = by != 0;
                ec.color = {static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g2),
                            static_cast<std::uint8_t>(b)};
            };
            read_ec(6, ts.line_color);
            read_ec(10, ts.text_color);
            std::string name(tok[14]);
            for (std::size_t k = 15; k < tok.size(); ++k) {
                name += ' ';
                name += std::string(tok[k]);
            }
            ts.name = name;
            doc.table_styles.push_back(std::move(ts));
        } else if (key == "TABLE") {
            std::uint64_t rows = 0, cols = 0, title = 0, header = 0, style = 0;
            vals.clear();
            EntityProps props;
            if (tok.size() != 9 + 7 || !to_uint(tok[1], rows) || !to_uint(tok[2], cols) ||
                !to_uint(tok[3], title) || !to_uint(tok[4], header) ||
                !parse_doubles(tok, 5, 3, vals) || !to_uint(tok[8], style) ||
                !parse_props(tok, 9, props)) {
                return fail("TABLE record malformed");
            }
            DocTable t;
            t.rows = static_cast<std::uint16_t>(rows);
            t.cols = static_cast<std::uint16_t>(cols);
            t.has_title = title != 0;
            t.has_header = header != 0;
            t.pos = {vals[0], vals[1]};
            t.rotation = vals[2];
            t.style = static_cast<std::uint16_t>(style);
            t.props = props;
            const auto read_doubles = [&](std::size_t n, std::vector<double>& dest) {
                std::string line;
                if (!read_line(line)) {
                    return false;
                }
                const std::vector<std::string_view> dt = tokenize(line);
                if (dt.size() != n) {
                    return false;
                }
                dest.resize(n);
                for (std::size_t k = 0; k < n; ++k) {
                    if (!to_double(dt[k], dest[k])) {
                        return false;
                    }
                }
                return true;
            };
            if (!read_doubles(t.cols, t.col_widths) || !read_doubles(t.rows, t.row_heights)) {
                return fail("TABLE size line malformed");
            }
            const std::size_t n_cells = static_cast<std::size_t>(t.rows) * t.cols;
            t.cells.reserve(n_cells);
            for (std::size_t k = 0; k < n_cells; ++k) {
                std::string meta;
                if (!read_line(meta)) {
                    return fail("TABLE missing cell line");
                }
                const std::vector<std::string_view> mt = tokenize(meta);
                std::uint64_t sc = 1, sr = 1, al = 1;
                if (mt.size() != 3 || !to_uint(mt[0], sc) || !to_uint(mt[1], sr) ||
                    !to_uint(mt[2], al)) {
                    return fail("TABLE cell metadata malformed");
                }
                DocTableCell c;
                c.span_cols = static_cast<std::uint16_t>(sc);
                c.span_rows = static_cast<std::uint16_t>(sr);
                c.align = static_cast<std::uint8_t>(std::min<std::uint64_t>(al, 2));
                if (!read_line(c.text)) {
                    return fail("TABLE missing cell text line");
                }
                t.cells.push_back(std::move(c));
            }
            doc.tables.push_back(std::move(t));
        } else if (key == "IMAGEDEF") {
            std::uint64_t pw = 0;
            std::uint64_t ph = 0;
            std::uint64_t chunks = 0;
            if (tok.size() != 4 || !to_uint(tok[1], pw) || !to_uint(tok[2], ph) ||
                !to_uint(tok[3], chunks)) {
                return fail("IMAGEDEF record malformed");
            }
            DocImageDef d;
            d.pixel_w = static_cast<std::uint32_t>(pw);
            d.pixel_h = static_cast<std::uint32_t>(ph);
            if (!read_line(d.source)) {
                return fail("IMAGEDEF missing source line");
            }
            std::string b64;
            for (std::uint64_t k = 0; k < chunks; ++k) {
                std::string chunk;
                if (!read_line(chunk)) {
                    return fail("IMAGEDEF missing payload line");
                }
                b64 += chunk;
            }
            if (!base64_decode(b64, d.bytes)) {
                return fail("IMAGEDEF payload is not valid base64");
            }
            doc.image_defs.push_back(std::move(d));
        } else if (key == "IMAGE") {
            // IMAGE def px py w h rot clipped u0 v0 u1 v1 <props7>
            std::uint64_t def = 0;
            std::uint64_t clipped = 0;
            vals.clear();
            EntityProps props;
            if (tok.size() != 12 + 7 || !to_uint(tok[1], def) || !parse_doubles(tok, 2, 5, vals) ||
                !to_uint(tok[7], clipped) || !parse_doubles(tok, 8, 4, vals) ||
                !parse_props(tok, 12, props)) {
                return fail("IMAGE record malformed");
            }
            DocImage im;
            im.def = static_cast<std::uint16_t>(def);
            im.pos = {vals[0], vals[1]};
            im.width = vals[2];
            im.height = vals[3];
            im.rotation = vals[4];
            im.clipped = clipped != 0;
            im.clip_u0 = vals[5];
            im.clip_v0 = vals[6];
            im.clip_u1 = vals[7];
            im.clip_v1 = vals[8];
            im.props = props;
            doc.images.push_back(im);
        } else if (key == "FCF") {
            // FCF <cellcount> px py rot style <props7> <override16>; cells on the
            // following lines (raw, may contain spaces).
            std::uint64_t ncell = 0;
            vals.clear();
            EntityProps props;
            std::uint64_t style = 0;
            if (tok.size() != 6 + 7 + 16 || !to_uint(tok[1], ncell) ||
                !parse_doubles(tok, 2, 3, vals) || !to_uint(tok[5], style) ||
                !parse_props(tok, 6, props)) {
                return fail("FCF record malformed");
            }
            DimOverrides ov{};
            if (!parse_overrides(tok, 13, ov, /*with_text_fit=*/true)) {
                return fail("FCF override block malformed");
            }
            DocFcf f;
            f.pos = {vals[0], vals[1]};
            f.rotation = vals[2];
            f.style = static_cast<std::uint16_t>(style);
            f.props = props;
            f.overrides = ov;
            f.cells.reserve(ncell);
            for (std::uint64_t k = 0; k < ncell; ++k) {
                std::string cell;
                if (!read_line(cell)) {
                    return fail("FCF missing cell line");
                }
                f.cells.push_back(std::move(cell));
            }
            doc.fcfs.push_back(std::move(f));
        } else if (key == "DATUM") {
            // DATUM tipx tipy px py rot style <props7> <override16>; letter next line.
            vals.clear();
            EntityProps props;
            std::uint64_t style = 0;
            if (tok.size() != 7 + 7 + 16 || !parse_doubles(tok, 1, 5, vals) ||
                !to_uint(tok[6], style) || !parse_props(tok, 7, props)) {
                return fail("DATUM record malformed");
            }
            DimOverrides ov{};
            if (!parse_overrides(tok, 14, ov, /*with_text_fit=*/true)) {
                return fail("DATUM override block malformed");
            }
            DocDatum d;
            d.tip = {vals[0], vals[1]};
            d.pos = {vals[2], vals[3]};
            d.rotation = vals[4];
            d.style = static_cast<std::uint16_t>(style);
            d.props = props;
            d.overrides = ov;
            if (!read_line(d.letter)) {
                return fail("DATUM missing letter line");
            }
            doc.datums.push_back(std::move(d));
        } else if (key == "LEADER") {
            // LEADER tipx tipy kneex kneey height style <props7> [<override15>]; content next line.
            vals.clear();
            EntityProps props;
            std::uint64_t style = 0;
            // v13 appends the override block (15 fields); v16 widens it to 16 (text_fit).
            const bool l_fit = tok.size() == 30;
            const bool l_has_ov = tok.size() == 29 || l_fit;
            if ((tok.size() != 14 && !l_has_ov) || !parse_doubles(tok, 1, 5, vals) ||
                !to_uint(tok[6], style) || !parse_props(tok, 7, props)) {
                return fail("LEADER record malformed");
            }
            DimOverrides lov{};
            if (l_has_ov && !parse_overrides(tok, 14, lov, l_fit)) {
                return fail("LEADER override block malformed");
            }
            std::string content;
            if (!std::getline(in, content)) {
                return fail("LEADER missing content line");
            }
            ++line_no;
            if (!content.empty() && content.back() == '\r') {
                content.pop_back();
            }
            std::string lfont;
            if (!read_font(lfont)) {
                return fail("LEADER missing font line");
            }
            doc.leaders.push_back(DocLeader{{vals[0], vals[1]},
                                            {vals[2], vals[3]},
                                            vals[4],
                                            static_cast<std::uint16_t>(style),
                                            std::move(content),
                                            props,
                                            std::move(lfont),
                                            lov});
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
            std::string mfont;
            if (!read_font(mfont)) {
                return fail("MTEXT missing font line");
            }
            t_mtexts->push_back(DocMText{b, unescape(content), props, std::move(mfont)});
        } else if (key == "MLEADER") {
            // MLEADER style nverts <x y...> px py width height rot wf ls attach <props7>.
            std::uint64_t style = 0;
            std::uint64_t nv = 0;
            if (tok.size() < 3 || !to_uint(tok[1], style) || !to_uint(tok[2], nv)) {
                return fail("MLEADER header malformed");
            }
            const std::size_t vbase = 3;
            const std::size_t bbase = vbase + nv * 2; // block fields start
            const std::size_t pbase = bbase + 7 + 1;  // props start (after 7 block + 1 attach)
            std::uint64_t attach = 0;
            vals.clear();
            std::vector<double> bvals;
            EntityProps props;
            const bool m_fit = tok.size() == pbase + 7 + 16; // v16 widened it (text_fit)
            const bool m_has_ov = tok.size() == pbase + 7 + 15 || m_fit;
            if ((tok.size() != pbase + 7 && !m_has_ov) || !parse_doubles(tok, vbase, nv * 2, vals) ||
                !parse_doubles(tok, bbase, 7, bvals) || !to_uint(tok[bbase + 7], attach) ||
                !parse_props(tok, pbase, props)) {
                return fail("MLEADER record malformed");
            }
            DimOverrides mov{};
            if (m_has_ov && !parse_overrides(tok, pbase + 7, mov, m_fit)) {
                return fail("MLEADER override block malformed");
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
            std::string mlfont;
            if (!read_font(mlfont)) {
                return fail("MLEADER missing font line");
            }
            doc.mleaders.push_back(DocMLeader{std::move(verts), static_cast<std::uint16_t>(style), b,
                                              unescape(content), props, std::move(mlfont), mov});
        } else if (key == "HATCH") {
            // HATCH nloops <loopN x y...>... scale angle ox oy <props7>; pattern on next line.
            std::uint64_t nloops = 0;
            if (tok.size() < 2 || !to_uint(tok[1], nloops)) {
                return fail("HATCH header malformed");
            }
            DocHatch h;
            h.loops.reserve(nloops);
            std::size_t ti = 2;
            for (std::uint64_t i = 0; i < nloops; ++i) {
                std::uint64_t nv = 0;
                if (ti >= tok.size() || !to_uint(tok[ti], nv)) {
                    return fail("HATCH loop count malformed");
                }
                ++ti;
                std::vector<Vec2> loop;
                loop.reserve(nv);
                for (std::uint64_t j = 0; j < nv; ++j) {
                    double x = 0.0;
                    double y = 0.0;
                    if (ti + 1 >= tok.size() || !to_double(tok[ti], x) || !to_double(tok[ti + 1], y)) {
                        return fail("HATCH vertex malformed");
                    }
                    loop.push_back({x, y});
                    ti += 2;
                }
                h.loops.push_back(std::move(loop));
            }
            if (ti + 11 > tok.size() || !to_double(tok[ti], h.pattern_scale) ||
                !to_double(tok[ti + 1], h.pattern_angle) ||
                !to_double(tok[ti + 2], h.pattern_origin.x) ||
                !to_double(tok[ti + 3], h.pattern_origin.y) || !parse_props(tok, ti + 4, h.props)) {
                return fail("HATCH fields malformed");
            }
            if (tok.size() >= ti + 14) { // v27: the GRADIENT second colour
                std::uint64_t cr = 0;
                std::uint64_t cg = 0;
                std::uint64_t cb = 0;
                if (!to_uint(tok[ti + 11], cr) || !to_uint(tok[ti + 12], cg) || !to_uint(tok[ti + 13], cb)) {
                    return fail("HATCH colour malformed");
                }
                h.color2 = Rgb{static_cast<std::uint8_t>(cr), static_cast<std::uint8_t>(cg),
                               static_cast<std::uint8_t>(cb)};
            }
            std::string pat;
            if (!std::getline(in, pat)) {
                return fail("HATCH missing pattern line");
            }
            ++line_no;
            if (!pat.empty() && pat.back() == '\r') {
                pat.pop_back();
            }
            h.pattern_name = unescape(pat);
            doc.hatches.push_back(std::move(h));
        } else if (key == "POINT") {
            EntityProps p;
            if (!read_fixed(tok, 2, p)) {
                return fail("POINT record malformed");
            }
            doc.points.push_back(DocPoint{{vals[0], vals[1]}, p});
        } else if (key == "ELLIPSE") {
            EntityProps p;
            if (!read_fixed(tok, 7, p)) {
                return fail("ELLIPSE record malformed");
            }
            doc.ellipses.push_back(DocEllipse{
                {vals[0], vals[1]}, {vals[2], vals[3]}, vals[4], vals[5], vals[6], p});
        } else if (key == "XLINE") {
            EntityProps p;
            if (!read_fixed(tok, 5, p)) {
                return fail("XLINE record malformed");
            }
            doc.xlines.push_back(
                DocXline{{vals[0], vals[1]}, {vals[2], vals[3]}, vals[4] != 0.0, p});
        } else if (key == "LINE") {
            EntityProps p;
            if (!read_fixed(tok, 4, p)) {
                return fail("LINE record malformed");
            }
            t_lines->push_back(DocLine{{vals[0], vals[1]}, {vals[2], vals[3]}, p});
        } else if (key == "CIRCLE") {
            EntityProps p;
            if (!read_fixed(tok, 3, p)) {
                return fail("CIRCLE record malformed");
            }
            t_circles->push_back(DocCircle{{vals[0], vals[1]}, vals[2], p});
        } else if (key == "ARC") {
            EntityProps p;
            if (!read_fixed(tok, 5, p)) {
                return fail("ARC record malformed");
            }
            t_arcs->push_back(DocArc{{vals[0], vals[1]}, vals[2], vals[3], vals[4], p});
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
            t_polylines->push_back(std::move(pl));
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
        } else if (key == "INSERT") {
            // INSERT px py sx sy rot <props7>; block name on the next line (v9).
            vals.clear();
            EntityProps props;
            if ((tok.size() != 13 && tok.size() != 14) || !parse_doubles(tok, 1, 5, vals) ||
                !parse_props(tok, 6, props)) {
                return fail("INSERT record malformed");
            }
            std::uint64_t nattr = 0;
            if (tok.size() == 14 && !to_uint(tok[13], nattr)) { // v28
                return fail("INSERT attribute count malformed");
            }
            std::string name;
            if (!std::getline(in, name)) {
                return fail("INSERT missing block name");
            }
            ++line_no;
            if (!name.empty() && name.back() == '\r') {
                name.pop_back();
            }
            DocInsert di{unescape(name), {vals[0], vals[1]}, vals[2], vals[3], vals[4], props, {}};
            for (std::uint64_t k = 0; k < nattr; ++k) {
                std::string al;
                if (!std::getline(in, al)) {
                    return fail("INSERT missing attribute line");
                }
                ++line_no;
                if (!al.empty() && al.back() == '\r') {
                    al.pop_back();
                }
                const std::size_t sp = al.find(' ');
                di.attribs.push_back(DocAttrib{al.substr(0, sp),
                                               sp == std::string::npos ? std::string() : al.substr(sp + 1)});
            }
            t_inserts->push_back(std::move(di));
        } else if (key == "BLOCKDEF") {
            // BLOCKDEF basex basey; name on the next line; content until ENDBLOCKDEF (v9).
            vals.clear();
            if (!parse_doubles(tok, 1, 2, vals)) {
                return fail("BLOCKDEF header malformed");
            }
            std::string name;
            if (!std::getline(in, name)) {
                return fail("BLOCKDEF missing name");
            }
            ++line_no;
            if (!name.empty() && name.back() == '\r') {
                name.pop_back();
            }
            cur_block = DocBlockDef{};
            cur_block.name = unescape(name);
            cur_block.base = {vals[0], vals[1]};
            in_block = true;
            target_block();
        } else if (key == "ENDBLOCKDEF") {
            if (in_block) {
                doc.block_defs.push_back(std::move(cur_block));
                cur_block = DocBlockDef{};
                in_block = false;
            }
            target_model();
        } else if (key == "CELTSCALE") {
            // v12: per-entity linetype scale; kindcode + ordinal into the top-level vectors.
            std::uint64_t kind = 0;
            std::uint64_t ord = 0;
            double cs = 1.0;
            if (tok.size() != 4 || !to_uint(tok[1], kind) || !to_uint(tok[2], ord) ||
                !to_double(tok[3], cs)) {
                return fail("malformed CELTSCALE");
            }
            if (kind == 0 && ord < doc.lines.size()) {
                doc.lines[ord].celtscale = cs;
            } else if (kind == 1 && ord < doc.circles.size()) {
                doc.circles[ord].celtscale = cs;
            } else if (kind == 2 && ord < doc.arcs.size()) {
                doc.arcs[ord].celtscale = cs;
            } else if (kind == 3 && ord < doc.polylines.size()) {
                doc.polylines[ord].celtscale = cs;
            }
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
