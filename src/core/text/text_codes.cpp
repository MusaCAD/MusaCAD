// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/text/text_codes.hpp"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <utility>

namespace musacad::core::text {

namespace {

void append_utf8(std::string& out, char32_t cp) {
    if (cp <= 0x7F) {
        out += static_cast<char>(cp);
    } else if (cp <= 0x7FF) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    const char l = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (l >= 'a' && l <= 'f') {
        return 10 + (l - 'a');
    }
    return -1;
}

} // namespace

namespace {
std::mutex g_field_mutex;
FieldContext g_fields;
std::string upper_ascii(std::string_view s) {
    std::string u(s);
    for (char& c : u) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return u;
}
} // namespace

void set_field_context(FieldContext ctx) {
    const std::scoped_lock lock(g_field_mutex);
    g_fields = std::move(ctx);
}

FieldContext field_context() {
    const std::scoped_lock lock(g_field_mutex);
    return g_fields;
}

namespace {
std::tm local_tm(std::time_t t) {
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    return tmv;
}
} // namespace

std::string format_date(std::int64_t when, std::string_view format) {
    static constexpr const char* kMonths[] = {"January", "February", "March",     "April",   "May",      "June",
                                              "July",    "August",   "September", "October", "November", "December"};
    static constexpr const char* kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    const std::tm t = local_tm(static_cast<std::time_t>(when));
    std::string out;
    const auto two = [&out](int v) {
        out += static_cast<char>('0' + (v / 10) % 10);
        out += static_cast<char>('0' + v % 10);
    };
    std::size_t i = 0;
    while (i < format.size()) {
        const char c = format[i];
        std::size_t n = 1;
        while (i + n < format.size() && format[i + n] == c) {
            ++n;
        }
        const int hour12 = t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12;
        switch (c) {
        case 'M':
            if (n >= 4) {
                out += kMonths[t.tm_mon];
            } else if (n == 3) {
                out += std::string(kMonths[t.tm_mon]).substr(0, 3);
            } else if (n == 2) {
                two(t.tm_mon + 1);
            } else {
                out += std::to_string(t.tm_mon + 1);
            }
            break;
        case 'd':
            if (n >= 4) {
                out += kDays[t.tm_wday];
            } else if (n == 3) {
                out += std::string(kDays[t.tm_wday]).substr(0, 3);
            } else if (n == 2) {
                two(t.tm_mday);
            } else {
                out += std::to_string(t.tm_mday);
            }
            break;
        case 'y':
            if (n >= 3) {
                out += std::to_string(t.tm_year + 1900);
            } else {
                two((t.tm_year + 1900) % 100);
            }
            break;
        case 'h':
            n >= 2 ? two(hour12) : (void)(out += std::to_string(hour12));
            break;
        case 'H':
            n >= 2 ? two(t.tm_hour) : (void)(out += std::to_string(t.tm_hour));
            break;
        case 'm':
            n >= 2 ? two(t.tm_min) : (void)(out += std::to_string(t.tm_min));
            break;
        case 's':
            n >= 2 ? two(t.tm_sec) : (void)(out += std::to_string(t.tm_sec));
            break;
        case 't':
            out += n >= 2 ? (t.tm_hour < 12 ? "AM" : "PM") : (t.tm_hour < 12 ? "A" : "P");
            break;
        default:
            out.append(n, c);
            break;
        }
        i += n;
    }
    return out;
}

FieldContext make_field_context(std::string_view document_path) {
    FieldContext fc;
    const std::time_t now = std::time(nullptr);
    fc.now = static_cast<std::int64_t>(now);
    fc.path = std::string(document_path);
    std::tm tmv{};
    // The thread-safe local-time conversion is spelled differently per C library:
    // POSIX has localtime_r(time, tm); the MSVC CRT has localtime_s(tm, time) and no
    // localtime_r at all.
#ifdef _WIN32
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tmv);
    fc.date = buf;
    std::strftime(buf, sizeof(buf), "%H:%M", &tmv);
    fc.time = buf;
    const std::size_t slash = document_path.find_last_of("/\\");
    fc.filename = document_path.empty()
                      ? std::string("Drawing1")
                      : std::string(document_path.substr(slash == std::string_view::npos ? 0 : slash + 1));
    // %<Login>%: the account name. POSIX shells export USER; Windows exports USERNAME
    // (and no USER), so without the fallback the field is always empty there.
    const char* user = std::getenv("USER");
    if (user == nullptr || *user == '\0') {
        user = std::getenv("USERNAME");
    }
    fc.login = user != nullptr ? user : "";
    return fc;
}

