// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

#include <QString>

class QApplication;

namespace musacad::ui {

/// Centralized application styling. The whole frame is themed from this single
/// stylesheet (applied to the QApplication) plus widget object names -- there is
/// no per-widget hardcoded styling elsewhere, so the look is swappable by
/// replacing this one function. Default is a dark, AutoCAD-2023-like palette.
[[nodiscard]] QString dark_theme_qss();

/// Applies the full dark theme: the Fusion style, a dark QPalette (so surfaces
/// the QSS doesn't reach -- dialogs, message boxes, menus, the file picker --
/// also render dark), the dark color-scheme hint (Qt 6.5+), and the QSS. Single
/// entry point; call once at startup.
void apply_dark_theme(QApplication& app);

} // namespace musacad::ui
