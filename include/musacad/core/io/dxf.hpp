// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

#include <string>

#include "musacad/core/io/document.hpp"
#include "musacad/core/io/io_result.hpp"

namespace musacad::core::io {

// ASCII DXF (AC1015 / AutoCAD R2000 baseline) import/export, scoped to the
// entity families Musa CAD models: LINE, LWPOLYLINE/POLYLINE, CIRCLE, ARC,
// POINT. Unsupported entities are skipped on import and summarized, never fatal.

/// Serializes a document to DXF text (HEADER + TABLES + ENTITIES).
[[nodiscard]] std::string serialize_dxf(const Document& doc);

/// Parses DXF text. Supported entities load into `out`; unsupported ones are
/// counted and named in the result message. Returns failure (out untouched) only
/// on a structurally invalid file.
[[nodiscard]] IoResult parse_dxf(const std::string& text, Document& out);

[[nodiscard]] IoResult save_dxf(const Document& doc, const std::string& path);
[[nodiscard]] IoResult load_dxf(const std::string& path, Document& out);

} // namespace musacad::core::io
