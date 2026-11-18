// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
#pragma once

#include <QDialog>
#include <QString>
class QTextEdit;
class QKeyEvent;

namespace musacad::ui {

/// Floating multiline editor. Unsupported MTEXT codes remain in source mode;
/// opening and accepting without edits always returns the original source.
class MTextEditor final : public QDialog {
public:
    MTextEditor(const QString& source, double height, const QString& font, QWidget* parent);
    QString text() const;
    bool source_mode() const { return source_mode_; }
    QTextEdit* editor() const { return editor_; }
protected:
    void keyPressEvent(QKeyEvent* event) override;
private:
    bool load_formatted(const QString& source, double height);
    QString original_;
    QTextEdit* editor_ = nullptr;
    bool source_mode_ = false;
    double height_ = 2.5;
};

} // namespace musacad::ui
