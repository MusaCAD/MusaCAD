// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

#include <string>
#include <string_view>

#include "musacad/core/io/document.hpp"
#include "musacad/core/io/io_result.hpp"

namespace musacad::core::io {

// Native ".musa" format: a versioned, line-oriented text format. One record per
// line; doubles are written with std::to_chars (shortest exact round-trip,
// locale-independent), so save -> load reproduces the document byte-for-byte in
// value. Strict parsing -- any malformed record fails without mutating `out`.

/// Serializes a document to the native text form (no disk access).
[[nodiscard]] std::string serialize_native(const Document& doc);

/// Parses the native text form. On success fills `out` and returns ok; on any
/// malformation returns a failure with a descriptive message and leaves `out`
/// untouched.
[[nodiscard]] IoResult parse_native(std::string_view text, Document& out);

/// Writes a document to `path` in the native format.
[[nodiscard]] IoResult save_native(const Document& doc, const std::string& path);

/// Reads a native file from `path` into `out` (unchanged on failure).
[[nodiscard]] IoResult load_native(const std::string& path, Document& out);

} // namespace musacad::core::io
