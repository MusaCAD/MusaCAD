// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <string>
#include <string_view>

#include "musacad/core/io/document.hpp"
#include "musacad/core/io/io_result.hpp"

namespace musacad::core::io {

/// The AutoCAD Color Index palette: the colour of ACI 1..255 (white outside it).
[[nodiscard]] Rgb dxf_aci_to_rgb(long aci);

// ASCII DXF (AC1015 / AutoCAD R2000 baseline) import/export, scoped to the
// entity families Musa CAD models: LINE, LWPOLYLINE/POLYLINE, CIRCLE, ARC,
// POINT. Unsupported entities are skipped on import and summarized, never fatal.

/// Serializes a document to DXF text (HEADER + TABLES + ENTITIES).
[[nodiscard]] std::string serialize_dxf(const Document& doc);

/// Parses DXF text. Supported entities load into `out`; unsupported ones are
/// counted and named in the result message. Returns failure (out untouched) only
/// on a structurally invalid file.
[[nodiscard]] IoResult parse_dxf(const std::string& text, Document& out);

/// A layer state as AutoCAD's Layer States Manager exports it (.las): one state, tagged
/// lines like DXF's -- LAYERSTATEDICTIONARY, LAYERSTATE, the name, the restore mask, the
/// description, the current layer, then each layer's name (8), flags (90: 1 off, 2 frozen,
/// 4 locked, 8 plotted, 16 frozen in new viewports), colour (62, and 92 for a true colour),
/// lineweight (370), linetype (6), plot style (2) and transparency (440).
[[nodiscard]] std::string serialize_las(const LayerState& state);
/// Reads one: false, with `error` set, when it is not a layer state file.
[[nodiscard]] bool parse_las(std::string_view text, LayerState& out, std::string& error);

[[nodiscard]] IoResult save_dxf(const Document& doc, const std::string& path);
[[nodiscard]] IoResult load_dxf(const std::string& path, Document& out);

} // namespace musacad::core::io
