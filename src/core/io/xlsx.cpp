// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/io/xlsx.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
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

bool read_xlsx(const std::string& path, std::vector<std::vector<std::string>>& rows, std::string& error,
               std::string_view sheet) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        error = "cannot open " + path;
        return false;
    }
    const std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return parse_xlsx(bytes, rows, error, sheet);
}

std::vector<std::string> xlsx_sheet_names(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
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
