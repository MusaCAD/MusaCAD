#include "musacad/core/io/dxf.hpp"

#include <cctype>
#include <charconv>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

#include "musacad/core/math/math.hpp"

namespace musacad::core::io {

namespace {

// --- writing ---------------------------------------------------------------

void code(std::string& s, int c, std::string_view value) {
    s += std::to_string(c);
    s += '\n';
    s += value;
    s += '\n';
}
void code_d(std::string& s, int c, double v) {
    char buf[40];
    const auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v);
    code(s, c, std::string_view(buf, static_cast<std::size_t>(ptr - buf)));
}
void code_i(std::string& s, int c, long v) { code(s, c, std::to_string(v)); }

void emit_header(std::string& s) {
    code(s, 0, "SECTION");
    code(s, 2, "HEADER");
    code(s, 9, "$ACADVER");
    code(s, 1, "AC1015"); // AutoCAD R2000 -- widely compatible
    code(s, 9, "$INSUNITS");
    code_i(s, 70, 0);
    code(s, 0, "ENDSEC");
}
void emit_tables(std::string& s) {
    code(s, 0, "SECTION");
    code(s, 2, "TABLES");
    code(s, 0, "TABLE");
    code(s, 2, "LAYER");
    code_i(s, 70, 1);
    code(s, 0, "LAYER");
    code(s, 2, "0"); // the default layer every entity references
    code_i(s, 70, 0);
    code_i(s, 62, 7);
    code(s, 6, "CONTINUOUS");
    code(s, 0, "ENDTAB");
    code(s, 0, "ENDSEC");
}

} // namespace

std::string serialize_dxf(const Document& doc) {
    std::string s;
    emit_header(s);
    emit_tables(s);

    code(s, 0, "SECTION");
    code(s, 2, "ENTITIES");

    for (const Vec2& p : doc.points) {
        code(s, 0, "POINT");
        code(s, 8, "0");
        code_d(s, 10, p.x);
        code_d(s, 20, p.y);
        code_d(s, 30, 0.0);
    }
    for (const DocLine& l : doc.lines) {
        code(s, 0, "LINE");
        code(s, 8, "0");
        code_d(s, 10, l.a.x);
        code_d(s, 20, l.a.y);
        code_d(s, 30, 0.0);
        code_d(s, 11, l.b.x);
        code_d(s, 21, l.b.y);
        code_d(s, 31, 0.0);
    }
    for (const DocCircle& c : doc.circles) {
        code(s, 0, "CIRCLE");
        code(s, 8, "0");
        code_d(s, 10, c.center.x);
        code_d(s, 20, c.center.y);
        code_d(s, 30, 0.0);
        code_d(s, 40, c.radius);
    }
    for (const DocArc& a : doc.arcs) {
        code(s, 0, "ARC");
        code(s, 8, "0");
        code_d(s, 10, a.center.x);
        code_d(s, 20, a.center.y);
        code_d(s, 30, 0.0);
        code_d(s, 40, a.radius);
        code_d(s, 50, to_degrees(a.start_angle)); // DXF arc angles are degrees, CCW
        code_d(s, 51, to_degrees(a.end_angle));
    }
    for (const DocPolyline& p : doc.polylines) {
        code(s, 0, "LWPOLYLINE");
        code(s, 8, "0");
        code_i(s, 90, static_cast<long>(p.points.size()));
        code_i(s, 70, p.closed ? 1 : 0);
        for (const Vec2& v : p.points) {
            code_d(s, 10, v.x);
            code_d(s, 20, v.y);
        }
    }
    // Splines are not part of the R2000 entity subset we emit; noted in COMMANDS.

    code(s, 0, "ENDSEC");
    code(s, 0, "EOF");
    return s;
}

namespace {

struct Pair {
    int code = 0;
    std::string value;
};

bool parse_int(const std::string& t, int& out) {
    // DXF codes can have surrounding whitespace; trim.
    std::size_t a = 0;
    std::size_t b = t.size();
    while (a < b && std::isspace(static_cast<unsigned char>(t[a]))) {
        ++a;
    }
    while (b > a && std::isspace(static_cast<unsigned char>(t[b - 1]))) {
        --b;
    }
    const auto [ptr, ec] = std::from_chars(t.data() + a, t.data() + b, out);
    return ec == std::errc{} && ptr == t.data() + b;
}

double to_d(const std::string& t) {
    double v = 0.0;
    std::size_t a = 0;
    while (a < t.size() && std::isspace(static_cast<unsigned char>(t[a]))) {
        ++a;
    }
    std::from_chars(t.data() + a, t.data() + t.size(), v);
    return v;
}

// Looks up the first value with the given group code in an entity body.
const std::string* find(const std::vector<Pair>& body, int c) {
    for (const Pair& p : body) {
        if (p.code == c) {
            return &p.value;
        }
    }
    return nullptr;
}

// Single-lookup double accessor (a default when the code is absent).
double getd(const std::vector<Pair>& body, int c, double def = 0.0) {
    const std::string* v = find(body, c);
    return v != nullptr ? to_d(*v) : def;
}

} // namespace

