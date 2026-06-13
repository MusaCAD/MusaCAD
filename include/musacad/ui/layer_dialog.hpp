// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <QDialog>

#include "musacad/core/command.hpp"
#include "musacad/core/properties.hpp"

class QTableWidget;

namespace musacad::ui {

/// The Layer Manager: a dark, modeless table of layers (name / on / frozen /
/// locked / colour / linetype / lineweight) with New / Delete / Set-Current and
/// "assign selection to layer". It never touches the store -- it reads the layer
/// table via a getter (fed from the published snapshot) and issues commands via a
/// submit callback. Refreshes itself from the snapshot on a timer.
class LayerDialog : public QDialog {
    Q_OBJECT
public:
    using LayersGetter = std::function<std::vector<core::Layer>()>;
    using CurrentGetter = std::function<std::uint16_t()>;
    using Submit = std::function<void(core::Command)>;

    LayerDialog(LayersGetter layers, CurrentGetter current, Submit submit,
                QWidget* parent = nullptr);

    /// Rebuilds the table from the current layer list (skips while the user is
    /// editing a cell, and only when the list actually changed).
    void refresh();

    // --- test hooks (drive the dialog as the buttons would) ---
    void test_new_layer();
    void test_delete_row(int row);
    void test_set_current_row(int row);
    void test_set_flag(int row, int column, bool value); // columns per kCol* below
    [[nodiscard]] int row_count() const;

private:
    void rebuild_table(const std::vector<core::Layer>& layers, std::uint16_t current);
    void submit_row(int row);
    [[nodiscard]] core::Layer layer_from_row(int row) const;

    LayersGetter layers_;
    CurrentGetter current_;
    Submit submit_;
    QTableWidget* table_ = nullptr;
    std::vector<core::Layer> shown_;   // last rendered list (change detection)
    std::uint16_t shown_current_ = 0;
    bool building_ = false;            // suppress signal feedback during rebuild
};

} // namespace musacad::ui
