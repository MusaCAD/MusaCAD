// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/ui/layer_dialog.hpp"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QString>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace musacad::ui {

namespace {
// Column layout of the layer table.
enum Col { kName = 0, kOn, kFrozen, kLocked, kColor, kLinetype, kLineweight, kColCount };

// A small set of standard lineweights, in hundredths of a millimetre.
const std::vector<int>& lineweights() {
    static const std::vector<int> v{0, 9, 13, 18, 25, 30, 50, 70, 100, 200};
    return v;
}
} // namespace

LayerDialog::LayerDialog(LayersGetter layers, CurrentGetter current, Submit submit, QWidget* parent)
    : QDialog(parent), layers_(std::move(layers)), current_(std::move(current)),
      submit_(std::move(submit)) {
    setWindowTitle(QStringLiteral("Layer Properties Manager"));
    setModal(false);
    resize(640, 320);

    auto* outer = new QVBoxLayout(this);

    auto* toolbar = new QHBoxLayout;
    auto* new_btn = new QPushButton(QStringLiteral("New Layer"), this);
    auto* del_btn = new QPushButton(QStringLiteral("Delete"), this);
    auto* cur_btn = new QPushButton(QStringLiteral("Set Current"), this);
    auto* assign_btn = new QPushButton(QStringLiteral("Assign Selection"), this);
    toolbar->addWidget(new_btn);
    toolbar->addWidget(del_btn);
    toolbar->addWidget(cur_btn);
    toolbar->addWidget(assign_btn);
    toolbar->addStretch(1);
    outer->addLayout(toolbar);

    table_ = new QTableWidget(0, kColCount, this);
    table_->setHorizontalHeaderLabels({"Name", "On", "Frozen", "Locked", "Color", "Linetype",
                                       "Lineweight"});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    outer->addWidget(table_, 1);

    connect(new_btn, &QPushButton::clicked, this, &LayerDialog::test_new_layer);
    connect(del_btn, &QPushButton::clicked, this,
            [this] { test_delete_row(table_->currentRow()); });
    connect(cur_btn, &QPushButton::clicked, this,
            [this] { test_set_current_row(table_->currentRow()); });
    connect(assign_btn, &QPushButton::clicked, this, [this] {
        const int row = table_->currentRow();
        if (row >= 0) {
            submit_(core::SetEntityLayerCommand{static_cast<std::uint16_t>(row), 0});
        }
    });
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (!building_ && item != nullptr) {
            submit_row(item->row());
        }
    });

    rebuild_table(layers_(), current_());

    // Track external changes (commands take effect asynchronously via the engine).
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &LayerDialog::refresh);
    timer->start(200);
}

int LayerDialog::row_count() const { return table_->rowCount(); }

void LayerDialog::refresh() {
    const std::vector<core::Layer> layers = layers_();
    const std::uint16_t cur = current_();
    if (layers == shown_ && cur == shown_current_) {
        return; // nothing changed -> don't disturb in-progress edits
    }
    rebuild_table(layers, cur);
}

