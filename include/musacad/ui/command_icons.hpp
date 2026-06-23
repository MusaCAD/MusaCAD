// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <QIcon>
#include <QString>

namespace musacad::ui {

/// Returns a simple, theme-coloured vector glyph for a tool/command `kind`
/// (e.g. "line", "circle", "erase", "undo", "zoom"). Drawn at runtime so the
/// ribbon needs no external image assets. Unknown kinds get a generic glyph.
/// Retained as the placeholder/fallback for ribbon_icon().
[[nodiscard]] QIcon make_icon(const QString& kind);

/// Loads a Musa-authored ribbon SVG by its asset path (e.g.
/// "assets/ribbon/line.svg"), resolved to the compiled Qt resource
/// (":/ribbon/line.svg"). `currentColor` in the SVG is recoloured to the theme's
/// icon grey and the glyph is rasterised crisply (HiDPI-ready) via the qsvg image
/// plugin. Results are cached by path. An empty path, a missing resource, or an
/// invalid SVG falls back to the generic placeholder square (graceful degradation).
[[nodiscard]] QIcon ribbon_icon(const QString& asset_path);

} // namespace musacad::ui
