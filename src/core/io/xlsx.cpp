// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/io/xlsx.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>
#include <utility>

namespace musacad::core::io {

namespace {

// --- DEFLATE (RFC 1951), the canonical-Huffman decoder ----------------------------------

struct BitReader {
    std::string_view in;
    std::size_t pos = 0;
    std::uint32_t buf = 0;
    int count = 0;
    bool error = false;

    std::uint32_t bit() {
        if (count == 0) {
            if (pos >= in.size()) {
                error = true;
                return 0;
            }
            buf = static_cast<unsigned char>(in[pos++]);
            count = 8;
        }
        const std::uint32_t b = buf & 1u;
        buf >>= 1;
        --count;
        return b;
    }
    std::uint32_t bits(int n) {
        std::uint32_t v = 0;
        for (int i = 0; i < n; ++i) {
            v |= bit() << i;
        }
        return v;
    }
};

struct Huffman {
    std::array<std::uint16_t, 16> counts{};
    std::vector<std::uint16_t> symbols;
};

void build(Huffman& h, const std::uint8_t* lengths, std::size_t n) {
    h.counts.fill(0);
    for (std::size_t i = 0; i < n; ++i) {
        ++h.counts[lengths[i]];
    }
    h.counts[0] = 0;
    std::array<std::uint16_t, 17> offsets{};
    for (std::size_t len = 1; len < 16; ++len) {
        offsets[len + 1] = static_cast<std::uint16_t>(offsets[len] + h.counts[len]);
    }
    h.symbols.assign(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        if (lengths[i] != 0) {
            h.symbols[offsets[lengths[i]]++] = static_cast<std::uint16_t>(i);
        }
    }
}

int decode(BitReader& br, const Huffman& h) {
    int code = 0;
    int first = 0;
    int index = 0;
    for (std::size_t len = 1; len < 16; ++len) {
        code |= static_cast<int>(br.bit());
        const int n = h.counts[len];
        if (code - n < first) {
            const auto at = static_cast<std::size_t>(index + (code - first));
            return at < h.symbols.size() ? h.symbols[at] : -1;
        }
        index += n;
        first += n;
        first <<= 1;
        code <<= 1;
        if (br.error) {
            return -1;
        }
    }
    return -1;
}

constexpr std::uint16_t kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                        31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                        2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::uint16_t kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,    65,    97,    129,
                                         193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                         6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool inflate_codes(BitReader& br, const Huffman& lit, const Huffman& dist, std::string& out) {
    for (;;) {
        const int sym = decode(br, lit);
        if (sym < 0 || br.error) {
            return false;
        }
        if (sym < 256) {
            out.push_back(static_cast<char>(sym));
            continue;
        }
        if (sym == 256) {
            return true;
        }
        const auto li = static_cast<std::size_t>(sym - 257);
        if (li >= 29) {
            return false;
        }
        const std::size_t len = kLenBase[li] + br.bits(kLenExtra[li]);
        const int ds = decode(br, dist);
        if (ds < 0 || ds >= 30) {
            return false;
        }
        const auto di = static_cast<std::size_t>(ds);
        const std::size_t back = kDistBase[di] + br.bits(kDistExtra[di]);
        if (back == 0 || back > out.size()) {
            return false;
        }
        for (std::size_t k = 0; k < len; ++k) {
            out.push_back(out[out.size() - back]);
        }
    }
}

bool inflate(std::string_view in, std::string& out) {
    BitReader br{in};
    out.clear();
    std::uint32_t last = 0;
    do {
        last = br.bit();
        const std::uint32_t type = br.bits(2);
        if (br.error) {
            return false;
        }
        if (type == 0) {
            br.count = 0; // to the byte boundary
            if (br.pos + 4 > in.size()) {
                return false;
            }
            const std::size_t len = static_cast<std::size_t>(static_cast<unsigned char>(in[br.pos])) |
                                    (static_cast<std::size_t>(static_cast<unsigned char>(in[br.pos + 1])) << 8);
            br.pos += 4; // LEN and its complement
            if (br.pos + len > in.size()) {
                return false;
            }
            out.append(in.substr(br.pos, len));
            br.pos += len;
        } else if (type == 1) {
            std::array<std::uint8_t, 288> l{};
            std::fill(l.begin(), l.begin() + 144, std::uint8_t{8});
            std::fill(l.begin() + 144, l.begin() + 256, std::uint8_t{9});
            std::fill(l.begin() + 256, l.begin() + 280, std::uint8_t{7});
            std::fill(l.begin() + 280, l.end(), std::uint8_t{8});
            std::array<std::uint8_t, 30> d{};
            d.fill(5);
            Huffman lit;
            Huffman dist;
            build(lit, l.data(), l.size());
            build(dist, d.data(), d.size());
            if (!inflate_codes(br, lit, dist, out)) {
                return false;
            }
        } else if (type == 2) {
            const std::size_t nlen = br.bits(5) + 257;
            const std::size_t ndist = br.bits(5) + 1;
            const std::size_t ncode = br.bits(4) + 4;
            static constexpr std::uint8_t kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            std::array<std::uint8_t, 19> cl{};
            for (std::size_t i = 0; i < ncode; ++i) {
                cl[kOrder[i]] = static_cast<std::uint8_t>(br.bits(3));
            }
            Huffman lencode;
            build(lencode, cl.data(), cl.size());
            std::array<std::uint8_t, 320> lengths{};
            std::size_t i = 0;
            while (i < nlen + ndist) {
                const int sym = decode(br, lencode);
                if (sym < 0 || br.error) {
                    return false;
                }
                if (sym < 16) {
                    lengths[i++] = static_cast<std::uint8_t>(sym);
                    continue;
                }
                std::uint8_t value = 0;
                std::size_t repeat = 0;
                if (sym == 16) {
                    if (i == 0) {
                        return false;
                    }
                    value = lengths[i - 1];
                    repeat = 3 + br.bits(2);
                } else if (sym == 17) {
                    repeat = 3 + br.bits(3);
                } else {
                    repeat = 11 + br.bits(7);
                }
                if (i + repeat > nlen + ndist) {
                    return false;
                }
                for (std::size_t k = 0; k < repeat; ++k) {
                    lengths[i++] = value;
                }
            }
            Huffman lit;
            Huffman dist;
            build(lit, lengths.data(), nlen);
            build(dist, lengths.data() + nlen, ndist);
            if (!inflate_codes(br, lit, dist, out)) {
                return false;
            }
        } else {
            return false;
        }
    } while (last == 0);
    return true;
}

// --- the zip archive ---------------------------------------------------------------------

std::uint32_t le32(std::string_view s, std::size_t at) {
    if (at + 4 > s.size()) {
        return 0;
    }
    return static_cast<std::uint32_t>(static_cast<unsigned char>(s[at])) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(s[at + 1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(s[at + 2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(s[at + 3])) << 24);
}
std::uint16_t le16(std::string_view s, std::size_t at) {
    if (at + 2 > s.size()) {
        return 0;
    }
    return static_cast<std::uint16_t>(static_cast<unsigned char>(s[at]) |
                                      (static_cast<unsigned char>(s[at + 1]) << 8));
}

// --- the workbook's XML --------------------------------------------------------------------

/// The text of an XML node with its entities read (&amp; &lt; &gt; &quot; &apos; &#n; &#xh;).
std::string unescape_xml(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out += s[i];
            continue;
        }
        const std::size_t semi = s.find(';', i);
        if (semi == std::string_view::npos) {
            out += s[i];
            continue;
        }
        const std::string_view ent = s.substr(i + 1, semi - i - 1);
        std::uint32_t cp = 0;
        bool known = true;
        if (ent == "amp") {
            cp = '&';
        } else if (ent == "lt") {
            cp = '<';
        } else if (ent == "gt") {
            cp = '>';
        } else if (ent == "quot") {
            cp = '"';
        } else if (ent == "apos") {
            cp = '\'';
        } else if (!ent.empty() && ent[0] == '#') {
            const bool hex = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X');
            for (std::size_t k = hex ? 2 : 1; k < ent.size(); ++k) {
                const char c = ent[k];
                const int d = c >= '0' && c <= '9'   ? c - '0'
                              : hex && c >= 'a' && c <= 'f' ? c - 'a' + 10
                              : hex && c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                            : -1;
                if (d < 0) {
                    known = false;
                    break;
                }
                cp = cp * (hex ? 16u : 10u) + static_cast<std::uint32_t>(d);
            }
        } else {
            known = false;
        }
        if (!known) {
            out += s[i];
            continue;
        }
        // UTF-8.
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        i = semi;
    }
    return out;
}

/// The value of attribute `name` in a start tag, or empty.
std::string attribute(std::string_view tag, std::string_view name) {
    std::size_t at = 0;
    while ((at = tag.find(name, at)) != std::string_view::npos) {
        const bool boundary = at > 0 && (tag[at - 1] == ' ' || tag[at - 1] == '\t' || tag[at - 1] == '\n' ||
                                         tag[at - 1] == '\r');
        std::size_t eq = at + name.size();
        while (eq < tag.size() && tag[eq] == ' ') {
            ++eq;
        }
        if (boundary && eq < tag.size() && tag[eq] == '=') {
            std::size_t q = eq + 1;
            while (q < tag.size() && tag[q] == ' ') {
                ++q;
            }
            if (q < tag.size() && (tag[q] == '"' || tag[q] == '\'')) {
                const std::size_t end = tag.find(tag[q], q + 1);
                if (end != std::string_view::npos) {
                    return unescape_xml(tag.substr(q + 1, end - q - 1));
                }
            }
        }
        at += name.size();
    }
    return {};
}

/// Every element `<name ...>...</name>` (or `<name .../>`) in `xml`, as (start tag, inner text).
std::vector<std::pair<std::string_view, std::string_view>> elements(std::string_view xml, std::string_view name) {
    std::vector<std::pair<std::string_view, std::string_view>> out;
    const std::string open = "<" + std::string(name);
    const std::string close = "</" + std::string(name) + ">";
    std::size_t at = 0;
    while ((at = xml.find(open, at)) != std::string_view::npos) {
        const std::size_t after = at + open.size();
        if (after >= xml.size() || (xml[after] != ' ' && xml[after] != '>' && xml[after] != '/' && xml[after] != '\t' &&
                                    xml[after] != '\n' && xml[after] != '\r')) {
            at = after; // a longer name (<c in <cols)
            continue;
        }
        const std::size_t gt = xml.find('>', after);
        if (gt == std::string_view::npos) {
            break;
        }
        const std::string_view tag = xml.substr(at, gt - at + 1);
        if (gt > 0 && xml[gt - 1] == '/') {
            out.emplace_back(tag, std::string_view{});
            at = gt + 1;
            continue;
        }
        const std::size_t end = xml.find(close, gt + 1);
        if (end == std::string_view::npos) {
            break;
        }
        out.emplace_back(tag, xml.substr(gt + 1, end - gt - 1));
        at = end + close.size();
    }
    return out;
}

/// The text of every <t> in a string item, the phonetic runs (<rPh>) left out.
std::string string_item(std::string_view si) {
    std::string out;
    std::string kept(si);
    for (std::size_t a = kept.find("<rPh"); a != std::string::npos; a = kept.find("<rPh")) {
        const std::size_t b = kept.find("</rPh>", a);
        kept.erase(a, b == std::string::npos ? std::string::npos : b + 6 - a);
    }
    for (const auto& [tag, text] : elements(kept, "t")) {
        out += unescape_xml(text);
    }
    return out;
}

} // namespace

namespace {
// ---------------------------------------------------------------------------
// Excel number formats
// ---------------------------------------------------------------------------
// A number as a workbook writes it (always with a point), whatever the locale.
std::optional<double> read_number(std::string_view s) {
    double v = 0.0;
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || end != s.data() + s.size()) {
        return std::nullopt;
    }
    return v;
}

std::string strip_zeros(std::string s) {
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') {
            s.pop_back();
        }
        if (!s.empty() && s.back() == '.') {
            s.pop_back();
        }
    }
    return s;
}

