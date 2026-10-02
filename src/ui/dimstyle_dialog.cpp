// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/ui/dimstyle_dialog.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <utility>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include "musacad/core/dimension.hpp"

namespace musacad::ui {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

constexpr std::array<std::pair<const char*, core::Rgb>, 7> kNamedColors = {{
    {"Red", {255, 0, 0}},
    {"Yellow", {255, 255, 0}},
    {"Green", {0, 255, 0}},
    {"Cyan", {0, 255, 255}},
    {"Blue", {0, 0, 255}},
    {"Magenta", {255, 0, 255}},
    {"White", {255, 255, 255}},
}};

QIcon swatch(const QColor& c) {
    QPixmap px(12, 12);
    px.fill(c);
    return QIcon(px);
}

/// ByLayer, the seven named colours, the style's own colour if it is none of them, and
/// "Select Colour..." to choose any.
QComboBox* color_box(const core::ElementColor& c, QWidget* parent) {
    auto* box = new QComboBox(parent);
    box->addItem(QStringLiteral("ByLayer"));
    int at = 0;
    for (const auto& [name, rgb] : kNamedColors) {
        const QColor q(rgb.r, rgb.g, rgb.b);
        box->addItem(swatch(q), QString::fromLatin1(name), QVariant::fromValue(q));
        if (!c.by_layer && c.color == rgb) {
            at = box->count() - 1;
        }
    }
    if (!c.by_layer && at == 0) {
        const QColor q(c.color.r, c.color.g, c.color.b);
        box->addItem(swatch(q), QStringLiteral("RGB %1,%2,%3").arg(c.color.r).arg(c.color.g).arg(c.color.b),
                     QVariant::fromValue(q));
        at = box->count() - 1;
    }
    box->addItem(QStringLiteral("Select Colour..."), QStringLiteral("pick"));
    box->setCurrentIndex(at);
    auto prev = std::make_shared<int>(at);
    QObject::connect(box, &QComboBox::activated, box, [box, prev](int i) {
        if (i != box->count() - 1) {
            *prev = i;
            return;
        }
        const QVariant was = box->itemData(*prev);
        const QColor start = was.userType() == QMetaType::QColor ? was.value<QColor>() : QColor(Qt::white);
        const QColor q = QColorDialog::getColor(start, box, QStringLiteral("Select Colour"));
        if (!q.isValid()) {
            box->setCurrentIndex(*prev);
            return;
        }
        box->insertItem(box->count() - 1, swatch(q), QStringLiteral("RGB %1,%2,%3").arg(q.red()).arg(q.green()).arg(q.blue()),
                        QVariant::fromValue(q));
        *prev = box->count() - 2;
        box->setCurrentIndex(*prev);
    });
    return box;
}

core::ElementColor read_color(const QComboBox* box) {
    const QVariant v = box->currentData();
    if (v.userType() != QMetaType::QColor) {
        return core::ElementColor{}; // ByLayer
    }
    const QColor q = v.value<QColor>();
    return core::ElementColor{false,
                              {static_cast<std::uint8_t>(q.red()), static_cast<std::uint8_t>(q.green()),
                               static_cast<std::uint8_t>(q.blue())}};
}

QDoubleSpinBox* length_box(double v, double min, QWidget* parent) {
    auto* box = new QDoubleSpinBox(parent);
    box->setDecimals(4);
    box->setRange(min, 1e6);
    box->setSingleStep(0.25);
    box->setValue(v);
    return box;
}

QString arrow_name(std::uint8_t t) {
    switch (t) {
    case 1:
        return QStringLiteral("tick");
    case 2:
        return QStringLiteral("open");
    case 3:
        return QStringLiteral("dot");
    default:
        return QStringLiteral("filled");
    }
}

} // namespace

QString dimstyle_summary(const core::DimStyle& s) {
    return QStringLiteral("Text %1, arrows %2 %3, values like %4 (%5 places)")
        .arg(qs(core::format_dim_value(s.text_height, core::DimStyle{})))
        .arg(qs(core::format_dim_value(s.arrow_size, core::DimStyle{})))
        .arg(arrow_name(s.arrow_type))
        .arg(qs(core::format_dim_value(1234.5, s)))
        .arg(s.precision);
}

