// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/ui/layer_dialog.hpp"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QPushButton>
#include <QSpinBox>
#include <QRegularExpression>
#include <QString>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace musacad::ui {

namespace {
// Column layout of the layer table.
// The two VPLAYER columns come last, as in AutoCAD's manager: "New VP Freeze" is the
// layer's default for viewports created later; "VP Freeze" is the current viewport's.
enum Col {
    kName = 0,
    kOn,
    kFrozen,
    kLocked,
    kColor,
    kLinetype,
    kLineweight,
    kPlot,
    kNewVpFreeze,
    kVpFreeze,
    kDescription,
    kTransparency, // 0..90 percent
    kColCount
};

constexpr int kBuiltInFilters = 8; // All ... Unlocked; the group filters follow

// A small set of standard lineweights, in hundredths of a millimetre.
const std::vector<int>& lineweights() {
    static const std::vector<int> v{0, 9, 13, 18, 25, 30, 50, 70, 100, 200};
    return v;
}
} // namespace

LayerDialog::LayerDialog(LayersGetter layers, CurrentGetter current, Submit submit, QWidget* parent,
                         VpFrozenGetter vp_frozen, UsedGetter used, FiltersGetter filters)
    : QDialog(parent), layers_(std::move(layers)), current_(std::move(current)),
      submit_(std::move(submit)), vp_frozen_(std::move(vp_frozen)), used_(std::move(used)),
      filters_(std::move(filters)) {
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
    // AutoCAD's filters: the layers shown by what they are, and Invert filter.
    filter_ = new QComboBox(this);
    filter_->setObjectName(QStringLiteral("LayerFilter"));
    filter_->addItems({QStringLiteral("All"), QStringLiteral("All Used Layers"), QStringLiteral("Layers On"),
                       QStringLiteral("Layers Off"), QStringLiteral("Frozen"), QStringLiteral("Thawed"),
                       QStringLiteral("Locked"), QStringLiteral("Unlocked")});
    filter_->setToolTip(QStringLiteral("Filter: the layers shown."));
    toolbar->addWidget(filter_);
    invert_ = new QCheckBox(QStringLiteral("Invert filter"), this);
    invert_->setObjectName(QStringLiteral("LayerFilterInvert"));
    toolbar->addWidget(invert_);
    // Group filters of the drawing's own: the selected layers, named, shown together.
    auto* new_filter = new QPushButton(QStringLiteral("New Group Filter"), this);
    new_filter->setObjectName(QStringLiteral("LayerNewGroupFilter"));
    new_filter->setToolTip(QStringLiteral("A filter of the selected layers, kept in the drawing."));
    auto* del_filter = new QPushButton(QStringLiteral("Delete Filter"), this);
    del_filter->setObjectName(QStringLiteral("LayerDeleteFilter"));
    toolbar->addWidget(new_filter);
    toolbar->addWidget(del_filter);
    connect(new_filter, &QPushButton::clicked, this, [this] { new_group_filter(); });
    connect(del_filter, &QPushButton::clicked, this, [this] { delete_group_filter(); });
    connect(filter_, &QComboBox::currentIndexChanged, this, [this] { apply_filter(); });
    connect(invert_, &QCheckBox::toggled, this, [this] { apply_filter(); });
    search_ = new QLineEdit(this);
    search_->setObjectName(QStringLiteral("LayerSearch"));
    search_->setPlaceholderText(QStringLiteral("Search for layer"));
    search_->setClearButtonEnabled(true);
    search_->setToolTip(QStringLiteral("Show the layers whose name holds this text; * and ? are wild cards."));
    toolbar->addWidget(search_);
    connect(search_, &QLineEdit::textChanged, this, [this] { apply_filter(); });
    outer->addLayout(toolbar);

    table_ = new QTableWidget(0, kColCount, this);
    table_->setHorizontalHeaderLabels({"Name", "On", "Frozen", "Locked", "Color", "Linetype",
                                       "Lineweight", "Plot", "New VP Freeze", "VP Freeze", "Description", "Transparency"});
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(kName, QHeaderView::Stretch);
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
        if (building_ || item == nullptr) {
            return;
        }
        if (item->column() == kVpFreeze) {
            // VP Freeze: the current viewport only (VPLAYER Freeze/Thaw, Current).
            const int row = item->row();
            if (row >= 0 && row < static_cast<int>(shown_.size())) {
                core::SetViewportLayerFreezeCommand c;
                c.op = item->checkState() == Qt::Checked
                           ? core::SetViewportLayerFreezeCommand::Op::Freeze
                           : core::SetViewportLayerFreezeCommand::Op::Thaw;
                c.target = core::SetViewportLayerFreezeCommand::Target::Current;
                c.layer_names = {shown_[static_cast<std::size_t>(row)].name};
                submit_(c);
            }
            return;
        }
        submit_row(item->row());
    });

    rebuild_table(layers_(), current_(), vp_frozen_ ? vp_frozen_() : std::nullopt);
    refill_filters();

    // Track external changes (commands take effect asynchronously via the engine).
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &LayerDialog::refresh);
    timer->start(200);
}