void LayerDialog::rebuild_table(const std::vector<core::Layer>& layers, std::uint16_t current) {
    building_ = true;
    shown_ = layers;
    shown_current_ = current;
    table_->setRowCount(static_cast<int>(layers.size()));
    for (int row = 0; row < static_cast<int>(layers.size()); ++row) {
        const core::Layer& l = layers[static_cast<std::size_t>(row)];

        // Name (current layer marked with a leading dot, AutoCAD-style).
        auto* name = new QTableWidgetItem(
            QString::fromStdString((row == current ? std::string("• ") : std::string()) +
                                   l.name));
        if (row == 0) {
            name->setFlags(name->flags() & ~Qt::ItemIsEditable); // layer 0 not renamable
        }
        table_->setItem(row, kName, name);

        const auto check_item = [&](bool on) {
            auto* it = new QTableWidgetItem();
            it->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
            return it;
        };
        table_->setItem(row, kOn, check_item(l.on));
        table_->setItem(row, kFrozen, check_item(l.frozen));
        table_->setItem(row, kLocked, check_item(l.locked));

        auto* color = new QPushButton(this);
        color->setText(QStringLiteral("■"));
        color->setStyleSheet(QStringLiteral("color: rgb(%1,%2,%3);")
                                 .arg(l.color.r)
                                 .arg(l.color.g)
                                 .arg(l.color.b));
        connect(color, &QPushButton::clicked, this, [this, row, l] {
            const QColor picked =
                QColorDialog::getColor(QColor(l.color.r, l.color.g, l.color.b), this,
                                       QStringLiteral("Layer Colour"),
                                       QColorDialog::DontUseNativeDialog);
            if (picked.isValid()) {
                core::Layer updated = layer_from_row(row);
                updated.color = {static_cast<std::uint8_t>(picked.red()),
                                 static_cast<std::uint8_t>(picked.green()),
                                 static_cast<std::uint8_t>(picked.blue())};
                submit_(core::SetLayerCommand{static_cast<std::uint16_t>(row), updated});
            }
        });
        table_->setCellWidget(row, kColor, color);

        auto* lt = new QComboBox(this);
        lt->addItems({"Continuous", "Dashed", "Center", "Hidden"});
        lt->setCurrentIndex(static_cast<int>(l.linetype));
        connect(lt, &QComboBox::currentIndexChanged, this, [this, row] {
            if (!building_) {
                submit_row(row);
            }
        });
        table_->setCellWidget(row, kLinetype, lt);

        auto* lw = new QComboBox(this);
        int sel = 0;
        for (std::size_t i = 0; i < lineweights().size(); ++i) {
            const int hund = lineweights()[i];
            lw->addItem(QStringLiteral("%1 mm").arg(hund / 100.0, 0, 'f', 2), hund);
            if (hund == l.lineweight) {
                sel = static_cast<int>(i);
            }
        }
        lw->setCurrentIndex(sel);
        connect(lw, &QComboBox::currentIndexChanged, this, [this, row] {
            if (!building_) {
                submit_row(row);
            }
        });
        table_->setCellWidget(row, kLineweight, lw);
    }
    building_ = false;
}

core::Layer LayerDialog::layer_from_row(int row) const {
    core::Layer l;
    if (row >= 0 && row < static_cast<int>(shown_.size())) {
        l = shown_[static_cast<std::size_t>(row)]; // start from the known state
    }
    if (auto* name = table_->item(row, kName)) {
        QString text = name->text();
        if (text.startsWith(QStringLiteral("• "))) {
            text = text.mid(2);
        }
        if (row != 0) {
            l.name = text.toStdString();
        }
    }
    if (auto* on = table_->item(row, kOn)) {
        l.on = on->checkState() == Qt::Checked;
    }
    if (auto* fr = table_->item(row, kFrozen)) {
        l.frozen = fr->checkState() == Qt::Checked;
    }
    if (auto* lk = table_->item(row, kLocked)) {
        l.locked = lk->checkState() == Qt::Checked;
    }
    if (auto* lt = qobject_cast<QComboBox*>(table_->cellWidget(row, kLinetype))) {
        l.linetype = static_cast<core::Linetype>(lt->currentIndex());
    }
    if (auto* lw = qobject_cast<QComboBox*>(table_->cellWidget(row, kLineweight))) {
        l.lineweight = static_cast<std::uint8_t>(lw->currentData().toInt());
    }
    return l;
}

void LayerDialog::submit_row(int row) {
    if (row < 0) {
        return;
    }
    submit_(core::SetLayerCommand{static_cast<std::uint16_t>(row), layer_from_row(row)});
}

void LayerDialog::test_new_layer() {
    // Unique default name.
    int n = 1;
    QString name;
    const std::vector<core::Layer> layers = layers_();
    const auto taken = [&](const QString& candidate) {
        for (const core::Layer& l : layers) {
            if (QString::fromStdString(l.name) == candidate) {
                return true;
            }
        }
        return false;
    };
    do {
        name = QStringLiteral("Layer%1").arg(n++);
    } while (taken(name));
    core::Layer l;
    l.name = name.toStdString();
    submit_(core::AddLayerCommand{l});
}

void LayerDialog::test_delete_row(int row) {
    if (row > 0) { // never layer 0
        submit_(core::RemoveLayerCommand{static_cast<std::uint16_t>(row)});
    }
}

void LayerDialog::test_set_current_row(int row) {
    if (row >= 0) {
        submit_(core::SetCurrentLayerCommand{static_cast<std::uint16_t>(row)});
    }
}

void LayerDialog::test_set_flag(int row, int column, bool value) {
    if (auto* it = table_->item(row, column)) {
        it->setCheckState(value ? Qt::Checked : Qt::Unchecked); // triggers itemChanged -> submit
    }
}

} // namespace musacad::ui
