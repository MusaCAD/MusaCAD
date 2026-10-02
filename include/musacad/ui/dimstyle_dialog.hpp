// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <QDialog>

#include "musacad/core/command.hpp"
#include "musacad/core/properties.hpp"

class QLabel;
class QListWidget;
class QPushButton;

namespace musacad::ui {

/// DIMSTYLE's Dimension Style Manager: the drawing's dimension styles, the current one,
/// and Set Current / New / Modify. Every change goes to the engine as a command through
/// `submit` (AddDimStyleCommand, SetDimStyleCommand, SetCurrentDimStyleCommand), so it is
/// saved with the drawing and every dimension in the style follows it at once; the
/// manager keeps its own copy of the table to show what it has done.
class DimStyleManager : public QDialog {
public:
    using Submit = std::function<void(core::Command)>;
    /// `unused`: the styles no dimension, leader or frame uses (the PURGE candidates) --
    /// the ones Delete is offered for.
    DimStyleManager(std::vector<core::DimStyle> styles, std::uint16_t current, std::vector<std::string> unused,
                    Submit submit, QWidget* parent = nullptr);

private:
    void refresh();
    [[nodiscard]] int selected() const;
    void set_current();
    void new_style();
    void modify_style();
    void rename_style();
    void delete_style();
    void update_buttons();

    std::vector<core::DimStyle> styles_;
    std::uint16_t current_ = 0;
    std::vector<std::string> unused_;
    Submit submit_;
    QPushButton* rename_btn_ = nullptr;
    QPushButton* delete_btn_ = nullptr;
    QListWidget* list_ = nullptr;
    QLabel* current_label_ = nullptr;
    QLabel* summary_ = nullptr;
};

/// New / Modify: one style's settings on four tabs -- Lines, Symbols and Arrows, Text,
/// Primary Units -- loaded from `style`; style() is what the author left.
class DimStyleEditor : public QDialog {
public:
    DimStyleEditor(const core::DimStyle& style, const QString& title, QWidget* parent = nullptr);
    [[nodiscard]] core::DimStyle style() const;

private:
    core::DimStyle base_;
    std::function<core::DimStyle()> read_;
};

/// One line about a style: "Text 2.5, arrows 2.5 filled, 0.00 (2 places)".
[[nodiscard]] QString dimstyle_summary(const core::DimStyle& style);

} // namespace musacad::ui
