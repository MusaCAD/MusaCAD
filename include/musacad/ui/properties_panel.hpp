// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <QWidget>

#include "musacad/core/properties.hpp"
#include "musacad/core/properties_palette.hpp"

class QVBoxLayout;

namespace musacad::ui {

/// The Properties palette (PR) panel: a generic renderer of a SelectionSummary.
/// It owns no model state -- it is handed the aggregated summary (computed on the
/// geometry thread) plus the layer table, builds grouped editable rows, and emits
/// a single (PropertyId, value) edit back to the host, which turns it into a
/// SetPropertyCommand. The panel never touches the store. It is fully generic
/// over the field list, so a new type's deep group needs no panel changes.
class PropertiesPanel : public QWidget {
    Q_OBJECT

public:
    explicit PropertiesPanel(QWidget* parent = nullptr);

    /// Host hook: called when the user edits a field. Host attaches a group id and
    /// submits SetPropertyCommand on the geometry queue.
    void set_edit_callback(std::function<void(core::PropertyId, const core::PropertyValue&)> cb) {
        edit_cb_ = std::move(cb);
    }

    /// The font names shown in the PR Font dropdown ("Standard" + the system outline
    /// faces). Set once from the font engine's registry by the host.
    void set_font_names(std::vector<std::string> names) { font_names_ = std::move(names); }

    /// Refresh from the latest selection summary + layer table. Rebuilds the rows
    /// only when something actually changed (cheap no-op otherwise).
    void update_view(const core::SelectionSummary& summary, const std::vector<core::Layer>& layers,
                     std::uint16_t current_layer);

    // --- test hooks (observed-outcome self-tests) ---
    [[nodiscard]] int field_count() const { return static_cast<int>(summary_.fields.size()); }
    [[nodiscard]] bool has_field(core::PropertyId id) const;
    [[nodiscard]] bool field_varies(core::PropertyId id) const;
    [[nodiscard]] core::PropertyValue field_value(core::PropertyId id) const;
    /// Simulate the user committing an edit (drives the real edit_cb_ path).
    void test_commit(core::PropertyId id, const core::PropertyValue& v);
    /// The editor widget for a field (for self-tests, e.g. focus it then send keys).
    [[nodiscard]] QWidget* editor_widget(core::PropertyId id) const;

private:
    void rebuild();
    void emit_edit(core::PropertyId id, const core::PropertyValue& v);

    core::SelectionSummary summary_;
    std::vector<core::Layer> layers_;
    std::uint16_t current_layer_ = 0;
    bool have_view_ = false;
    std::function<void(core::PropertyId, const core::PropertyValue&)> edit_cb_;
    QVBoxLayout* root_ = nullptr;
    QWidget* body_ = nullptr; ///< replaced wholesale on each rebuild
    std::unordered_map<int, QWidget*> editors_; ///< PropertyId -> editor widget (rebuilt)
    std::vector<std::string> font_names_{"Standard"}; ///< PR Font dropdown options
};

} // namespace musacad::ui