namespace {
std::string_view trim_ascii(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) {
        s.remove_suffix(1);
    }
    return s;
}

/// %tc1 UPPERCASE, %tc2 lowercase, %tc3 First capital, %tc4 Title Case.
std::string text_case(std::string s, std::string_view format) {
    if (format.find("%tc1") != std::string_view::npos) {
        return upper_ascii(s);
    }
    const bool lower = format.find("%tc2") != std::string_view::npos;
    const bool first = format.find("%tc3") != std::string_view::npos;
    const bool title = format.find("%tc4") != std::string_view::npos;
    if (!lower && !first && !title) {
        return s;
    }
    bool start = true;
    for (std::size_t k = 0; k < s.size(); ++k) {
        const auto ch = static_cast<unsigned char>(s[k]);
        const bool cap = (first && k == 0) || (title && start);
        s[k] = static_cast<char>(cap ? std::toupper(ch) : (lower || first || title) ? std::tolower(ch) : ch);
        start = std::isspace(ch) != 0;
    }
    return s;
}

/// One field's value: the inside of `%<...>%`, in the short form (`Date`) or AutoCAD's
/// (`\AcVar Date \f "M/d/yyyy"`).
std::string field_value(const FieldContext& fc, std::string_view inner) {
    inner = trim_ascii(inner);
    if (inner.size() > 6 && upper_ascii(inner.substr(0, 6)) == "\\ACVAR") {
        inner = trim_ascii(inner.substr(6));
    }
    std::string_view format;
    if (const std::size_t f = inner.find("\\f"); f != std::string_view::npos) {
        format = trim_ascii(inner.substr(f + 2));
        if (format.size() >= 2 && format.front() == '"' && format.back() == '"') {
            format = format.substr(1, format.size() - 2);
        }
        inner = trim_ascii(inner.substr(0, f));
    }
    const std::string name = upper_ascii(inner);
    const auto date = [&](std::int64_t when, const char* fallback) -> std::string {
        if (when == 0) {
            return "----";
        }
        return format_date(when, format.empty() ? std::string_view(fallback) : format);
    };
    const auto prop = [&](const std::string& v) -> std::string { return v.empty() ? "----" : text_case(v, format); };
    if (name == "DATE") {
        return format.empty() ? fc.date : date(fc.now, "");
    }
    if (name == "TIME") {
        return fc.time;
    }
    if (name == "CREATEDATE") {
        return date(fc.created, "M/d/yyyy");
    }
    if (name == "SAVEDATE") {
        return date(fc.saved, "M/d/yyyy");
    }
    if (name == "FILENAME") {
        std::string v = fc.filename;
        if (format.find("%fn6") != std::string_view::npos && !fc.path.empty()) {
            v = fc.path;
        } else if (format.find("%fn1") != std::string_view::npos) {
            if (const std::size_t dot = v.find_last_of('.'); dot != std::string::npos && dot > 0) {
                v.resize(dot);
            }
        }
        return text_case(v, format);
    }
    if (name == "LOGIN") {
        return text_case(fc.login, format);
    }
    if (name == "TITLE") {
        return prop(fc.title);
    }
    if (name == "SUBJECT") {
        return prop(fc.subject);
    }
    if (name == "AUTHOR") {
        return prop(fc.author);
    }
    if (name == "KEYWORDS") {
        return prop(fc.keywords);
    }
    if (name == "COMMENTS") {
        return prop(fc.comments);
    }
    if (name == "HYPERLINKBASE") {
        return prop(fc.hyperlink_base);
    }
    if (name.rfind("CUSTOMDP.", 0) == 0) {
        const std::string key = name.substr(9);
        for (const auto& [n, v] : fc.custom) {
            if (upper_ascii(n) == key) {
                return prop(v);
            }
        }
    }
    return "####";
}
} // namespace

std::string expand_fields(std::string_view raw) {
    if (raw.find("%<") == std::string_view::npos) {
        return std::string(raw);
    }
    const FieldContext fc = field_context();
    std::string out;
    out.reserve(raw.size());
    std::size_t i = 0;
    while (i < raw.size()) {
        if (raw[i] == '%' && i + 1 < raw.size() && raw[i + 1] == '<') {
            const std::size_t close = raw.find(">%", i + 2);
            if (close != std::string_view::npos) {
                out += field_value(fc, raw.substr(i + 2, close - i - 2));
                i = close + 2;
                continue;
            }
        }
        out += raw[i];
        ++i;
    }
    return out;
}