IoResult parse_dxf(const std::string& text, Document& out) {
    // Read the file as (code, value) pairs -- two lines each.
    std::istringstream in(text);
    std::vector<Pair> pairs;
    std::string code_line;
    std::string value_line;
    while (std::getline(in, code_line)) {
        if (!std::getline(in, value_line)) {
            return IoResult::failure("Malformed DXF: dangling group code (truncated file).");
        }
        if (!code_line.empty() && code_line.back() == '\r') {
            code_line.pop_back();
        }
        if (!value_line.empty() && value_line.back() == '\r') {
            value_line.pop_back();
        }
        int gc = 0;
        if (!parse_int(code_line, gc)) {
            return IoResult::failure("Malformed DXF: expected a numeric group code.");
        }
        pairs.push_back(Pair{gc, value_line});
    }
    if (pairs.empty()) {
        return IoResult::failure("Empty or invalid DXF file.");
    }

    Document doc;
    std::map<std::string, int> skipped;
    bool saw_section = false;
    std::string section;
    std::size_t i = 0;
    const std::size_t n = pairs.size();

    const auto build = [&](const std::string& type, const std::vector<Pair>& body) {
        if (type == "LINE") {
            doc.lines.push_back(DocLine{{getd(body, 10), getd(body, 20)},
                                        {getd(body, 11), getd(body, 21)}});
        } else if (type == "CIRCLE") {
            doc.circles.push_back(DocCircle{{getd(body, 10), getd(body, 20)}, getd(body, 40)});
        } else if (type == "ARC") {
            doc.arcs.push_back(DocArc{{getd(body, 10), getd(body, 20)},
                                      getd(body, 40),
                                      to_radians(getd(body, 50)),
                                      to_radians(getd(body, 51))});
        } else if (type == "POINT") {
            doc.points.push_back({getd(body, 10), getd(body, 20)});
        } else if (type == "LWPOLYLINE") {
            DocPolyline pl;
            const std::string* flag = find(body, 70);
            pl.closed = flag != nullptr && (std::stol(*flag) & 1) != 0;
            // Collect 10/20 pairs in order.
            double x = 0.0;
            bool have_x = false;
            for (const Pair& p : body) {
                if (p.code == 10) {
                    x = to_d(p.value);
                    have_x = true;
                } else if (p.code == 20 && have_x) {
                    pl.points.push_back({x, to_d(p.value)});
                    have_x = false;
                }
            }
            doc.polylines.push_back(std::move(pl));
        } else {
            ++skipped[type];
        }
    };

    while (i < n) {
        const Pair& p = pairs[i];
        if (p.code != 0) {
            ++i; // header/table data outside an entity body
            continue;
        }
        if (p.value == "SECTION") {
            saw_section = true;
            const bool has_name = i + 1 < n && pairs[i + 1].code == 2;
            section = has_name ? pairs[i + 1].value : std::string();
            i += has_name ? std::size_t{2} : std::size_t{1};
            continue;
        }
        if (p.value == "ENDSEC") {
            section.clear();
            ++i;
            continue;
        }
        if (p.value == "EOF") {
            break;
        }
        if (section == "ENTITIES") {
            const std::string type = p.value;
            std::vector<Pair> body;
            ++i;
            while (i < n && pairs[i].code != 0) {
                body.push_back(pairs[i]);
                ++i;
            }
            build(type, body);
            continue;
        }
        ++i; // a 0-record outside ENTITIES (e.g. TABLE/LAYER) -- ignore
    }

    if (!saw_section) {
        return IoResult::failure("Not a DXF file (no SECTION found).");
    }

    std::string msg = "Imported " + std::to_string(doc.entity_count()) + " entities";
    if (!skipped.empty()) {
        int total = 0;
        std::string names;
        for (const auto& [name, count] : skipped) {
            total += count;
            if (!names.empty()) {
                names += ", ";
            }
            names += name;
        }
        msg += "; skipped " + std::to_string(total) + " unsupported (" + names + ")";
    }
    msg += ".";
    out = std::move(doc);
    return IoResult::success(msg);
}

IoResult save_dxf(const Document& doc, const std::string& path) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        return IoResult::failure("Cannot write file: " + path);
    }
    const std::string text = serialize_dxf(doc);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!f) {
        return IoResult::failure("Write failed: " + path);
    }
    return IoResult::success("Exported " + std::to_string(doc.entity_count()) + " entities to DXF.");
}

IoResult load_dxf(const std::string& path, Document& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return IoResult::failure("Cannot open file: " + path);
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return parse_dxf(ss.str(), out);
}

} // namespace musacad::core::io
