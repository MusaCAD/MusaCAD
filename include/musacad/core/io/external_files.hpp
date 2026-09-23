// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <string_view>

namespace musacad::core::io {

// What a drawing needs from the filesystem beyond its own file, and whether a sandboxed
// build can reach it. Inside Flatpak the user's files arrive through the document portal,
// which grants exactly the file picked in a dialog; these two checks let the UI see when
// that single file is not enough. Pure string scans: no disk access, no parse.

/// True when a drawing names other files it loads by path: an external reference, or an
/// image kept beside the drawing instead of embedded in it. `dxf` selects the format;
/// otherwise `text` is a native .musa file. A malformed file answers false.
[[nodiscard]] bool references_external_files(std::string_view text, bool dxf);

/// True when `path` is one file granted through the document portal:
/// `<runtime_dir>/doc/<id>/<name>`, or the sandbox's own `/run/flatpak/doc/<id>/<name>`.
/// The directory around such a file shows only that file, and any other name written
/// into it becomes a hidden temporary on the host that the user never sees. A folder
/// granted through the portal (`.../doc/<id>/<folder>/...`) is a real directory: false.
[[nodiscard]] bool is_portal_file_grant(std::string_view path, std::string_view runtime_dir);

} // namespace musacad::core::io
