// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/command/snap_keywords.hpp"

#include <array>
#include <cctype>

namespace musacad::command {

namespace {
constexpr std::array<SnapKeyword, 14> kKeywords{{
    {"END", "Endpoint", core::SnapType::Endpoint, "of", 1},
    {"MID", "Midpoint", core::SnapType::Midpoint, "of", 2},
    {"CEN", "Center", core::SnapType::Center, "of", 4},
    {"NOD", "Node", core::SnapType::Node, "of", 8},
    {"QUA", "Quadrant", core::SnapType::Quadrant, "of", 16},
    {"INT", "Intersection", core::SnapType::Intersection, "of", 32},
    {"INS", "Insertion", core::SnapType::Insertion, "of", 64},
    {"PER", "Perpendicular", core::SnapType::Perpendicular, "to", 128},
    {"TAN", "Tangent", core::SnapType::Tangent, "to", 256},
    {"NEA", "Nearest", core::SnapType::Nearest, "to", 512},
    {"GCEN", "Geometric Center", core::SnapType::GeometricCenter, "of", 1024},
    {"APP", "Apparent Intersection", core::SnapType::ApparentIntersection, "of", 2048},
    {"EXT", "Extension", core::SnapType::Extension, "of", 4096},
    {"PAR", "Parallel", core::SnapType::Parallel, "to", 8192},
}};

std::string upper(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

// A typed word names a mode by its keyword, by the keyword with more of the name after
// it (ENDP, ENDPOINT, PERP, APPINT), or by the whole name without its spaces. AutoCAD's
// older spellings CENTROID and INSERT are taken too.
const SnapKeyword* keyword_of(const std::string& word) {
    if (word.size() < 3) {
        return nullptr;
    }
    if (word == "CENTROID") {
        return &kKeywords[10];
    }
    if (word == "INSERT") {
        return &kKeywords[6];
    }
    if (word == "APPINT" || word == "APPARENT") {
        return &kKeywords[11];
    }
    for (const SnapKeyword& k : kKeywords) {
        std::string name;
        for (const char c : std::string_view(k.name)) {
            if (c != ' ') {
                name += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
        }
        if (word == k.code || name.compare(0, word.size(), word) == 0) {
            return &k;
        }
    }
    return nullptr;
}
} // namespace

std::span<const SnapKeyword> snap_keywords() noexcept { return kKeywords; }

bool parse_snap_list(std::string_view text, std::uint32_t& mask, std::string* label,
                     std::string* joiner, std::string* unknown) {
    std::string u = upper(text);
    for (char& c : u) {
        if (c == ',' || c == ';') {
            c = ' ';
        }
    }
    std::uint32_t out = 0;
    std::string names;
    std::string join;
    bool any = false;
    std::size_t at = 0;
    while (at < u.size()) {
        while (at < u.size() && std::isspace(static_cast<unsigned char>(u[at]))) {
            ++at;
        }
        std::size_t end = at;
        while (end < u.size() && !std::isspace(static_cast<unsigned char>(u[end]))) {
            ++end;
        }
        if (end == at) {
            break;
        }
        const std::string word = u.substr(at, end - at);
        at = end;
        any = true;
        if (word == "NON" || word == "NONE" || word == "OFF") {
            out = 0;
            names.clear();
            continue;
        }
        if (word == "ALL") {
            for (const SnapKeyword& k : kKeywords) {
                out |= core::snap_bit(k.type);
            }
            names = "All";
            continue;
        }
        const SnapKeyword* k = keyword_of(word);
        if (k == nullptr) {
            if (unknown != nullptr) {
                *unknown = word;
            }
            return false;
        }
        out |= core::snap_bit(k->type);
        names += (names.empty() ? "" : ", ") + std::string(k->name);
        if (join.empty()) {
            join = k->joiner;
        }
    }
    if (!any) {
        return false;
    }
    mask = out;
    if (label != nullptr) {
        *label = names;
    }
    if (joiner != nullptr) {
        *joiner = join;
    }
    return true;
}

std::string snap_list(std::uint32_t mask) {
    std::string out;
    for (const SnapKeyword& k : kKeywords) {
        if ((mask & core::snap_bit(k.type)) != 0) {
            out += (out.empty() ? "" : ", ") + std::string(k.name);
        }
    }
    return out.empty() ? "none" : out;
}

std::string snap_codes(std::uint32_t mask) {
    std::string out;
    for (const SnapKeyword& k : kKeywords) {
        if ((mask & core::snap_bit(k.type)) != 0) {
            std::string code(k.code);
            for (std::size_t i = 1; i < code.size(); ++i) {
                code[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(code[i])));
            }
            out += (out.empty() ? "" : ",") + code;
        }
    }
    return out.empty() ? "None" : out;
}

int osmode_of(std::uint32_t mask) noexcept {
    int v = 0;
    for (const SnapKeyword& k : kKeywords) {
        if ((mask & core::snap_bit(k.type)) != 0) {
            v |= k.osmode;
        }
    }
    return v;
}

std::uint32_t mask_of_osmode(int osmode) noexcept {
    std::uint32_t m = 0;
    for (const SnapKeyword& k : kKeywords) {
        if ((osmode & k.osmode) != 0) {
            m |= core::snap_bit(k.type);
        }
    }
    return m;
}

} // namespace musacad::command
