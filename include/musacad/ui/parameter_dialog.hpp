// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <QDialog>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLineEdit;

namespace musacad::ui {

/// The kind of input a dialog field collects.
enum class FieldType { Number, Integer, Choice, Bool };

/// One declarative field in a command dialog. `group` gates visibility: a field
/// with a non-empty `group` is shown only when the dialog's controller Choice
/// currently equals that group string (so a single dialog can present, e.g., the
/// Rectangular vs Polar parameter sets). An empty `group` is always visible.
struct FieldSpec {
    std::string key;
    std::string label;
    FieldType type = FieldType::Number;
    double number = 0.0;               ///< default for Number / Integer
    std::vector<std::string> choices;  ///< options for Choice
    int choice = 0;                    ///< default index for Choice
    bool boolean = false;              ///< default for Bool
    std::string group;                 ///< visibility group (empty = always shown)
};

/// A declarative description of a command's inputs. `controller_key` names the
/// Choice field whose current value gates the grouped fields.
struct DialogSpec {
    std::string title;
    std::string controller_key;
    std::vector<FieldSpec> fields;
};

/// A reusable modal/modeless dialog built from a DialogSpec. The command layer
/// stays untouched: a dialog simply collects parameters and the caller submits
/// the resulting Command, exactly as the command-line flow would. Emits
/// valuesChanged() on every edit so callers can drive a live preview.
class ParameterDialog : public QDialog {
    Q_OBJECT
public:
    explicit ParameterDialog(DialogSpec spec, QWidget* parent = nullptr);

    [[nodiscard]] double number(const std::string& key) const;
    [[nodiscard]] int integer(const std::string& key) const;
    [[nodiscard]] int choice_index(const std::string& key) const;
    [[nodiscard]] std::string choice_value(const std::string& key) const;
    [[nodiscard]] bool boolean(const std::string& key) const;

    // Programmatic setters (used by previews and the self-test harness).
    void set_number(const std::string& key, double value);
    void set_choice(const std::string& key, int index);
    void set_boolean(const std::string& key, bool value);

signals:
    void valuesChanged();

private:
    void update_visibility();

    struct Row {
        FieldType type = FieldType::Number;
        int form_row = 0;
        QLineEdit* edit = nullptr;
        QComboBox* combo = nullptr;
        QCheckBox* check = nullptr;
        std::string group;
    };

    DialogSpec spec_;
    QFormLayout* form_ = nullptr;
    QComboBox* controller_ = nullptr;
    std::unordered_map<std::string, Row> rows_;
};

} // namespace musacad::ui
