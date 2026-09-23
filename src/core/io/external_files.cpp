// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/io/external_files.hpp"

#include <charconv>
#include <string>

namespace musacad::core::io {

namespace {

/// Splits `text` into lines one call at a time (LF or CRLF); false once exhausted.
bool next_line(std::string_view text, std::size_t& pos, std::string_view& line) {
    if (pos >= text.size()) {
        return false;
    }
    const std::size_t nl = text.find('\n', pos);
    const std::size_t end = nl == std::string_view::npos ? text.size() : nl;
    line = text.substr(pos, end - pos);
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    pos = end + 1;
    return true;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
        s.remove_suffix(1);
    }
    return s;
}

/// Native: an xref block carries an XREFPATH record; an image kept by path is an
/// `IMAGEDEF <w> <h> 0` (no base64 chunks) followed by its non-empty source line.
bool native_references(std::string_view text) {
    std::size_t pos = 0;
    std::string_view line;
    while (next_line(text, pos, line)) {
        if (line.starts_with("XREFPATH ")) {
            return true;
        }
        if (line.starts_with("IMAGEDEF ") && line.ends_with(" 0")) {
            std::string_view source;
            if (next_line(text, pos, source) && !source.empty()) {
                return true;
            }
        }
    }
    return false;
}

/// DXF has no embedded rasters, so any IMAGEDEF object names a file; an xref is a BLOCK
/// whose group-70 flags carry bit 4. Group codes and values alternate line by line.
bool dxf_references(std::string_view text) {
    std::size_t pos = 0;
    std::string_view code_line;
    std::string_view value;
    bool in_block_header = false;
    while (next_line(text, pos, code_line) && next_line(text, pos, value)) {
        const std::string_view code = trim(code_line);
        const std::string_view v = trim(value);
        if (code == "0") {
            if (v == "IMAGEDEF") {
                return true;
            }
            in_block_header = v == "BLOCK";
        } else if (in_block_header && code == "70") {
            int flags = 0;
            const auto [end, ec] = std::from_chars(v.data(), v.data() + v.size(), flags);
            if (ec == std::errc{} && end == v.data() + v.size() && (flags & 4) != 0) {
                return true;
            }
        }
    }
    return false;
}

/// `path` is `<root><id>/<name>`: one non-empty id, then one non-empty name, no deeper.
bool single_file_under(std::string_view path, std::string_view root) {
    if (root.empty() || !path.starts_with(root)) {
        return false;
    }
    const std::string_view rest = path.substr(root.size());
    const std::size_t slash = rest.find('/');
    return slash != std::string_view::npos && slash > 0 && slash + 1 < rest.size() &&
           rest.find('/', slash + 1) == std::string_view::npos;
}

} // namespace

bool references_external_files(std::string_view text, bool dxf) {
    return dxf ? dxf_references(text) : native_references(text);
}

bool is_portal_file_grant(std::string_view path, std::string_view runtime_dir) {
    while (!runtime_dir.empty() && runtime_dir.back() == '/') {
        runtime_dir.remove_suffix(1);
    }
    if (!runtime_dir.empty() && single_file_under(path, std::string(runtime_dir) + "/doc/")) {
        return true;
    }
    return single_file_under(path, "/run/flatpak/doc/");
}

} // namespace musacad::core::io