int LayerDialog::row_count() const { return table_->rowCount(); }

void LayerDialog::refill_filters() {
    const std::vector<core::LayerGroupFilter> now = filters_ ? filters_() : std::vector<core::LayerGroupFilter>{};
    if (now == shown_filters_) {
        return;
    }
    const QString keep = filter_->currentText();
    const QSignalBlocker block(filter_);
    while (filter_->count() > kBuiltInFilters) {
        filter_->removeItem(filter_->count() - 1);
    }
    for (const core::LayerGroupFilter& f : now) {
        filter_->addItem(QString::fromStdString(f.name));
    }
    shown_filters_ = now;
    const int at = filter_->findText(keep);
    filter_->setCurrentIndex(at >= 0 ? at : 0);
    apply_filter();
}

void LayerDialog::new_group_filter() {
    // The layers of the selected rows (or the current one).
    std::vector<std::string> members;
    std::vector<int> rows;
    for (const QModelIndex& ix : table_->selectionModel()->selectedIndexes()) {
        if (std::find(rows.begin(), rows.end(), ix.row()) == rows.end()) {
            rows.push_back(ix.row());
        }
    }
    if (rows.empty() && table_->currentRow() >= 0) {
        rows.push_back(table_->currentRow());
    }
    std::sort(rows.begin(), rows.end());
    for (const int r : rows) {
        if (r >= 0 && static_cast<std::size_t>(r) < shown_.size()) {
            members.push_back(shown_[static_cast<std::size_t>(r)].name);
        }
    }
    if (members.empty()) {
        QMessageBox::information(this, QStringLiteral("New Group Filter"),
                                 QStringLiteral("Select the layers the filter is to show."));
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("New Group Filter"), QStringLiteral("Filter name:"),
                                               QLineEdit::Normal,
                                               QStringLiteral("Group Filter%1").arg(shown_filters_.size() + 1), &ok)
                             .trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    std::vector<core::LayerGroupFilter> next = shown_filters_;
    const auto same = std::find_if(next.begin(), next.end(), [&](const core::LayerGroupFilter& f) {
        return QString::fromStdString(f.name).compare(name, Qt::CaseInsensitive) == 0;
    });
    if (same != next.end()) {
        same->layers = members; // the same name: its layers replaced
    } else {
        next.push_back(core::LayerGroupFilter{name.toStdString(), members});
    }
    submit_(core::SetLayerFiltersCommand{next});
    shown_filters_ = next;
    {
        const QSignalBlocker block(filter_);
        while (filter_->count() > kBuiltInFilters) {
            filter_->removeItem(filter_->count() - 1);
        }
        for (const core::LayerGroupFilter& f : next) {
            filter_->addItem(QString::fromStdString(f.name));
        }
        filter_->setCurrentText(name);
    }
    apply_filter();
}

void LayerDialog::delete_group_filter() {
    const int which = filter_->currentIndex() - kBuiltInFilters;
    if (which < 0 || static_cast<std::size_t>(which) >= shown_filters_.size()) {
        QMessageBox::information(this, QStringLiteral("Delete Filter"),
                                 QStringLiteral("The built-in filters cannot be deleted; pick a group filter."));
        return;
    }
    std::vector<core::LayerGroupFilter> next = shown_filters_;
    next.erase(next.begin() + which);
    submit_(core::SetLayerFiltersCommand{next});
    shown_filters_ = next;
    {
        const QSignalBlocker block(filter_);
        filter_->removeItem(filter_->currentIndex());
        filter_->setCurrentIndex(0);
    }
    apply_filter();
}

