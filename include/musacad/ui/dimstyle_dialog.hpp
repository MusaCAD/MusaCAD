// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <QDialog>
#include <QWidget>

#include "musacad/core/command.hpp"
#include "musacad/core/properties.hpp"

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace musacad::ui {

/// A sample part dimensioned in a style -- linear, aligned, angular, radius -- drawn by
/// the same dimension geometry and stroke font the drawing uses, so what it shows is
/// what the style will draw: arrowheads, sizes, colours, the text's place and its
/// numbers (decimal places, separator, zero suppression).
class DimStylePreview : public QWidget {
public:
    explicit DimStylePreview(QWidget* parent = nullptr);
    void set_style(const core::DimStyle& style);
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(class QPaintEvent* event) override;

private:
    core::DimStyle style_;
};

/// DIMSTYLE's Dimension Style Manager: the drawing's dimension styles (all, or those in
/// use), a preview of the selected one and a description of it, and Set Current / New /
/// Modify / Rename / Delete (also on the list's context menu). Every change goes to the
/// engine as a command through `submit` (AddDimStyleCommand, SetDimStyleCommand,
/// SetCurrentDimStyleCommand, RenameDimStyleCommand, DeleteDimStyleCommand), so it is
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
    void show_selected();
    void set_current();
    void new_style();
    void modify_style();
    void rename_style();
    void delete_style();
    void update_buttons();
    [[nodiscard]] bool in_use(std::size_t i) const;

    std::vector<core::DimStyle> styles_;
    std::uint16_t current_ = 0;
    std::vector<std::string> unused_;
    Submit submit_;
    std::vector<std::size_t> shown_; ///< the list's rows, as indices into styles_
    QListWidget* list_ = nullptr;
    QComboBox* filter_ = nullptr;
    QLabel* current_label_ = nullptr;
    QLabel* preview_title_ = nullptr;
    DimStylePreview* preview_ = nullptr;
    QLabel* description_ = nullptr;
    QPushButton* current_btn_ = nullptr;
    QPushButton* rename_btn_ = nullptr;
    QPushButton* delete_btn_ = nullptr;
};

/// New / Modify: one style's settings on AutoCAD's tabs -- Lines, Symbols and Arrows,
/// Text, Fit, Primary Units -- loaded from `style`, with the preview beside them
/// following every change; style() is what the author left.
class DimStyleEditor : public QDialog {
public:
    DimStyleEditor(const core::DimStyle& style, const QString& title, QWidget* parent = nullptr);
    [[nodiscard]] core::DimStyle style() const;

private:
    core::DimStyle base_;
    std::function<core::DimStyle()> read_;
};

/// A few lines about a style, for the Manager's description: its text, arrowheads,
/// numbers and where moved text goes.
[[nodiscard]] QString dimstyle_summary(const core::DimStyle& style);

} // namespace musacad::ui