// `a` (not negative) with `n` decimals, rounded as Excel shows it: half away from zero, in
// decimal, on the value read to fifteen figures (so 1.005 shows as 1.01).
std::string fixed_digits(double a, int n) {
    char b[64];
    std::snprintf(b, sizeof b, "%.14e", a);
    std::string digits(1, b[0]);
    std::size_t i = 2;
    for (; b[i] != '\0' && b[i] != 'e' && b[i] != 'E'; ++i) {
        digits += b[i];
    }
    int exp10 = b[i] != '\0' ? std::atoi(b + i + 1) : 0;
    const int keep = exp10 + 1 + n; // the figures up to the last decimal shown
    if (keep < 0) {
        return n > 0 ? "0." + std::string(static_cast<std::size_t>(n), '0') : "0";
    }
    if (static_cast<std::size_t>(keep) < digits.size()) {
        bool up = digits[static_cast<std::size_t>(keep)] >= '5';
        digits.resize(static_cast<std::size_t>(keep));
        for (std::size_t k = digits.size(); up && k-- > 0;) {
            if (digits[k] == '9') {
                digits[k] = '0';
            } else {
                ++digits[k];
                up = false;
            }
        }
        if (up) {
            digits.insert(0, "1");
            ++exp10;
        }
    } else {
        digits.append(static_cast<std::size_t>(keep) - digits.size(), '0');
    }
    const int int_count = exp10 + 1;
    std::string ip = "0";
    std::string fp = digits;
    if (int_count > 0) {
        ip = digits.substr(0, static_cast<std::size_t>(int_count));
        fp = digits.substr(static_cast<std::size_t>(int_count));
    } else {
        fp.insert(0, static_cast<std::size_t>(-int_count), '0');
    }
    fp.resize(static_cast<std::size_t>(n), '0');
    return n > 0 ? ip + "." + fp : ip;
}

