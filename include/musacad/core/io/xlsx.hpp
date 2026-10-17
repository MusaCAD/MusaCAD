// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace musacad::core::io {

/// The first worksheet of an Excel workbook (.xlsx) as rows of cell text -- what a data link
/// takes from it, as from a CSV file: shared and inline strings as they are, numbers as Excel
/// stores them, TRUE / FALSE for booleans. Empty cells are empty strings; the grid is as wide
/// as its widest row. False, with `error` set, when the file is not a workbook this can read.
/// `sheet` names the worksheet to read instead of the first (an unknown name is an error).
[[nodiscard]] bool read_xlsx(const std::string& path, std::vector<std::vector<std::string>>& rows,
                             std::string& error, std::string_view sheet = {});

/// The same, from the workbook's bytes.
[[nodiscard]] bool parse_xlsx(std::string_view bytes, std::vector<std::vector<std::string>>& rows,
                              std::string& error, std::string_view sheet = {});

/// The names of a workbook's sheets, in its order (empty when it is not one).
[[nodiscard]] std::vector<std::string> xlsx_sheet_names(const std::string& path);

/// The cells of `rows` within an Excel range ("B2:E20", or one cell "C3"), the rest cut
/// away; false (rows untouched) when `range` is not one.
bool crop_to_range(std::vector<std::vector<std::string>>& rows, std::string_view range);

/// One file in a zip archive (stored or deflated), by its name; false when it is not there or
/// cannot be read. Exposed for the tests.
[[nodiscard]] bool zip_entry(std::string_view archive, std::string_view name, std::string& out);

} // namespace musacad::core::io
