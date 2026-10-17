// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "musacad/core/properties.hpp"

// The small, standalone table value types, in their own header so commands and the
// serializable IR can name them WITHOUT pulling in the whole GeometryStore -- exactly
// the reason core/mtext_block.hpp exists.

namespace musacad::core {

/// A named table style: the PRESENTATION a table resolves through. Cell contents and the
/// column/row sizes are the table's own stored data.
struct TableStyle {
    std::string name;
    double title_height = 5.0;  ///< text height of the title row
    double header_height = 3.5; ///< text height of the header row
    double data_height = 2.5;   ///< text height of every data row
    double margin = 0.4;        ///< cell padding, as a multiple of the row's text height
    std::uint8_t lineweight = 25;
    ElementColor line_color{}; ///< borders + grid (ByLayer by default)
    ElementColor text_color{};
    friend bool operator==(const TableStyle&, const TableStyle&) = default;
};

/// DATALINK: a named link from tables to an outside file of data -- a CSV file (comma-
/// separated values, as a spreadsheet saves them). A linked table takes the file's
/// rows and columns; DATALINKUPDATE reads it again, or writes the table back to it.
struct DataLink {
    std::string name;
    std::string path;
    /// An Excel workbook's sheet by its name ("" the first), and the cells taken from it
    /// ("B2:E20", "" all of them) -- for a CSV file the range only.
    std::string sheet{};
    std::string range{};
    friend bool operator==(const DataLink&, const DataLink&) = default;
};

/// How a cell's text sits in its rectangle.
enum class CellAlign : std::uint8_t { Left = 0, Center = 1, Right = 2 };

/// One table cell. Content is TEXT in the shared char pool -- the decision #8 made for
/// GD&T cells, which keeps the model small and gives cells the existing control-code
/// pass for free.
///
/// MERGES: a cell spanning several columns/rows carries the span; the cells it covers
/// carry `span_cols == 0`, marking them "not drawn, not picked". That keeps the grid
/// row-major and indexable while still allowing merges.
struct TableCell {
    std::uint32_t str_offset = 0;
    std::uint32_t str_len = 0;
    std::uint16_t span_cols = 1; ///< 0 = covered by a merge to its left/above
    std::uint16_t span_rows = 1;
    std::uint8_t align = static_cast<std::uint8_t>(CellAlign::Center);
    friend bool operator==(const TableCell&, const TableCell&) = default;
};

/// A cell as the geometry builder sees it: span/alignment plus the RAW text, so
/// compute_table_geometry needs no access to the store's pools.
struct TableCellView {
    std::string_view text;
    std::uint16_t span_cols = 1;
    std::uint16_t span_rows = 1;
    std::uint8_t align = static_cast<std::uint8_t>(CellAlign::Center);
};

/// Grip index layout for a table.
///
///   0                        -- the insertion point: moves the whole table.
///   1 .. cols-1              -- interior COLUMN boundary i, which sizes column i-1.
///   kTableRowGripBase + j    -- interior ROW boundary j, which sizes row j.
///
/// A base rather than one contiguous run so the two families can never be confused
/// as the column count changes, and so a grip index captured before an edit cannot
/// silently come to mean a different axis after it. Mirrors DimData::kTextGripIndex.
inline constexpr std::uint32_t kTableRowGripBase = 1000;

} // namespace musacad::core