// ---------------------------------------------------------------------------
// The editor.
// ---------------------------------------------------------------------------
DimStyleEditor::DimStyleEditor(const core::DimStyle& style, const QString& title, QWidget* parent)
    : QDialog(parent), base_(style) {
    setWindowTitle(title);
    auto* tabs = new QTabWidget(this);

    // Lines.
    auto* lines = new QWidget(tabs);
    auto* lf = new QFormLayout(lines);
    QComboBox* dim_color = color_box(style.dim_color, lines);
    auto* lineweight = new QSpinBox(lines);
    lineweight->setRange(0, 211);
    lineweight->setSuffix(QStringLiteral(" (1/100 mm)"));
    lineweight->setValue(style.dim_lineweight);
    QComboBox* ext_color = color_box(style.ext_color, lines);
    QDoubleSpinBox* ext_beyond = length_box(style.ext_extension, 0.0, lines);
    QDoubleSpinBox* ext_offset = length_box(style.ext_offset, 0.0, lines);
    lf->addRow(QStringLiteral("Dimension line colour:"), dim_color);
    lf->addRow(QStringLiteral("Lineweight:"), lineweight);
    lf->addRow(QStringLiteral("Extension line colour:"), ext_color);
    lf->addRow(QStringLiteral("Extend beyond dim lines:"), ext_beyond);
    lf->addRow(QStringLiteral("Offset from origin:"), ext_offset);
    tabs->addTab(lines, QStringLiteral("Lines"));

    // Symbols and arrows (leaders take their arrowheads from here too).
    auto* arrows = new QWidget(tabs);
    auto* af = new QFormLayout(arrows);
    auto* arrow_type = new QComboBox(arrows);
    arrow_type->addItems({QStringLiteral("Closed filled"), QStringLiteral("Architectural tick"), QStringLiteral("Open"),
                          QStringLiteral("Dot")});
    arrow_type->setCurrentIndex(style.arrow_type < 4 ? style.arrow_type : 0);
    QDoubleSpinBox* arrow_size = length_box(style.arrow_size, 0.0, arrows);
    QComboBox* arrow_color = color_box(style.arrow_color, arrows);
    af->addRow(QStringLiteral("Arrowheads:"), arrow_type);
    af->addRow(QStringLiteral("Arrow size:"), arrow_size);
    af->addRow(QStringLiteral("Arrowhead colour:"), arrow_color);
    af->addRow(new QLabel(QStringLiteral("Leaders and multileaders in this style use these arrowheads."), arrows));
    tabs->addTab(arrows, QStringLiteral("Symbols and Arrows"));

    // Text.
    auto* text = new QWidget(tabs);
    auto* tf = new QFormLayout(text);
    QDoubleSpinBox* text_height = length_box(style.text_height, 0.0001, text);
    QComboBox* text_color = color_box(style.text_color, text);
    auto* placement = new QComboBox(text);
    placement->addItems({QStringLiteral("Above the dimension line"), QStringLiteral("Centred on the dimension line")});
    placement->setCurrentIndex(style.text_above ? 0 : 1);
    auto* fit = new QComboBox(text);
    fit->addItems({QStringLiteral("Automatic: inside when it fits"), QStringLiteral("Always inside"),
                   QStringLiteral("Always outside")});
    fit->setCurrentIndex(style.text_fit < 3 ? style.text_fit : 0);
    tf->addRow(QStringLiteral("Text height:"), text_height);
    tf->addRow(QStringLiteral("Text colour:"), text_color);
    tf->addRow(QStringLiteral("Vertical placement:"), placement);
    tf->addRow(QStringLiteral("Narrow dimensions:"), fit);
    tabs->addTab(text, QStringLiteral("Text"));

    // Primary units.
    auto* units = new QWidget(tabs);
    auto* uf = new QFormLayout(units);
    auto* precision = new QSpinBox(units);
    precision->setRange(0, 8);
    precision->setValue(style.precision);
    auto* separator = new QComboBox(units);
    separator->addItems({QStringLiteral("'.' (Period)"), QStringLiteral("',' (Comma)")});
    separator->setCurrentIndex(style.decimal_separator == ',' ? 1 : 0);
    auto* leading = new QCheckBox(QStringLiteral("Leading zeros (0.50 as .50)"), units);
    leading->setChecked((style.zero_suppression & core::kDimZinLeading) != 0);
    auto* trailing = new QCheckBox(QStringLiteral("Trailing zeros (12.50 as 12.5)"), units);
    trailing->setChecked((style.zero_suppression & core::kDimZinTrailing) != 0);
    auto* sample = new QLabel(units);
    uf->addRow(QStringLiteral("Precision (decimal places):"), precision);
    uf->addRow(QStringLiteral("Decimal separator:"), separator);
    uf->addRow(QStringLiteral("Suppress:"), leading);
    uf->addRow(QString(), trailing);
    uf->addRow(QStringLiteral("Sample:"), sample);
    tabs->addTab(units, QStringLiteral("Primary Units"));

    read_ = [=, this] {
        core::DimStyle s = base_;
        s.dim_color = read_color(dim_color);
        s.dim_lineweight = static_cast<std::uint8_t>(lineweight->value());
        s.ext_color = read_color(ext_color);
        s.ext_extension = ext_beyond->value();
        s.ext_offset = ext_offset->value();
        s.arrow_type = static_cast<std::uint8_t>(arrow_type->currentIndex());
        s.arrow_size = arrow_size->value();
        s.arrow_color = read_color(arrow_color);
        s.text_height = text_height->value();
        s.text_color = read_color(text_color);
        s.text_above = placement->currentIndex() == 0;
        s.text_fit = static_cast<std::uint8_t>(fit->currentIndex());
        s.precision = static_cast<std::uint8_t>(precision->value());
        s.decimal_separator = separator->currentIndex() == 1 ? ',' : '.';
        s.zero_suppression = static_cast<std::uint8_t>((leading->isChecked() ? core::kDimZinLeading : 0) |
                                                       (trailing->isChecked() ? core::kDimZinTrailing : 0));
        return s;
    };
    const auto update_sample = [=, this] {
        const core::DimStyle s = read_();
        sample->setText(qs(core::format_dim_value(1234.5, s)) + QStringLiteral("    ") +
                        qs(core::format_dim_value(0.5, s)) + QStringLiteral("    ") +
                        qs(core::format_dim_value(20.0, s)));
    };
    update_sample();
    connect(precision, &QSpinBox::valueChanged, this, update_sample);
    connect(separator, &QComboBox::currentIndexChanged, this, update_sample);
    connect(leading, &QCheckBox::toggled, this, update_sample);
    connect(trailing, &QCheckBox::toggled, this, update_sample);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);
}