// General: as many figures as an ordinary cell shows (eleven), scientific beyond them.
std::string general_number(double v) {
    if (v == 0.0) {
        return "0";
    }
    const double a = std::fabs(v);
    std::string sign = v < 0.0 ? "-" : "";
    if (a >= 1e11 || a < 1e-9) {
        char b[64];
        std::snprintf(b, sizeof b, "%.5E", a);
        const std::string s(b);
        const std::size_t e = s.find('E');
        return sign + strip_zeros(s.substr(0, e)) + s.substr(e);
    }
    const int int_digits = a >= 1.0 ? static_cast<int>(std::floor(std::log10(a))) + 1 : 1;
    return sign + strip_zeros(fixed_digits(a, std::max(0, 11 - int_digits)));
}

struct FmtTok {
    enum Kind : std::uint8_t { Lit, Digit, Point, Comma, Percent, Exp, Slash, Date, AmPm, Elapsed, Text, General };
    Kind kind = Lit;
    std::string s;
};

std::vector<std::string> format_sections(std::string_view code) {
    std::vector<std::string> out(1);
    bool quoted = false;
    bool bracket = false;
    for (std::size_t i = 0; i < code.size(); ++i) {
        const char c = code[i];
        if (!quoted && !bracket && c == '\\' && i + 1 < code.size()) {
            out.back() += c;
            out.back() += code[++i];
            continue;
        }
        if (c == '"' && !bracket) {
            quoted = !quoted;
        } else if (!quoted && c == '[') {
            bracket = true;
        } else if (!quoted && c == ']') {
            bracket = false;
        } else if (!quoted && !bracket && c == ';') {
            out.emplace_back();
            continue;
        }
        out.back() += c;
    }
    return out;
}

std::vector<FmtTok> format_tokens(std::string_view s) {
    std::vector<FmtTok> out;
    const auto lit = [&](std::string text) {
        if (!out.empty() && out.back().kind == FmtTok::Lit) {
            out.back().s += text;
        } else {
            out.push_back({FmtTok::Lit, std::move(text)});
        }
    };
    const auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    const auto starts = [&](std::size_t i, std::string_view w) {
        if (i + w.size() > s.size()) {
            return false;
        }
        for (std::size_t k = 0; k < w.size(); ++k) {
            if (lower(s[i + k]) != lower(w[k])) {
                return false;
            }
        }
        return true;
    };
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        const char lc = lower(c);
        if (c == '"') {
            const std::size_t end = s.find('"', i + 1);
            lit(std::string(s.substr(i + 1, end == std::string_view::npos ? std::string_view::npos : end - i - 1)));
            i = end == std::string_view::npos ? s.size() : end;
        } else if (c == '\\' && i + 1 < s.size()) {
            lit(std::string(1, s[++i]));
        } else if (c == '_' && i + 1 < s.size()) {
            ++i;
            lit(" "); // the width of the next character
        } else if (c == '*' && i + 1 < s.size()) {
            ++i; // repeat to fill the cell: nothing in a table cell
        } else if (c == '[') {
            const std::size_t end = s.find(']', i);
            const std::string_view in = s.substr(i + 1, end == std::string_view::npos ? std::string_view::npos : end - i - 1);
            i = end == std::string_view::npos ? s.size() : end;
            if (!in.empty() && in.front() == '$') {
                lit(std::string(in.substr(1, in.find('-') == std::string_view::npos ? std::string_view::npos : in.find('-') - 1)));
            } else if (!in.empty() && std::all_of(in.begin(), in.end(), [&](char ch) { return lower(ch) == lower(in.front()); }) &&
                       (lower(in.front()) == 'h' || lower(in.front()) == 'm' || lower(in.front()) == 's')) {
                out.push_back({FmtTok::Elapsed, std::string(in)});
            } // a colour or a condition: nothing shown
        } else if (starts(i, "general")) {
            out.push_back({FmtTok::General, {}});
            i += 6;
        } else if (starts(i, "am/pm")) {
            out.push_back({FmtTok::AmPm, std::string(s.substr(i, 5))});
            i += 4;
        } else if (starts(i, "a/p")) {
            out.push_back({FmtTok::AmPm, std::string(s.substr(i, 3))});
            i += 2;
        } else if (c == '@') {
            out.push_back({FmtTok::Text, {}});
        } else if ((c == 'E' || c == 'e') && i + 1 < s.size() && (s[i + 1] == '+' || s[i + 1] == '-')) {
            out.push_back({FmtTok::Exp, std::string(s.substr(i, 2))});
            ++i;
        } else if (lc == 'y' || lc == 'm' || lc == 'd' || lc == 'h' || lc == 's') {
            std::size_t j = i;
            while (j < s.size() && lower(s[j]) == lc) {
                ++j;
            }
            out.push_back({FmtTok::Date, std::string(j - i, lc)});
            i = j - 1;
        } else if (c == '0' || c == '#' || c == '?') {
            out.push_back({FmtTok::Digit, std::string(1, c)});
        } else if (c == '.') {
            out.push_back({FmtTok::Point, "."});
        } else if (c == ',') {
            out.push_back({FmtTok::Comma, ","});
        } else if (c == '%') {
            out.push_back({FmtTok::Percent, "%"});
        } else if (c == '/') {
            out.push_back({FmtTok::Slash, "/"});
        } else {
            lit(std::string(1, c));
        }
    }
    return out;
}

