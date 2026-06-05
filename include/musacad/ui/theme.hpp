#pragma once

#include <QString>

namespace musacad::ui {

/// Centralized application styling. The whole frame is themed from this single
/// stylesheet (applied to the QApplication) plus widget object names -- there is
/// no per-widget hardcoded styling elsewhere, so the look is swappable by
/// replacing this one function. Default is a dark, AutoCAD-2023-like palette.
[[nodiscard]] QString dark_theme_qss();

} // namespace musacad::ui
