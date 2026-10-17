// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include <QDialog>

#include "musacad/core/command.hpp"
#include "musacad/core/properties.hpp"

class QTableWidget;
class QLineEdit;
class QComboBox;
class QCheckBox;

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

    /// The layers frozen in the current viewport (MSPACE), or nullopt when no viewport is
    /// current -- the "VP Freeze" column follows it.
    using VpFrozenGetter = std::function<std::optional<std::vector<std::uint16_t>>()>;

    /// Per layer: whether an entity is on it (All Used Layers).
    using UsedGetter = std::function<std::vector<std::uint8_t>()>;

    LayerDialog(LayersGetter layers, CurrentGetter current, Submit submit,
                QWidget* parent = nullptr, VpFrozenGetter vp_frozen = {}, UsedGetter used = {});

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
    void rebuild_table(const std::vector<core::Layer>& layers, std::uint16_t current,
                       const std::optional<std::vector<std::uint16_t>>& vp_frozen);
    void submit_row(int row);
    [[nodiscard]] core::Layer layer_from_row(int row) const;
    /// Search for layer: only the rows whose name holds the text (or matches it, with *
    /// and ? wild cards) show.
    void apply_filter();

    LayersGetter layers_;
    CurrentGetter current_;
    Submit submit_;
    VpFrozenGetter vp_frozen_;
    UsedGetter used_;
    QComboBox* filter_ = nullptr;  ///< All, All Used Layers, On, Off, Frozen, Thawed, Locked, Unlocked
    QCheckBox* invert_ = nullptr;  ///< Invert filter
    std::optional<std::vector<std::uint16_t>> shown_vp_; // last rendered VP Freeze state
    QTableWidget* table_ = nullptr;
    QLineEdit* search_ = nullptr;
    std::vector<core::Layer> shown_;   // last rendered list (change detection)
    std::uint16_t shown_current_ = 0;
    bool building_ = false;            // suppress signal feedback during rebuild
};

} // namespace musacad::ui