// Days since 1970-01-01 to a civil date (H. Hinnant's algorithm).
void civil_from_days(long long z, int& y, int& m, int& d) {
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const long long doe = z - era * 146097;
    const long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const long long mp = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    y = static_cast<int>(yoe + era * 400 + (m <= 2 ? 1 : 0));
}

std::string pad2(long long v) {
    return (v < 10 ? "0" : "") + std::to_string(v);
}

std::string format_date(double v, const std::vector<FmtTok>& toks, bool date1904) {
    static constexpr const char* kMonths[] = {"January", "February", "March",     "April",   "May",      "June",
                                              "July",    "August",   "September", "October", "November", "December"};
    static constexpr const char* kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    if (v < 0.0) {
        return general_number(v); // Excel shows #### for a negative date
    }
    // Fractions of a second shown (ss.00): rounded to them, else to the second.
    int sub_digits = 0;
    for (std::size_t i = 0; i + 1 < toks.size(); ++i) {
        if (toks[i].kind == FmtTok::Date && toks[i].s[0] == 's' && toks[i + 1].kind == FmtTok::Point) {
            for (std::size_t k = i + 2; k < toks.size() && toks[k].kind == FmtTok::Digit; ++k) {
                ++sub_digits;
            }
        }
    }
    const double unit = std::pow(10.0, sub_digits);
    auto day = static_cast<long long>(std::floor(v));
    auto ticks = static_cast<long long>(std::llround((v - static_cast<double>(day)) * 86400.0 * unit));
    if (ticks >= static_cast<long long>(86400.0 * unit)) {
        ++day;
        ticks = 0;
    }
    const long long secs = static_cast<long long>(static_cast<double>(ticks) / unit);
    const long long sub = ticks - static_cast<long long>(static_cast<double>(secs) * unit);
    const long long hour = secs / 3600;
    const long long minute = secs / 60 % 60;
    const long long second = secs % 60;
    int y = 1900;
    int mo = 1;
    int d = 0;
    long long z = 0; // days since 1970-01-01
    if (date1904) {
        z = day - 24107;
    } else if (day == 60) {
        z = -25569 + 60; // Excel's 29 February 1900, which never was
    } else {
        z = day < 60 ? day - 25568 : day - 25569;
    }
    civil_from_days(z, y, mo, d);
    if (!date1904 && day == 60) {
        y = 1900;
        mo = 2;
        d = 29;
    }
    const long long weekday = ((z % 7) + 7 + 4) % 7;
    const bool twelve = std::any_of(toks.begin(), toks.end(), [](const FmtTok& t) { return t.kind == FmtTok::AmPm; });
    std::string out;
    for (std::size_t i = 0; i < toks.size(); ++i) {
        const FmtTok& t = toks[i];
        switch (t.kind) {
        case FmtTok::Date: {
            const char c = t.s[0];
            const std::size_t n = t.s.size();
            if (c == 'y') {
                out += n <= 2 ? pad2(y % 100) : std::to_string(y);
            } else if (c == 'd') {
                out += n == 1 ? std::to_string(d) : n == 2 ? pad2(d) : n == 3 ? std::string(kDays[weekday]).substr(0, 3)
                                                                               : std::string(kDays[weekday]);
            } else if (c == 'h') {
                long long h = hour;
                if (twelve) {
                    h = h % 12 == 0 ? 12 : h % 12;
                }
                out += n == 1 ? std::to_string(h) : pad2(h);
            } else if (c == 's') {
                out += n == 1 ? std::to_string(second) : pad2(second);
            } else { // m: minutes after hours or before seconds, else the month
                bool minutes = false;
                if (n <= 2) {
                    for (std::size_t k = i; k-- > 0;) {
                        if (toks[k].kind == FmtTok::Date || toks[k].kind == FmtTok::Elapsed) {
                            minutes = toks[k].s[0] == 'h' || toks[k].s[0] == 'H';
                            break;
                        }
                    }
                    for (std::size_t k = i + 1; !minutes && k < toks.size(); ++k) {
                        if (toks[k].kind == FmtTok::Date || toks[k].kind == FmtTok::Elapsed) {
                            minutes = toks[k].s[0] == 's' || toks[k].s[0] == 'S';
                            break;
                        }
                    }
                }
                if (minutes) {
                    out += n == 1 ? std::to_string(minute) : pad2(minute);
                } else {
                    const std::string name = kMonths[mo - 1];
                    out += n == 1 ? std::to_string(mo) : n == 2 ? pad2(mo) : n == 3 ? name.substr(0, 3)
                                                                 : n == 4 ? name : name.substr(0, 1);
                }
            }
            break;
        }
        case FmtTok::Elapsed: {
            const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(t.s[0])));
            const long long total = day * 86400 + secs;
            const long long value = c == 'h' ? total / 3600 : c == 'm' ? total / 60 : total;
            const std::string s = std::to_string(value);
            out += t.s.size() >= 2 && s.size() < 2 ? "0" + s : s;
            break;
        }
        case FmtTok::AmPm: {
            const bool pm = hour >= 12;
            const bool lower_case = std::islower(static_cast<unsigned char>(t.s[0])) != 0;
            std::string s = t.s.size() == 3 ? (pm ? "P" : "A") : (pm ? "PM" : "AM");
            if (lower_case) {
                for (char& ch : s) {
                    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                }
            }
            out += s;
            break;
        }
        case FmtTok::Point:
            if (sub_digits > 0) {
                std::string s = std::to_string(sub);
                s.insert(0, static_cast<std::size_t>(sub_digits) > s.size() ? static_cast<std::size_t>(sub_digits) - s.size() : 0, '0');
                out += "." + s;
                while (i + 1 < toks.size() && toks[i + 1].kind == FmtTok::Digit) {
                    ++i;
                }
            } else {
                out += ".";
            }
            break;
        case FmtTok::Lit:
        case FmtTok::Slash:
        case FmtTok::Comma:
            out += t.s;
            break;
        default:
            break;
        }
    }
    return out;
}

