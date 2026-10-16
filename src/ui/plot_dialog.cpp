// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/ui/plot_dialog.hpp"

#include <array>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QPushButton>
#include <QSpinBox>
#include <QString>
#include <QVBoxLayout>

namespace musacad::ui {

namespace {
// The paper table lives in plot.cpp (standard_papers()) so the dialog and the
// headless CLI's --paper resolve the same sheets; this is just a local alias.
using Paper = PaperSize;
const std::span<const PaperSize>& papers() {
    static const std::span<const PaperSize> p = standard_papers();
    return p;
}
} // namespace

PlotDialog::PlotDialog(const PlotSpec& initial, std::vector<std::string> printer_names,
                       std::vector<std::string> setup_names, QWidget* parent)
    : QDialog(parent), spec_(initial) {
    setWindowTitle(QStringLiteral("Plot"));
    setModal(true);
    auto* outer = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    outer->addLayout(form);

    // Target: PDF + every installed printer.
    target_ = new QComboBox(this);
    target_->addItem(QStringLiteral("PDF File…"), QStringLiteral("PDF"));
    for (const std::string& p : printer_names) {
        target_->addItem(QString::fromStdString(p), QString::fromStdString(p));
    }
    form->addRow(QStringLiteral("Printer/plotter"), target_);

    paper_ = new QComboBox(this);
    for (const Paper& p : papers()) {
        paper_->addItem(QString::fromLatin1(p.name));
    }
    form->addRow(QStringLiteral("Paper size"), paper_);

    orient_ = new QComboBox(this);
    orient_->addItems({QStringLiteral("Landscape"), QStringLiteral("Portrait")});
    form->addRow(QStringLiteral("Orientation"), orient_);

    // Plot area + the "Pick Window" button (yields to the viewport).
    area_ = new QComboBox(this);
    area_->addItems({QStringLiteral("Display"), QStringLiteral("Extents"), QStringLiteral("Window")});
    auto* area_row = new QHBoxLayout;
    area_row->addWidget(area_, 1);
    auto* pick = new QPushButton(QStringLiteral("Pick Window <"), this);
    area_row->addWidget(pick);
    form->addRow(QStringLiteral("Plot area"), area_row);
    connect(pick, &QPushButton::clicked, this, [this] { Q_EMIT pickWindowRequested(); });

    // Scale: fit, or an explicit mm : drawing-unit ratio.
    fit_ = new QCheckBox(QStringLiteral("Fit to paper"), this);
    form->addRow(QString(), fit_);
    scale_num_ = new QDoubleSpinBox(this);
    scale_num_->setRange(0.0001, 1e6);
    scale_num_->setDecimals(4);
    scale_den_ = new QDoubleSpinBox(this);
    scale_den_->setRange(0.0001, 1e6);
    scale_den_->setDecimals(4);
    units_ = new QComboBox(this);
    units_->addItems({QStringLiteral("mm"), QStringLiteral("inches")});
    units_->setToolTip(QStringLiteral("The scale and the offset in millimetres or inches."));
    auto* scale_row = new QHBoxLayout;
    scale_row->addWidget(scale_num_);
    scale_row->addWidget(units_);
    scale_row->addWidget(new QLabel(QStringLiteral("=  "), this));
    scale_row->addWidget(scale_den_);
    scale_row->addWidget(new QLabel(QStringLiteral("units"), this));
    form->addRow(QStringLiteral("Scale"), scale_row);

    center_ = new QCheckBox(QStringLiteral("Center the plot"), this);
    form->addRow(QString(), center_);
    off_x_ = new QDoubleSpinBox(this);
    off_x_->setRange(-10000, 10000);
    off_y_ = new QDoubleSpinBox(this);
    off_y_->setRange(-10000, 10000);
    auto* off_row = new QHBoxLayout;
    off_row->addWidget(new QLabel(QStringLiteral("X"), this));
    off_row->addWidget(off_x_);
    off_row->addWidget(new QLabel(QStringLiteral("Y"), this));
    off_row->addWidget(off_y_);
    off_units_ = new QLabel(QStringLiteral("mm"), this);
    off_row->addWidget(off_units_);
    form->addRow(QStringLiteral("Offset"), off_row);

    lineweights_ = new QCheckBox(QStringLiteral("Plot object lineweights"), this);
    form->addRow(QString(), lineweights_);

    style_ = new QComboBox(this);
    style_->addItems({QStringLiteral("None"), QStringLiteral("Monochrome (all black)"),
                      QStringLiteral("Grayscale")});
    form->addRow(QStringLiteral("Plot style (CTB)"), style_);

    copies_ = new QSpinBox(this);
    copies_->setRange(1, 999);
    form->addRow(QStringLiteral("Copies"), copies_);

    // Saved page setups: recall a named setup, or save the current one.
    setup_ = new QComboBox(this);
    setup_->addItem(QStringLiteral("<none>"));
    for (const std::string& n : setup_names) {
        setup_->addItem(QString::fromStdString(n));
    }
    auto* setup_row = new QHBoxLayout;
    setup_row->addWidget(setup_, 1);
    auto* save_setup = new QPushButton(QStringLiteral("Save As…"), this);
    setup_row->addWidget(save_setup);
    form->addRow(QStringLiteral("Page setup"), setup_row);
    connect(setup_, &QComboBox::activated, this, [this](int i) {
        if (i > 0) {
            Q_EMIT recallSetupRequested(setup_->itemText(i));
        }
    });
    connect(save_setup, &QPushButton::clicked, this,
            [this] { Q_EMIT saveSetupRequested(QString()); });

    // Buttons: Preview + Plot/Cancel.
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Plot"));
    auto* preview = buttons->addButton(QStringLiteral("Preview…"), QDialogButtonBox::ActionRole);
    connect(preview, &QPushButton::clicked, this, [this] { Q_EMIT previewRequested(); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);

    connect(fit_, &QCheckBox::toggled, this, &PlotDialog::sync_enabled);
    connect(center_, &QCheckBox::toggled, this, &PlotDialog::sync_enabled);
    set_spec(initial);
    connect(units_, &QComboBox::currentIndexChanged, this, [this](int i) { show_units(i == 1); });
    // An inch paper (ANSI, ARCH) takes inches, a metric one millimetres, as AutoCAD does.
    connect(paper_, &QComboBox::currentIndexChanged, this, [this](int i) {
        const QString name = paper_->itemText(i);
        units_->setCurrentIndex(name.startsWith(QStringLiteral("ANSI")) || name.startsWith(QStringLiteral("ARCH")) ? 1 : 0);
    });
}

void PlotDialog::show_units(bool inches) {
    if (inches == inches_shown_) {
        return;
    }
    const double f = inches ? 1.0 / 25.4 : 25.4;
    for (QDoubleSpinBox* b : {scale_num_, off_x_, off_y_}) {
        b->setValue(b->value() * f);
    }
    inches_shown_ = inches;
    off_units_->setText(inches ? QStringLiteral("inches") : QStringLiteral("mm"));
    if (units_->currentIndex() != (inches ? 1 : 0)) {
        const QSignalBlocker block(units_);
        units_->setCurrentIndex(inches ? 1 : 0);
    }
}

void PlotDialog::sync_enabled() {
    const bool ratio = !fit_->isChecked();
    scale_num_->setEnabled(ratio);
    scale_den_->setEnabled(ratio);
    const bool off = !center_->isChecked();
    off_x_->setEnabled(off);
    off_y_->setEnabled(off);
}

void PlotDialog::set_spec(const PlotSpec& s) {
    spec_ = s;
    if (const int i = target_->findData(QString::fromStdString(s.target)); i >= 0) {
        target_->setCurrentIndex(i);
    }
    for (std::size_t i = 0; i < papers().size(); ++i) {
        if (s.paper == papers()[i].name) {
            paper_->setCurrentIndex(static_cast<int>(i));
        }
    }
    orient_->setCurrentIndex(s.landscape ? 0 : 1);
    area_->setCurrentIndex(static_cast<int>(s.area));
    fit_->setChecked(s.fit);
    // Shown in inches on an inch paper; the spec itself is always in millimetres.
    const bool inches = s.paper.starts_with("ANSI") || s.paper.starts_with("ARCH");
    const double f = inches ? 1.0 / 25.4 : 1.0;
    inches_shown_ = inches;
    {
        const QSignalBlocker block(units_);
        units_->setCurrentIndex(inches ? 1 : 0);
    }
    off_units_->setText(inches ? QStringLiteral("inches") : QStringLiteral("mm"));
    scale_num_->setValue(s.scale_num * f);
    scale_den_->setValue(s.scale_den);
    center_->setChecked(s.center);
    off_x_->setValue(s.off_x_mm * f);
    off_y_->setValue(s.off_y_mm * f);
    lineweights_->setChecked(s.plot_lineweights);
    style_->setCurrentIndex(static_cast<int>(s.style));
    copies_->setValue(s.copies);
    sync_enabled();
}

PlotSpec PlotDialog::spec() const {
    PlotSpec s = spec_; // carries the picked window corners
    s.target = target_->currentData().toString().toStdString();
    const Paper& p = papers()[static_cast<std::size_t>(paper_->currentIndex())];
    s.paper = p.name;
    s.landscape = orient_->currentIndex() == 0;
    s.paper_w_mm = s.landscape ? p.long_mm : p.short_mm;
    s.paper_h_mm = s.landscape ? p.short_mm : p.long_mm;
    s.area = static_cast<PlotSpec::Area>(area_->currentIndex());
    s.fit = fit_->isChecked();
    const double to_mm = inches_shown_ ? 25.4 : 1.0;
    s.scale_num = scale_num_->value() * to_mm;
    s.scale_den = scale_den_->value();
    s.center = center_->isChecked();
    s.off_x_mm = off_x_->value() * to_mm;
    s.off_y_mm = off_y_->value() * to_mm;
    s.plot_lineweights = lineweights_->isChecked();
    s.style = static_cast<PlotSpec::Style>(style_->currentIndex());
    s.copies = copies_->value();
    return s;
}

} // namespace musacad::ui
