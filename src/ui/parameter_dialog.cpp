// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#include "musacad/ui/parameter_dialog.hpp"

#include <cmath>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

namespace musacad::ui {

ParameterDialog::ParameterDialog(DialogSpec spec, QWidget* parent)
    : QDialog(parent), spec_(std::move(spec)) {
    setWindowTitle(QString::fromStdString(spec_.title));
    setModal(false);

    auto* outer = new QVBoxLayout(this);
    form_ = new QFormLayout;
    outer->addLayout(form_);

    int row_index = 0;
    for (const FieldSpec& f : spec_.fields) {
        Row row;
        row.type = f.type;
        row.form_row = row_index;
        row.group = f.group;
        const QString label = QString::fromStdString(f.label);

        switch (f.type) {
        case FieldType::Number:
        case FieldType::Integer: {
            auto* edit = new QLineEdit(this);
            edit->setValidator(new QDoubleValidator(edit));
            edit->setText(QString::number(f.number));
            connect(edit, &QLineEdit::textChanged, this, [this] { emit valuesChanged(); });
            form_->addRow(label, edit);
            row.edit = edit;
            break;
        }
        case FieldType::Choice: {
            auto* combo = new QComboBox(this);
            for (const std::string& c : f.choices) {
                combo->addItem(QString::fromStdString(c));
            }
            combo->setCurrentIndex(f.choice);
            connect(combo, &QComboBox::currentIndexChanged, this, [this] {
                update_visibility();
                emit valuesChanged();
            });
            form_->addRow(label, combo);
            row.combo = combo;
            if (f.key == spec_.controller_key) {
                controller_ = combo;
            }
            break;
        }
        case FieldType::Bool: {
            auto* check = new QCheckBox(this);
            check->setChecked(f.boolean);
            connect(check, &QCheckBox::toggled, this, [this] { emit valuesChanged(); });
            form_->addRow(label, check);
            row.check = check;
            break;
        }
        }
        rows_.emplace(f.key, row);
        ++row_index;
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);

    update_visibility();
}

void ParameterDialog::update_visibility() {
    const QString active = controller_ != nullptr ? controller_->currentText() : QString();
    for (const auto& [key, row] : rows_) {
        const bool visible =
            row.group.empty() || QString::fromStdString(row.group) == active;
        form_->setRowVisible(row.form_row, visible);
    }
}

double ParameterDialog::number(const std::string& key) const {
    const auto it = rows_.find(key);
    if (it == rows_.end() || it->second.edit == nullptr) {
        return 0.0;
    }
    return it->second.edit->text().toDouble();
}

int ParameterDialog::integer(const std::string& key) const {
    return static_cast<int>(std::lround(number(key)));
}

int ParameterDialog::choice_index(const std::string& key) const {
    const auto it = rows_.find(key);
    if (it == rows_.end() || it->second.combo == nullptr) {
        return -1;
    }
    return it->second.combo->currentIndex();
}

std::string ParameterDialog::choice_value(const std::string& key) const {
    const auto it = rows_.find(key);
    if (it == rows_.end() || it->second.combo == nullptr) {
        return {};
    }
    return it->second.combo->currentText().toStdString();
}

bool ParameterDialog::boolean(const std::string& key) const {
    const auto it = rows_.find(key);
    if (it == rows_.end() || it->second.check == nullptr) {
        return false;
    }
    return it->second.check->isChecked();
}

void ParameterDialog::set_number(const std::string& key, double value) {
    const auto it = rows_.find(key);
    if (it != rows_.end() && it->second.edit != nullptr) {
        it->second.edit->setText(QString::number(value));
    }
}

void ParameterDialog::set_choice(const std::string& key, int index) {
    const auto it = rows_.find(key);
    if (it != rows_.end() && it->second.combo != nullptr) {
        it->second.combo->setCurrentIndex(index);
    }
}

void ParameterDialog::set_boolean(const std::string& key, bool value) {
    const auto it = rows_.find(key);
    if (it != rows_.end() && it->second.check != nullptr) {
        it->second.check->setChecked(value);
    }
}

} // namespace musacad::ui
