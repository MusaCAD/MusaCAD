#pragma once

#include <QIcon>
#include <QString>

namespace musacad::ui {

/// Returns a simple, theme-coloured vector glyph for a tool/command `kind`
/// (e.g. "line", "circle", "erase", "undo", "zoom"). Drawn at runtime so the
/// ribbon needs no external image assets. Unknown kinds get a generic glyph.
[[nodiscard]] QIcon make_icon(const QString& kind);

} // namespace musacad::ui