SubstitutedText substitute_text_codes(std::string_view raw_in) {
    const std::string expanded = expand_fields(raw_in);
    const std::string_view in = expanded;
    SubstitutedText r;
    r.text.reserve(in.size());

    bool over = false;
    bool under = false;
    std::size_t over_start = 0;
    std::size_t under_start = 0;
    const auto toggle = [&](bool& flag, std::size_t& start, std::vector<DecorSpan>& spans) {
        if (flag) {
            if (r.text.size() > start) {
                spans.push_back({start, r.text.size()});
            }
            flag = false;
        } else {
            flag = true;
            start = r.text.size();
        }
    };

    for (std::size_t i = 0; i < in.size();) {
        // AutoCAD %%-codes -------------------------------------------------------
        if (in[i] == '%' && i + 2 < in.size() && in[i + 1] == '%') {
            const char c = in[i + 2];
            const char lc = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lc == 'd') {
                append_utf8(r.text, 0x00B0); // degree
                i += 3;
                continue;
            }
            if (lc == 'p') {
                append_utf8(r.text, 0x00B1); // plus-minus
                i += 3;
                continue;
            }
            if (lc == 'c') {
                append_utf8(r.text, 0x2300); // diameter
                i += 3;
                continue;
            }
            // Hole-callout aliases. The letters are chosen from what is still free:
            // d/p/c/o/u/% and the numeric %%nnn form are all taken, so counterbore,
            // depth ("hole depth") and countersink ("vee") get b/h/v.
            if (lc == 'b') {
                append_utf8(r.text, 0x2334); // counterbore / spotface
                i += 3;
                continue;
            }
            if (lc == 'h') {
                append_utf8(r.text, 0x21A7); // depth
                i += 3;
                continue;
            }
            if (lc == 'v') {
                append_utf8(r.text, 0x2335); // countersink
                i += 3;
                continue;
            }
            if (c == '%') {
                r.text += '%'; // literal percent
                i += 3;
                continue;
            }
            if (lc == 'o') {
                toggle(over, over_start, r.overline);
                i += 3;
                continue;
            }
            if (lc == 'u') {
                toggle(under, under_start, r.underline);
                i += 3;
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(c))) {
                // %%nnn -> Latin-1 character (1..3 decimal digits).
                std::size_t j = i + 2;
                int val = 0;
                int n = 0;
                while (j < in.size() && n < 3 && std::isdigit(static_cast<unsigned char>(in[j]))) {
                    val = val * 10 + (in[j] - '0');
                    ++j;
                    ++n;
                }
                if (val > 0 && val <= 0xFF) {
                    append_utf8(r.text, static_cast<char32_t>(val));
                } else {
                    // Out of range (0 or >255): keep the sequence literal -- never emit a
                    // wrapped glyph or an embedded NUL.
                    r.text.append(in.substr(i, j - i));
                }
                i = j;
                continue;
            }
            // Unknown %%x: keep one literal '%' and re-scan from the next char.
            r.text += in[i];
            ++i;
            continue;
        }
        // Unicode escape \U+XXXX ------------------------------------------------
        // Universal, not MTEXT-only: it is the general escape for any code point the
        // font carries but no %%-alias names (the GD&T characteristics, the material
        // condition modifiers). Restricting it to MTEXT would mean a symbol reachable
        // in a paragraph but not in a single-line TEXT, a leader, or dimension text.
        if (in[i] == '\\' && i + 2 < in.size() &&
            (in[i + 1] == 'U' || in[i + 1] == 'u') && in[i + 2] == '+') {
            std::size_t j = i + 3;
            unsigned cp = 0;
            int n = 0;
            while (j < in.size() && n < 4 && hex_value(in[j]) >= 0) {
                cp = cp * 16u + static_cast<unsigned>(hex_value(in[j]));
                ++j;
                ++n;
            }
            if (n == 4) {
                append_utf8(r.text, static_cast<char32_t>(cp));
                i = j;
                continue;
            }
            r.text += in[i]; // not a valid escape -> literal backslash
            ++i;
            continue;
        }
        r.text += in[i];
        ++i;
    }

    // A toggle left open at end-of-string decorates to the end.
    if (over && r.text.size() > over_start) {
        r.overline.push_back({over_start, r.text.size()});
    }
    if (under && r.text.size() > under_start) {
        r.underline.push_back({under_start, r.text.size()});
    }
    return r;
}

std::string substitute_text(std::string_view in) {
    return substitute_text_codes(in).text;
}

} // namespace musacad::core::text
