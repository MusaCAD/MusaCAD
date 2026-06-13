// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

#include <string>
#include <vector>

#include <QDialog>

#include "musacad/ui/plot.hpp"

class QComboBox;
class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QLineEdit;

namespace musacad::ui {

/// The PLOT dialog (dark, inherits the global Fusion dark palette). Exposes the AutoCAD
/// controls -- target (PDF + installed printers), paper, orientation, plot area
/// (Display/Extents/Window + "Pick"), scale (fit/ratio), centre/offset, plot-lineweights,
/// plot style (CTB), copies, plus saved page-setup recall/save. It owns no plotting logic:
/// it gathers a PlotSpec and emits signals; the host (MainWindow) drives pick/preview/plot.
class PlotDialog : public QDialog {
    Q_OBJECT

public:
    PlotDialog(const PlotSpec& initial, std::vector<std::string> printer_names,
               std::vector<std::string> setup_names, QWidget* parent = nullptr);

    /// Read every control into a PlotSpec (paper dimensions resolved per orientation).
    [[nodiscard]] PlotSpec spec() const;
    /// Push a spec into the controls (after a window pick or a page-setup recall).
    void set_spec(const PlotSpec& s);

Q_SIGNALS:
    void pickWindowRequested();
    void previewRequested();
    void saveSetupRequested(const QString& name);
    void recallSetupRequested(const QString& name);

private:
    void sync_enabled();

    QComboBox* target_ = nullptr;
    QComboBox* paper_ = nullptr;
    QComboBox* orient_ = nullptr;
    QComboBox* area_ = nullptr;
    QCheckBox* fit_ = nullptr;
    QDoubleSpinBox* scale_num_ = nullptr;
    QDoubleSpinBox* scale_den_ = nullptr;
    QCheckBox* center_ = nullptr;
    QDoubleSpinBox* off_x_ = nullptr;
    QDoubleSpinBox* off_y_ = nullptr;
    QCheckBox* lineweights_ = nullptr;
    QComboBox* style_ = nullptr;
    QSpinBox* copies_ = nullptr;
    QComboBox* setup_ = nullptr;
    PlotSpec spec_; // window corners kept here (picked via the viewport, no control)
};

} // namespace musacad::ui