void LayerDialog::apply_filter() {
    const QString f = search_ != nullptr ? search_->text().trimmed() : QString();
    const bool wild = f.contains(QLatin1Char('*')) || f.contains(QLatin1Char('?'));
    const QRegularExpression re(wild ? QRegularExpression::wildcardToRegularExpression(f) : QString(),
                                QRegularExpression::CaseInsensitiveOption);
    const std::vector<std::uint8_t> used = used_ ? used_() : std::vector<std::uint8_t>{};
    const int which = filter_ != nullptr ? filter_->currentIndex() : 0;
    const bool invert = invert_ != nullptr && invert_->isChecked() && which != 0;
    for (int row = 0; row < table_->rowCount(); ++row) {
        const auto i = static_cast<std::size_t>(row);
        const QString name = i < shown_.size() ? QString::fromStdString(shown_[i].name) : QString();
        bool pass = true;
        if (i < shown_.size()) {
            const core::Layer& l = shown_[i];
            switch (which) {
            case 1: // All Used Layers (layer 0 and the current layer count as used)
                pass = (i < used.size() && used[i] != 0) || i == 0 || i == shown_current_;
                break;
            case 2:
                pass = l.on;
                break;
            case 3:
                pass = !l.on;
                break;
            case 4:
                pass = l.frozen;
                break;
            case 5:
                pass = !l.frozen;
                break;
            case 6:
                pass = l.locked;
                break;
            case 7:
                pass = !l.locked;
                break;
            default:
                if (which >= kBuiltInFilters && static_cast<std::size_t>(which - kBuiltInFilters) < shown_filters_.size()) {
                    const core::LayerGroupFilter& g = shown_filters_[static_cast<std::size_t>(which - kBuiltInFilters)];
                    pass = std::find(g.layers.begin(), g.layers.end(), l.name) != g.layers.end();
                }
                break;
            }
        }
        const bool found = f.isEmpty() || (wild ? re.match(name).hasMatch() : name.contains(f, Qt::CaseInsensitive));
        table_->setRowHidden(row, !(found && (pass != invert)));
    }
}

void LayerDialog::refresh() {
    refill_filters(); // another drawing's, or an undo's
    const std::vector<core::Layer> layers = layers_();
    const std::uint16_t cur = current_();
    const std::optional<std::vector<std::uint16_t>> vp = vp_frozen_ ? vp_frozen_() : std::nullopt;
    if (layers == shown_ && cur == shown_current_ && vp == shown_vp_) {
        return; // nothing changed -> don't disturb in-progress edits
    }
    rebuild_table(layers, cur, vp);
}

void LayerDialog::rebuild_table(const std::vector<core::Layer>& layers, std::uint16_t current,
                                const std::optional<std::vector<std::uint16_t>>& vp_frozen) {
    building_ = true;
    shown_ = layers;
    shown_current_ = current;
    shown_vp_ = vp_frozen;
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
        table_->setItem(row, kNewVpFreeze, check_item(l.vp_freeze_new));
        table_->setItem(row, kPlot, check_item(l.plot));
        table_->setItem(row, kDescription, new QTableWidgetItem(QString::fromStdString(l.description)));
        if (vp_frozen.has_value()) {
            const bool frozen_here = std::find(vp_frozen->begin(), vp_frozen->end(),
                                               static_cast<std::uint16_t>(row)) != vp_frozen->end();
            table_->setItem(row, kVpFreeze, check_item(frozen_here));
        } else {
            auto* none = new QTableWidgetItem(QStringLiteral("—")); // no current viewport
            none->setFlags(Qt::NoItemFlags);
            none->setToolTip(QStringLiteral("Freeze per viewport: MSPACE into a viewport first."));
            table_->setItem(row, kVpFreeze, none);
        }

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

        auto* tpy = new QSpinBox(this);
        tpy->setRange(0, 90);
        tpy->setSuffix(QStringLiteral(" %"));
        tpy->setValue(l.transparency);
        tpy->setToolTip(QStringLiteral("How see-through the layer's objects are on screen, 0 to 90 percent."));
        connect(tpy, &QSpinBox::valueChanged, this, [this, row] {
            if (!building_) {
                submit_row(row);
            }
        });
        table_->setCellWidget(row, kTransparency, tpy);
    }
    building_ = false;
    apply_filter(); // a rebuilt table keeps the search
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
    if (auto* nv = table_->item(row, kNewVpFreeze)) {
        l.vp_freeze_new = nv->checkState() == Qt::Checked;
    }
    if (auto* pl = table_->item(row, kPlot)) {
        l.plot = pl->checkState() == Qt::Checked;
    }
    if (auto* de = table_->item(row, kDescription)) {
        l.description = de->text().toStdString();
    }
    if (auto* lt = qobject_cast<QComboBox*>(table_->cellWidget(row, kLinetype))) {
        l.linetype = static_cast<core::Linetype>(lt->currentIndex());
    }
    if (auto* lw = qobject_cast<QComboBox*>(table_->cellWidget(row, kLineweight))) {
        l.lineweight = static_cast<std::uint8_t>(lw->currentData().toInt());
    }
    if (auto* tpy = qobject_cast<QSpinBox*>(table_->cellWidget(row, kTransparency))) {
        l.transparency = static_cast<std::uint8_t>(tpy->value());
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