core::DimStyle DimStyleEditor::style() const { return read_ ? read_() : base_; }

// ---------------------------------------------------------------------------
// The manager.
// ---------------------------------------------------------------------------
DimStyleManager::DimStyleManager(std::vector<core::DimStyle> styles, std::uint16_t current,
                                 std::vector<std::string> unused, Submit submit, QWidget* parent)
    : QDialog(parent), styles_(std::move(styles)), current_(current), unused_(std::move(unused)),
      submit_(std::move(submit)) {
    setWindowTitle(QStringLiteral("Dimension Style Manager"));
    if (styles_.empty()) {
        styles_.push_back(core::DimStyle{"Standard"});
    }
    if (current_ >= styles_.size()) {
        current_ = 0;
    }
    current_label_ = new QLabel(this);
    list_ = new QListWidget(this);
    list_->setMinimumWidth(200);
    summary_ = new QLabel(this);
    summary_->setWordWrap(true);
    auto* current_btn = new QPushButton(QStringLiteral("Set Current"), this);
    auto* new_btn = new QPushButton(QStringLiteral("New..."), this);
    auto* modify_btn = new QPushButton(QStringLiteral("Modify..."), this);
    rename_btn_ = new QPushButton(QStringLiteral("Rename..."), this);
    delete_btn_ = new QPushButton(QStringLiteral("Delete"), this);
    auto* close_btn = new QPushButton(QStringLiteral("Close"), this);
    connect(rename_btn_, &QPushButton::clicked, this, [this] { rename_style(); });
    connect(delete_btn_, &QPushButton::clicked, this, [this] { delete_style(); });
    connect(current_btn, &QPushButton::clicked, this, [this] { set_current(); });
    connect(new_btn, &QPushButton::clicked, this, [this] { new_style(); });
    connect(modify_btn, &QPushButton::clicked, this, [this] { modify_style(); });
    connect(close_btn, &QPushButton::clicked, this, &QDialog::accept);
    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && static_cast<std::size_t>(row) < styles_.size()) {
            summary_->setText(dimstyle_summary(styles_[static_cast<std::size_t>(row)]));
        }
        update_buttons();
    });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this] { modify_style(); });

    auto* side = new QVBoxLayout;
    side->addWidget(current_btn);
    side->addWidget(new_btn);
    side->addWidget(modify_btn);
    side->addWidget(rename_btn_);
    side->addWidget(delete_btn_);
    side->addStretch(1);
    side->addWidget(close_btn);
    auto* body = new QHBoxLayout;
    body->addWidget(list_, 1);
    body->addLayout(side);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(current_label_);
    layout->addLayout(body, 1);
    layout->addWidget(summary_);
    refresh();
    list_->setCurrentRow(static_cast<int>(current_));
}