// The integer part laid into its placeholders and the text between them, right to left;
// the leftmost placeholder takes the digits beyond them.
std::string fill_integer(const std::string& digits, const std::vector<FmtTok>& pat, bool grouping) {
    std::size_t zeros = 0;
    bool literal_inside = false;
    for (const FmtTok& t : pat) {
        if (t.kind == FmtTok::Digit && t.s[0] == '0') {
            ++zeros;
        }
        literal_inside = literal_inside || t.kind == FmtTok::Lit;
    }
    if (grouping && !literal_inside) {
        std::string s = digits;
        if (s.size() < zeros) {
            s.insert(0, zeros - s.size(), '0');
        }
        std::string out;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (i > 0 && (s.size() - i) % 3 == 0) {
                out += ',';
            }
            out += s[i];
        }
        return out;
    }
    std::size_t leftmost = pat.size();
    for (std::size_t k = 0; k < pat.size(); ++k) {
        if (pat[k].kind == FmtTok::Digit) {
            leftmost = k;
            break;
        }
    }
    std::string rev;
    std::size_t di = digits.size();
    for (std::size_t k = pat.size(); k-- > 0;) {
        const FmtTok& t = pat[k];
        if (t.kind == FmtTok::Lit) {
            rev.append(t.s.rbegin(), t.s.rend());
            continue;
        }
        if (t.kind != FmtTok::Digit) {
            continue;
        }
        if (di > 0) {
            if (k == leftmost) {
                while (di > 0) {
                    rev += digits[--di];
                }
            } else {
                rev += digits[--di];
            }
        } else if (t.s[0] == '0') {
            rev += '0';
        } else if (t.s[0] == '?') {
            rev += ' ';
        }
    }
    return {rev.rbegin(), rev.rend()};
}

std::string format_fraction(double a, const std::vector<FmtTok>& toks, std::size_t slash) {
    // Before the slash: the whole part (when a space or text parts it off), then the numerator.
    std::size_t num_begin = slash;
    while (num_begin > 0 && toks[num_begin - 1].kind == FmtTok::Digit) {
        --num_begin;
    }
    bool whole_part = false;
    for (std::size_t k = 0; k < num_begin; ++k) {
        whole_part = whole_part || toks[k].kind == FmtTok::Digit;
    }
    long long fixed_den = 0;
    int den_digits = 0;
    std::size_t after = slash + 1;
    if (after < toks.size() && toks[after].kind == FmtTok::Lit &&
        std::isdigit(static_cast<unsigned char>(toks[after].s[0])) != 0) {
        fixed_den = std::atoll(toks[after].s.c_str());
    }
    while (after < toks.size() && toks[after].kind == FmtTok::Digit) {
        ++den_digits;
        ++after;
    }
    long long whole = whole_part ? static_cast<long long>(std::floor(a)) : 0;
    const double frac = a - static_cast<double>(whole);
    long long num = 0;
    long long den = 1;
    if (fixed_den > 0) {
        den = fixed_den;
        num = std::llround(frac * static_cast<double>(den));
    } else {
        const long long max_den = static_cast<long long>(std::pow(10.0, std::max(1, den_digits))) - 1;
        double best = 2.0;
        for (long long q = 1; q <= max_den; ++q) {
            const long long p = std::llround(frac * static_cast<double>(q));
            const double err = std::fabs(frac - static_cast<double>(p) / static_cast<double>(q));
            if (err < best - 1e-12) {
                best = err;
                num = p;
                den = q;
            }
        }
    }
    if (whole_part && num == den) {
        ++whole;
        num = 0;
    }
    std::string out;
    for (std::size_t k = 0; k < num_begin; ++k) {
        if (toks[k].kind == FmtTok::Lit) {
            out += toks[k].s;
        } else if (toks[k].kind == FmtTok::Digit && (k + 1 >= num_begin || toks[k + 1].kind != FmtTok::Digit)) {
            out += whole != 0 || num == 0 ? std::to_string(whole) : "";
        }
    }
    if (num != 0) {
        out += std::to_string(num) + "/" + std::to_string(den);
    }
    for (std::size_t k = after; k < toks.size(); ++k) {
        if (toks[k].kind == FmtTok::Lit) {
            out += toks[k].s;
        }
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    out.erase(0, out.find_first_not_of(' ') == std::string::npos ? out.size() : out.find_first_not_of(' '));
    return out.empty() ? "0" : out;
}

std::string format_section(double a, const std::vector<FmtTok>& toks) {
    // a: not negative; the section's own text says how a sign shows.
    bool general = false;
    std::size_t first = toks.size();
    std::size_t last = 0;
    for (std::size_t k = 0; k < toks.size(); ++k) {
        const FmtTok::Kind kind = toks[k].kind;
        general = general || kind == FmtTok::General || kind == FmtTok::Text;
        if (kind == FmtTok::Digit || kind == FmtTok::Point) {
            first = std::min(first, k);
            last = k;
        }
        if (kind == FmtTok::Percent) {
            a *= 100.0;
        }
    }
    if (general || first == toks.size()) {
        std::string out;
        for (const FmtTok& t : toks) {
            if (t.kind == FmtTok::General || t.kind == FmtTok::Text) {
                out += general_number(a);
            } else if (t.kind == FmtTok::Lit || t.kind == FmtTok::Percent || t.kind == FmtTok::Slash ||
                       t.kind == FmtTok::Comma) {
                out += t.s;
            }
        }
        return out;
    }
    for (std::size_t k = first; k <= last; ++k) {
        if (toks[k].kind == FmtTok::Slash) {
            return format_fraction(a, toks, k);
        }
    }
    // Commas straight after the last placeholder divide by a thousand each.
    std::size_t tail = last + 1;
    while (tail < toks.size() && toks[tail].kind == FmtTok::Comma) {
        a /= 1000.0;
        ++tail;
    }
    std::size_t point = toks.size();
    std::size_t exp = toks.size();
    for (std::size_t k = first; k <= last + 1 && k < toks.size(); ++k) {
        if (toks[k].kind == FmtTok::Point && point == toks.size()) {
            point = k;
        }
        if (toks[k].kind == FmtTok::Exp && exp == toks.size()) {
            exp = k;
        }
    }
    // The integer pattern, the decimal placeholders and the exponent's.
    std::vector<FmtTok> ipat;
    bool grouping = false;
    const std::size_t int_end = std::min(point, exp);
    for (std::size_t k = first; k < int_end && k <= last; ++k) {
        if (toks[k].kind == FmtTok::Comma) {
            grouping = true;
        } else if (toks[k].kind == FmtTok::Digit || toks[k].kind == FmtTok::Lit) {
            ipat.push_back(toks[k]);
        }
    }
    std::string dpat;
    for (std::size_t k = point + 1; point < toks.size() && k < toks.size() && k < exp && toks[k].kind == FmtTok::Digit; ++k) {
        dpat += toks[k].s;
    }
    std::string epat;
    for (std::size_t k = exp + 1; exp < toks.size() && k < toks.size() && toks[k].kind == FmtTok::Digit; ++k) {
        epat += toks[k].s;
    }
    const int decimals = static_cast<int>(dpat.size());
    std::string exp_text;
    if (exp < toks.size()) {
        int ip = 0;
        bool hash = false;
        for (const FmtTok& t : ipat) {
            ip += t.kind == FmtTok::Digit ? 1 : 0;
            hash = hash || (t.kind == FmtTok::Digit && t.s[0] == '#');
        }
        ip = std::max(ip, 1);
        int e = a > 0.0 ? static_cast<int>(std::floor(std::log10(a))) : 0;
        e = hash && ip > 1 ? static_cast<int>(std::floor(static_cast<double>(e) / ip)) * ip : e - (ip - 1);
        double mant = a / std::pow(10.0, e);
        if (std::strtod(fixed_digits(mant, decimals).c_str(), nullptr) >= std::pow(10.0, ip)) {
            e += hash && ip > 1 ? ip : 1;
            mant = a / std::pow(10.0, e);
        }
        a = mant;
        std::string ed = std::to_string(std::abs(e));
        const auto min_digits = static_cast<std::size_t>(std::count(epat.begin(), epat.end(), '0'));
        if (ed.size() < min_digits) {
            ed.insert(0, min_digits - ed.size(), '0');
        }
        exp_text = std::string(1, toks[exp].s[0]) + (e < 0 ? "-" : toks[exp].s[1] == '+' ? "+" : "") + ed;
    }
    const std::string num = fixed_digits(a, decimals);
    const std::size_t dot = num.find('.');
    std::string int_digits = num.substr(0, dot);
    if (int_digits == "0") {
        int_digits.clear();
    }
    std::string dec = dot == std::string::npos ? std::string() : num.substr(dot + 1);
    // Trailing zeros shown only where a 0 asks; ? keeps their room.
    for (std::size_t k = dec.size(); k-- > 0;) {
        if (dec[k] != '0' || dpat[k] == '0') {
            break;
        }
        dec[k] = dpat[k] == '?' ? ' ' : '\0';
    }
    dec.erase(std::remove(dec.begin(), dec.end(), '\0'), dec.end());
    std::string out;
    for (std::size_t k = 0; k < first; ++k) {
        if (toks[k].kind == FmtTok::Lit || toks[k].kind == FmtTok::Percent) {
            out += toks[k].s;
        }
    }
    out += fill_integer(int_digits, ipat, grouping);
    if (point < toks.size()) {
        out += "." + dec;
    }
    out += exp_text;
    for (std::size_t k = tail; k < toks.size(); ++k) {
        if (toks[k].kind == FmtTok::Lit || toks[k].kind == FmtTok::Percent) {
            out += toks[k].s;
        }
    }
    return out;
}
} // namespace

std::string_view excel_builtin_format(int id) {
    switch (id) {
    case 1: return "0";
    case 2: return "0.00";
    case 3: return "#,##0";
    case 4: return "#,##0.00";
    case 9: return "0%";
    case 10: return "0.00%";
    case 11: return "0.00E+00";
    case 12: return "# ?/?";
    case 13: return "# ?\?/?\?";
    case 14: return "m/d/yyyy"; // the short date, as Excel shows it in English (United States)
    case 15: return "d-mmm-yy";
    case 16: return "d-mmm";
    case 17: return "mmm-yy";
    case 18: return "h:mm AM/PM";
    case 19: return "h:mm:ss AM/PM";
    case 20: return "h:mm";
    case 21: return "h:mm:ss";
    case 22: return "m/d/yyyy h:mm";
    case 37: return "#,##0 ;(#,##0)";
    case 38: return "#,##0 ;[Red](#,##0)";
    case 39: return "#,##0.00;(#,##0.00)";
    case 40: return "#,##0.00;[Red](#,##0.00)";
    case 45: return "mm:ss";
    case 46: return "[h]:mm:ss";
    case 47: return "mmss.0";
    case 48: return "##0.0E+0";
    case 49: return "@";
    default: return "General";
    }
}

std::string format_excel_number(double value, std::string_view code, bool date1904) {
    const std::vector<std::string> sections = format_sections(code);
    std::size_t which = 0;
    bool own_sign = false; // the section chosen says how the sign shows
    if (value < 0.0 && sections.size() >= 2 && !sections[1].empty()) {
        which = 1;
        own_sign = true;
    } else if (value == 0.0 && sections.size() >= 3 && !sections[2].empty()) {
        which = 2;
    }
    const std::vector<FmtTok> toks = format_tokens(sections[which]);
    const bool date = std::any_of(toks.begin(), toks.end(), [](const FmtTok& t) {
        return t.kind == FmtTok::Date || t.kind == FmtTok::AmPm || t.kind == FmtTok::Elapsed;
    });
    if (date) {
        return format_date(value, toks, date1904);
    }
    std::string s = format_section(std::fabs(value), toks);
    if (value < 0.0 && !own_sign) {
        // A minus, unless the value rounds to nothing but zeros.
        const bool nonzero = std::any_of(s.begin(), s.end(), [](char c) { return c >= '1' && c <= '9'; });
        if (nonzero) {
            s.insert(0, "-");
        }
    }
    return s;
}