void DimStyleManager::refresh() {
    const int row = list_->currentRow();
    list_->clear();
    for (std::size_t i = 0; i < styles_.size(); ++i) {
        auto* item = new QListWidgetItem(qs(styles_[i].name), list_);
        if (i == current_) {
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
        }
    }
    current_label_->setText(QStringLiteral("Current dimension style: ") + qs(styles_[current_].name));
    list_->setCurrentRow(row >= 0 && row < list_->count() ? row : static_cast<int>(current_));
    update_buttons();
}

int DimStyleManager::selected() const {
    const int row = list_->currentRow();
    return row >= 0 && static_cast<std::size_t>(row) < styles_.size() ? row : static_cast<int>(current_);
}

void DimStyleManager::update_buttons() {
    const auto i = static_cast<std::size_t>(selected());
    const bool unused = std::find(unused_.begin(), unused_.end(), styles_[i].name) != unused_.end();
    rename_btn_->setEnabled(i != 0);
    delete_btn_->setEnabled(i != 0 && i != current_ && unused);
    delete_btn_->setToolTip(i == 0 ? QStringLiteral("Standard is always there.")
                            : i == current_ ? QStringLiteral("The current style cannot be deleted.")
                            : !unused       ? QStringLiteral("Dimensions use this style.")
                                            : QString());
}

void DimStyleManager::rename_style() {
    const auto i = static_cast<std::size_t>(selected());
    if (i == 0) {
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Rename Dimension Style"), QStringLiteral("New name:"),
                                               QLineEdit::Normal, qs(styles_[i].name), &ok)
                             .trimmed();
    if (!ok || name.isEmpty() || name == qs(styles_[i].name)) {
        return;
    }
    for (const core::DimStyle& s : styles_) {
        if (qs(s.name) == name) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("There is already a style called %1.").arg(name));
            return;
        }
    }
    const std::string from = styles_[i].name;
    styles_[i].name = name.toStdString();
    for (std::string& u : unused_) {
        if (u == from) {
            u = styles_[i].name;
        }
    }
    submit_(core::RenameDimStyleCommand{from, styles_[i].name});
    refresh();
}

void DimStyleManager::delete_style() {
    const auto i = static_cast<std::size_t>(selected());
    if (i == 0 || i == current_) {
        return;
    }
    if (QMessageBox::question(this, windowTitle(), QStringLiteral("Delete the dimension style %1?").arg(qs(styles_[i].name))) !=
        QMessageBox::Yes) {
        return;
    }
    submit_(core::DeleteDimStyleCommand{styles_[i].name});
    styles_.erase(styles_.begin() + static_cast<std::ptrdiff_t>(i));
    if (current_ > i) {
        --current_;
    }
    refresh();
}

void DimStyleManager::set_current() {
    const auto i = static_cast<std::size_t>(selected());
    current_ = static_cast<std::uint16_t>(i);
    submit_(core::SetCurrentDimStyleCommand{styles_[i].name, false});
    refresh();
}

void DimStyleManager::new_style() {
    const auto from = static_cast<std::size_t>(selected());
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Create New Dimension Style"),
                                               QStringLiteral("New style name (starting with %1):").arg(qs(styles_[from].name)),
                                               QLineEdit::Normal, QStringLiteral("Copy of ") + qs(styles_[from].name), &ok)
                             .trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }
    for (const QChar c : name) {
        if (QStringLiteral("<>/\\\":;?*|,=`").contains(c)) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("A style name cannot contain %1").arg(c));
            return;
        }
    }
    for (const core::DimStyle& s : styles_) {
        if (qs(s.name) == name) {
            QMessageBox::warning(this, windowTitle(), QStringLiteral("There is already a style called %1.").arg(name));
            return;
        }
    }
    core::DimStyle start = styles_[from];
    start.name = name.toStdString();
    DimStyleEditor editor(start, QStringLiteral("New Dimension Style: ") + name, this);
    if (editor.exec() != QDialog::Accepted) {
        return;
    }
    core::DimStyle made = editor.style();
    made.name = start.name;
    styles_.push_back(made);
    submit_(core::AddDimStyleCommand{made});
    refresh();
    list_->setCurrentRow(static_cast<int>(styles_.size() - 1));
}

void DimStyleManager::modify_style() {
    const auto i = static_cast<std::size_t>(selected());
    DimStyleEditor editor(styles_[i], QStringLiteral("Modify Dimension Style: ") + qs(styles_[i].name), this);
    if (editor.exec() != QDialog::Accepted) {
        return;
    }
    core::DimStyle changed = editor.style();
    changed.name = styles_[i].name;
    styles_[i] = changed;
    submit_(core::SetDimStyleCommand{static_cast<std::uint16_t>(i), changed});
    refresh();
    summary_->setText(dimstyle_summary(changed));
}

} // namespace musacad::ui