bool zip_entry(std::string_view archive, std::string_view name, std::string& out) {
    // The end of central directory record, found from the end (a comment may follow it).
    if (archive.size() < 22) {
        return false;
    }
    std::size_t eocd = std::string_view::npos;
    const std::size_t lowest = archive.size() > 22 + 65535 ? archive.size() - 22 - 65535 : 0;
    for (std::size_t i = archive.size() - 22 + 1; i-- > lowest;) {
        if (le32(archive, i) == 0x06054b50u) {
            eocd = i;
            break;
        }
    }
    if (eocd == std::string_view::npos) {
        return false;
    }
    const std::size_t entries = le16(archive, eocd + 10);
    std::size_t at = le32(archive, eocd + 16);
    for (std::size_t e = 0; e < entries; ++e) {
        if (le32(archive, at) != 0x02014b50u) {
            return false;
        }
        const std::uint16_t method = le16(archive, at + 10);
        const std::uint32_t csize = le32(archive, at + 20);
        const std::uint16_t nlen = le16(archive, at + 28);
        const std::uint16_t xlen = le16(archive, at + 30);
        const std::uint16_t clen = le16(archive, at + 32);
        const std::uint32_t local = le32(archive, at + 42);
        if (at + 46 + static_cast<std::size_t>(nlen) > archive.size()) {
            return false;
        }
        const std::string_view entry_name = archive.substr(at + 46, nlen);
        at += 46 + static_cast<std::size_t>(nlen) + static_cast<std::size_t>(xlen) + static_cast<std::size_t>(clen);
        if (entry_name != name) {
            continue;
        }
        if (le32(archive, local) != 0x04034b50u) {
            return false;
        }
        const std::size_t data = static_cast<std::size_t>(local) + 30 + static_cast<std::size_t>(le16(archive, local + 26)) +
                                 static_cast<std::size_t>(le16(archive, local + 28));
        if (data + csize > archive.size()) {
            return false;
        }
        const std::string_view raw = archive.substr(data, csize);
        if (method == 0) {
            out.assign(raw);
            return true;
        }
        return method == 8 && inflate(raw, out);
    }
    return false;
}

bool parse_xlsx(std::string_view bytes, std::vector<std::vector<std::string>>& rows, std::string& error,
                std::string_view sheet_name) {
    rows.clear();
    std::string workbook;
    if (!zip_entry(bytes, "xl/workbook.xml", workbook)) {
        error = "not an Excel workbook (.xlsx)";
        return false;
    }
    // The sheet asked for (else the first): its relationship id, then the part it names.
    std::string sheet_path = "xl/worksheets/sheet1.xml";
    const auto sheets = elements(workbook, "sheet");
    std::size_t which = 0;
    if (!sheet_name.empty()) {
        which = sheets.size();
        for (std::size_t i = 0; i < sheets.size(); ++i) {
            if (unescape_xml(attribute(sheets[i].first, "name")) == sheet_name) {
                which = i;
            }
        }
        if (which == sheets.size()) {
            error = "the workbook has no sheet \"" + std::string(sheet_name) + "\"";
            return false;
        }
    }
    std::string rels;
    if (!sheets.empty() && zip_entry(bytes, "xl/_rels/workbook.xml.rels", rels)) {
        std::string id = attribute(sheets[which].first, "r:id");
        for (const auto& [tag, inner] : elements(rels, "Relationship")) {
            if (attribute(tag, "Id") == id) {
                std::string target = attribute(tag, "Target");
                sheet_path = !target.empty() && target.front() == '/' ? target.substr(1) : "xl/" + target;
            }
        }
    }
    std::string sheet;
    if (!zip_entry(bytes, sheet_path, sheet)) {
        error = "the workbook's first sheet cannot be read";
        return false;
    }
    // Each cell style's number format (xl/styles.xml), and whether the dates count from 1904.
    std::vector<std::string> xf_formats;
    if (std::string styles; zip_entry(bytes, "xl/styles.xml", styles)) {
        std::vector<std::pair<int, std::string>> custom;
        for (const auto& [tag, inner] : elements(styles, "numFmt")) {
            custom.emplace_back(std::atoi(attribute(tag, "numFmtId").c_str()), attribute(tag, "formatCode"));
        }
        if (const auto xfs = elements(styles, "cellXfs"); !xfs.empty()) {
            for (const auto& [tag, inner] : elements(xfs.front().second, "xf")) {
                const int id = std::atoi(attribute(tag, "numFmtId").c_str());
                std::string code(excel_builtin_format(id));
                for (const auto& [cid, ccode] : custom) {
                    if (cid == id) {
                        code = ccode;
                    }
                }
                xf_formats.push_back(std::move(code));
            }
        }
    }
    bool date1904 = false;
    for (const auto& [tag, inner] : elements(workbook, "workbookPr")) {
        const std::string v = attribute(tag, "date1904");
        date1904 = v == "1" || v == "true";
    }
    std::vector<std::string> shared;
    std::string sst;
    if (zip_entry(bytes, "xl/sharedStrings.xml", sst)) {
        for (const auto& [tag, inner] : elements(sst, "si")) {
            shared.push_back(string_item(inner));
        }
    }
    std::size_t width = 0;
    std::size_t next_row = 0;
    for (const auto& [row_tag, row_inner] : elements(sheet, "row")) {
        std::size_t r = next_row;
        if (const std::string ref = attribute(row_tag, "r"); !ref.empty()) {
            std::size_t n = 0;
            for (const char ch : ref) {
                n = ch >= '0' && ch <= '9' ? n * 10 + static_cast<std::size_t>(ch - '0') : n;
            }
            r = n > 0 ? n - 1 : r;
        }
        next_row = r + 1;
        if (rows.size() <= r) {
            rows.resize(r + 1);
        }
        std::size_t next_col = 0;
        for (const auto& [cell_tag, cell_inner] : elements(row_inner, "c")) {
            std::size_t c = next_col;
            if (const std::string ref = attribute(cell_tag, "r"); !ref.empty()) {
                std::size_t col = 0;
                for (const char ch : ref) {
                    if (ch >= 'A' && ch <= 'Z') {
                        col = col * 26 + static_cast<std::size_t>(ch - 'A' + 1);
                    } else if (ch >= 'a' && ch <= 'z') {
                        col = col * 26 + static_cast<std::size_t>(ch - 'a' + 1);
                    }
                }
                c = col > 0 ? col - 1 : c;
            }
            next_col = c + 1;
            const std::string type = attribute(cell_tag, "t");
            std::string value;
            if (type == "inlineStr") {
                const auto is = elements(cell_inner, "is");
                value = is.empty() ? std::string() : string_item(is.front().second);
            } else {
                const auto v = elements(cell_inner, "v");
                value = v.empty() ? std::string() : unescape_xml(v.front().second);
                if (type == "s") {
                    std::size_t k = 0;
                    bool digits = !value.empty();
                    for (const char ch : value) {
                        digits = digits && ch >= '0' && ch <= '9';
                        k = digits ? k * 10 + static_cast<std::size_t>(ch - '0') : k;
                    }
                    value = digits && k < shared.size() ? shared[k] : std::string();
                } else if (type == "b") {
                    value = value == "1" ? "TRUE" : "FALSE";
                } else if ((type.empty() || type == "n") && !value.empty()) {
                    // A number as its cell shows it, in the cell's number format.
                    std::string_view code = "General";
                    if (const std::string st = attribute(cell_tag, "s"); !st.empty()) {
                        const auto k = static_cast<std::size_t>(std::atoi(st.c_str()));
                        code = k < xf_formats.size() ? std::string_view(xf_formats[k]) : code;
                    }
                    if (const std::optional<double> num = read_number(value)) {
                        value = format_excel_number(*num, code, date1904);
                    }
                }
            }
            std::vector<std::string>& row = rows[r];
            if (row.size() <= c) {
                row.resize(c + 1);
            }
            row[c] = std::move(value);
            width = std::max(width, row.size());
        }
    }
    for (std::vector<std::string>& row : rows) {
        row.resize(width);
    }
    while (!rows.empty() && std::all_of(rows.back().begin(), rows.back().end(), [](const std::string& s) { return s.empty(); })) {
        rows.pop_back();
    }
    return true;
}

namespace {
/// A file's bytes; false when it cannot be read.
bool read_file(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        return false;
    }
    const std::streamoff size = f.tellg();
    if (size < 0) {
        return false;
    }
    out.assign(static_cast<std::size_t>(size), '\0');
    f.seekg(0);
    f.read(out.data(), static_cast<std::streamsize>(size));
    return static_cast<bool>(f);
}
} // namespace

bool read_xlsx(const std::string& path, std::vector<std::vector<std::string>>& rows, std::string& error,
               std::string_view sheet) {
    std::string bytes;
    if (!read_file(path, bytes)) {
        error = "cannot open " + path;
        return false;
    }
    return parse_xlsx(bytes, rows, error, sheet);
}

std::vector<std::string> xlsx_sheet_names(const std::string& path) {
    std::string bytes;
    if (!read_file(path, bytes)) {
        return {};
    }
    std::string workbook;
    std::vector<std::string> out;
    if (!zip_entry(bytes, "xl/workbook.xml", workbook)) {
        return out;
    }
    for (const auto& [tag, inner] : elements(workbook, "sheet")) {
        out.push_back(unescape_xml(attribute(tag, "name")));
    }
    return out;
}

bool crop_to_range(std::vector<std::vector<std::string>>& rows, std::string_view range) {
    // A cell reference: its column letters and row number, both from 1.
    const auto cell = [](std::string_view ref, std::size_t& col, std::size_t& row) {
        col = 0;
        row = 0;
        std::size_t i = 0;
        while (i < ref.size() && ref[i] == '$') {
            ++i;
        }
        for (; i < ref.size() && std::isalpha(static_cast<unsigned char>(ref[i])); ++i) {
            col = col * 26 + static_cast<std::size_t>(std::toupper(static_cast<unsigned char>(ref[i])) - 'A' + 1);
        }
        while (i < ref.size() && ref[i] == '$') {
            ++i;
        }
        for (; i < ref.size() && std::isdigit(static_cast<unsigned char>(ref[i])); ++i) {
            row = row * 10 + static_cast<std::size_t>(ref[i] - '0');
        }
        return i == ref.size() && col > 0 && row > 0;
    };
    const std::size_t colon = range.find(':');
    std::size_t c0 = 0;
    std::size_t r0 = 0;
    std::size_t c1 = 0;
    std::size_t r1 = 0;
    if (!cell(range.substr(0, colon), c0, r0)) {
        return false;
    }
    if (colon == std::string_view::npos) {
        c1 = c0;
        r1 = r0;
    } else if (!cell(range.substr(colon + 1), c1, r1)) {
        return false;
    }
    if (c1 < c0) {
        std::swap(c0, c1);
    }
    if (r1 < r0) {
        std::swap(r0, r1);
    }
    // A range as given, its empty cells too -- but one of a whole sheet (a million rows)
    // goes no further than the data does.
    if ((r1 - r0 + 1) * (c1 - c0 + 1) > 1000000) {
        std::size_t width = 0;
        for (const std::vector<std::string>& line : rows) {
            width = std::max(width, line.size());
        }
        r1 = std::min(r1, std::max(rows.size(), r0));
        c1 = std::min(c1, std::max(width, c0));
    }
    std::vector<std::vector<std::string>> out;
    for (std::size_t r = r0 - 1; r < r1; ++r) {
        std::vector<std::string> line;
        for (std::size_t c = c0 - 1; c < c1; ++c) {
            line.push_back(r < rows.size() && c < rows[r].size() ? rows[r][c] : std::string());
        }
        out.push_back(std::move(line));
    }
    rows = std::move(out);
    return true;
}

} // namespace musacad::core::io
