// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
#include "musacad/ui/mtext_editor.hpp"

#include <algorithm>
#include <cmath>
#include <vector>
#include <QAction>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QShortcut>
#include <QTextBlock>
#include <QTextEdit>
#include <QTextFragment>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/text/mtext.hpp"

namespace musacad::ui {
namespace {
constexpr double preview_size = 14.0;
QString escaped(QString s) {
    s.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    s.replace(QStringLiteral("{"), QStringLiteral("\\{"));
    s.replace(QStringLiteral("}"), QStringLiteral("\\}"));
    s.replace(QChar(0xa0), QStringLiteral("\\~"));
    return s;
}
}

MTextEditor::MTextEditor(const QString& source, double height, const QString& font, QWidget* parent)
    : QDialog(parent, Qt::Tool), original_(source), height_(height > 0 ? height : 2.5) {
    setObjectName(QStringLiteral("MTextEditor"));
    setWindowTitle(QStringLiteral("Text Editor"));
    resize(660, 300);
    setMinimumSize(420, 220);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);
    auto* format = new QToolBar(this);
    format->setObjectName(QStringLiteral("MTextFormat"));
    auto* family = new QFontComboBox(format);
    family->setObjectName(QStringLiteral("MTextFont"));
    family->setMaximumWidth(190);
    family->setToolTip(QStringLiteral("Font"));
    format->addWidget(family);
    auto* height_box = new QDoubleSpinBox(format);
    height_box->setObjectName(QStringLiteral("MTextHeight"));
    height_box->setRange(std::max(0.000001, height_ / 1024.0), std::min(1000000000.0, height_ * 64.0));
    height_box->setDecimals(3);
    height_box->setKeyboardTracking(false);
    height_box->setValue(height_);
    height_box->setPrefix(QStringLiteral("Height "));
    height_box->setToolTip(QStringLiteral("Height of the selected characters in drawing units"));
    format->addWidget(height_box);
    layout->addWidget(format);
    editor_ = new QTextEdit(this);
    editor_->setObjectName(QStringLiteral("MTextContent"));
    // Keep editing legible regardless of the ribbon's compact application font.
    editor_->setStyleSheet(QStringLiteral("QTextEdit { font-size: 14pt; }"));
    editor_->setAcceptRichText(false); // paste text; imported HTML must not introduce unsupported styles
    QFont base = editor_->font();
    if (!font.isEmpty() && font.compare(QStringLiteral("Standard"), Qt::CaseInsensitive) != 0) {
        base.setFamily(font);
    }
    base.setPointSizeF(preview_size);
    editor_->setFont(base);
    editor_->document()->setDefaultFont(base);
    family->setCurrentFont(base);
    source_mode_ = !load_formatted(source, height_);
    if (source_mode_) {
        editor_->setPlainText(source);
        format->setEnabled(false);
        auto* notice = new QLabel(QStringLiteral("Formatting source — this text contains codes that cannot yet be edited visually."), this);
        notice->setWordWrap(true);
        notice->setObjectName(QStringLiteral("MTextSourceNotice"));
        layout->addWidget(notice);
    }
    layout->addWidget(editor_, 1);
    const auto merge = [this](const QTextCharFormat& f) {
        editor_->mergeCurrentCharFormat(f);
        editor_->setFocus();
    };
    connect(family, &QComboBox::activated, this, [family, merge] {
        QTextCharFormat fmt;
        fmt.setFontFamilies({family->currentFont().family()});
        merge(fmt);
    });
    connect(height_box, &QDoubleSpinBox::valueChanged, this, [this, merge](double h) {
        QTextCharFormat fmt;
        fmt.setFontPointSize(preview_size * h / height_);
        merge(fmt);
    });
    auto* styles = new QToolBar(this);
    styles->setObjectName(QStringLiteral("MTextStyles"));
    styles->setEnabled(!source_mode_);
    layout->insertWidget(1, styles);
    const auto toggle = [this, styles, merge](const QString& label, const QString& tip,
                                           const QString& id, auto apply) {
        auto* a = styles->addAction(label);
        a->setObjectName(id);
        a->setToolTip(tip);
        a->setCheckable(true);
        connect(a, &QAction::triggered, this, [merge, apply](bool on) {
            QTextCharFormat f;
            apply(f, on);
            merge(f);
        });
        return a;
    };
    auto* bold = toggle(QStringLiteral("B"), QStringLiteral("Bold"), QStringLiteral("MTextBold"),
                        [](QTextCharFormat& f, bool on) { f.setFontWeight(on ? QFont::Bold : QFont::Normal); });
    auto* italic = toggle(QStringLiteral("I"), QStringLiteral("Italic"), QStringLiteral("MTextItalic"),
                          [](QTextCharFormat& f, bool on) { f.setFontItalic(on); });
    auto* underline = toggle(QStringLiteral("U"), QStringLiteral("Underline"), QStringLiteral("MTextUnderline"),
                             [](QTextCharFormat& f, bool on) { f.setFontUnderline(on); });
    auto* overline = toggle(QStringLiteral("O̅"), QStringLiteral("Overline"), QStringLiteral("MTextOverline"),
                            [](QTextCharFormat& f, bool on) { f.setFontOverline(on); });
    auto* strike = toggle(QStringLiteral("S̶"), QStringLiteral("Strikethrough"), QStringLiteral("MTextStrike"),
                          [](QTextCharFormat& f, bool on) { f.setFontStrikeOut(on); });
    QFont bold_font = styles->font(); bold_font.setBold(true); bold->setFont(bold_font);
    QFont italic_font = styles->font(); italic_font.setItalic(true); italic->setFont(italic_font);
    QFont underline_font = styles->font(); underline_font.setUnderline(true); underline->setFont(underline_font);
    styles->addSeparator();
    auto* color = styles->addAction(QStringLiteral("Color…"));
    color->setObjectName(QStringLiteral("MTextColor"));
    connect(color, &QAction::triggered, this, [this, merge] {
        const QColor c = QColorDialog::getColor(editor_->textColor(), this, QStringLiteral("Text Color"));
        if (c.isValid()) {
            QTextCharFormat f;
            f.setForeground(c);
            merge(f);
        }
    });
    auto* symbols = new QToolButton(styles);
    symbols->setText(QStringLiteral("Symbol"));
    symbols->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(symbols);
    for (const auto& pair : {std::pair{QStringLiteral("Diameter  Ø"), QStringLiteral("Ø")},
                            std::pair{QStringLiteral("Degree  °"), QStringLiteral("°")},
                            std::pair{QStringLiteral("Plus/minus  ±"), QStringLiteral("±")}}) {
        menu->addAction(pair.first, this, [this, value = pair.second] { editor_->insertPlainText(value); editor_->setFocus(); });
    }
    symbols->setMenu(menu);
    styles->addWidget(symbols);
    styles->addSeparator();
    auto* undo = styles->addAction(QStringLiteral("Undo"));
    connect(undo, &QAction::triggered, editor_, &QTextEdit::undo);
    auto* redo = styles->addAction(QStringLiteral("Redo"));
    connect(redo, &QAction::triggered, editor_, &QTextEdit::redo);
    connect(editor_, &QTextEdit::currentCharFormatChanged, this,
            [family, height_box, bold, italic, underline, overline, strike, this](const QTextCharFormat& f) {
        const QSignalBlocker b1(family), b2(height_box), b3(bold), b4(italic), b5(underline), b6(overline), b7(strike);
        if (!f.property(QTextFormat::FontFamilies).toStringList().isEmpty()) family->setCurrentFont(f.font());
        height_box->setValue((f.fontPointSize() > 0 ? f.fontPointSize() : preview_size) / preview_size * height_);
        bold->setChecked(f.fontWeight() >= QFont::Bold);
        italic->setChecked(f.fontItalic());
        underline->setChecked(f.fontUnderline());
        overline->setChecked(f.fontOverline());
        strike->setChecked(f.fontStrikeOut());
    });
    auto* footer = new QHBoxLayout;
    footer->addWidget(new QLabel(QStringLiteral("Ctrl+Enter to save · Esc to cancel"), this));
    footer->addStretch();
    auto* cancel = new QPushButton(QStringLiteral("Cancel"), this);
    cancel->setObjectName(QStringLiteral("MTextCancel"));
    cancel->setAutoDefault(false);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    footer->addWidget(cancel);
    auto* close = new QPushButton(QStringLiteral("Close Text Editor"), this);
    close->setObjectName(QStringLiteral("MTextSave"));
    close->setAutoDefault(false);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    footer->addWidget(close);
    layout->addLayout(footer);
    for (const auto& key : {QKeySequence(Qt::CTRL | Qt::Key_Return), QKeySequence(Qt::CTRL | Qt::Key_Enter)}) {
        auto* shortcut = new QShortcut(key, this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, &QDialog::accept);
    }
    editor_->document()->clearUndoRedoStacks();
    editor_->document()->setModified(false);
    editor_->setFocus();
}

bool MTextEditor::load_formatted(const QString& source, double height) {
    if (!core::text::has_mtext_codes(source.toStdString()) && !source.contains(QStringLiteral("%<")) &&
        !source.contains(QStringLiteral("%%")) && !source.contains(QStringLiteral("\\U+"))) {
        editor_->setPlainText(source);
        return true;
    }
    // Keep fields, stacks, paragraph settings and extended shape metrics intact in source mode.
    if (source.contains(QStringLiteral("%<")) || source.contains(QStringLiteral("%%")) || source.contains(QLatin1Char('^'))) return false;
    QTextCursor cursor(editor_->document());
    std::vector<QTextCharFormat> stack(1);
    stack.back().setFontPointSize(preview_size);
    QString run;
    const auto flush = [&] {
        if (!run.isEmpty()) { cursor.insertText(run, stack.back()); run.clear(); }
    };
    for (int i = 0; i < source.size(); ++i) {
        QChar c = source[i];
        if (c == QLatin1Char('{')) { flush(); stack.push_back(stack.back()); continue; }
        if (c == QLatin1Char('}')) {
            flush();
            if (stack.size() == 1) return false;
            stack.pop_back(); continue;
        }
        if (c == QLatin1Char('\n')) { flush(); cursor.insertBlock(); continue; }
        if (c != QLatin1Char('\\')) { run += c; continue; }
        if (++i == source.size()) return false;
        c = source[i];
        if (c == QLatin1Char('\\') || c == QLatin1Char('{') || c == QLatin1Char('}')) { run += c; continue; }
        if (c == QLatin1Char('~')) { run += QChar(0xa0); continue; }
        flush();
        if (c == QLatin1Char('P')) { cursor.insertBlock(); continue; }
        auto& f = stack.back();
        if (QStringLiteral("LlOoKk").contains(c)) {
            if (c.toUpper() == QLatin1Char('L')) f.setFontUnderline(c.isUpper());
            if (c.toUpper() == QLatin1Char('O')) f.setFontOverline(c.isUpper());
            if (c.toUpper() == QLatin1Char('K')) f.setFontStrikeOut(c.isUpper());
            continue;
        }
        if (!QStringLiteral("HfFCc").contains(c)) return false;
        const auto end = source.indexOf(QLatin1Char(';'), i + 1);
        if (end < 0) return false;
        QString arg = source.mid(i + 1, end - i - 1);
        i = static_cast<int>(end);
        bool ok = false;
        if (c == QLatin1Char('H')) {
            const bool relative = arg.endsWith(QLatin1Char('x'), Qt::CaseInsensitive);
            if (relative) arg.chop(1);
            const double v = arg.toDouble(&ok);
            if (!ok || !std::isfinite(v) || v <= 0) return false;
            const double size = relative ? f.fontPointSize() * v : preview_size * v / height;
            if (!std::isfinite(size) || size < 0.01 || size > 1000) return false;
            f.setFontPointSize(size);
        } else if (c == QLatin1Char('f') || c == QLatin1Char('F')) {
            const auto parts = arg.split(QLatin1Char('|'));
            if (parts.front().endsWith(QStringLiteral(".shx"), Qt::CaseInsensitive)) return false;
            f.setFontFamilies({parts.front()});
            for (int j = 1; j < parts.size(); ++j) {
                if (parts[j] == QStringLiteral("b0") || parts[j] == QStringLiteral("b1")) f.setFontWeight(parts[j].endsWith(QLatin1Char('1')) ? QFont::Bold : QFont::Normal);
                else if (parts[j] == QStringLiteral("i0") || parts[j] == QStringLiteral("i1")) f.setFontItalic(parts[j].endsWith(QLatin1Char('1')));
                else return false; // charset and pitch must survive unchanged
            }
        } else {
            const uint v = arg.toUInt(&ok);
            if (!ok || (c == QLatin1Char('C') ? v < 1 || v > 255 : v > 0xffffff)) return false;
            if (c == QLatin1Char('C')) {
                const auto rgb = core::io::dxf_aci_to_rgb(v);
                f.setForeground(QColor(rgb.r, rgb.g, rgb.b));
            } else f.setForeground(QColor(static_cast<int>((v >> 16) & 255), static_cast<int>((v >> 8) & 255), static_cast<int>(v & 255)));
        }
    }
    flush();
    return stack.size() == 1;
}

QString MTextEditor::text() const {
    if (!editor_->document()->isModified()) return original_;
    if (source_mode_) return editor_->toPlainText();
    QString result;
    for (auto block = editor_->document()->begin(); block.isValid(); block = block.next()) {
        if (block != editor_->document()->begin()) result += QStringLiteral("\\P");
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            if (!fragment.isValid()) continue;
            const auto f = fragment.charFormat();
            const double size = f.fontPointSize() > 0 ? f.fontPointSize() : preview_size;
            result += QStringLiteral("{\\H%1x;").arg(size / preview_size, 0, 'g', 12);
            QString family = f.property(QTextFormat::FontFamilies).toStringList().isEmpty() ? editor_->font().family() : f.property(QTextFormat::FontFamilies).toStringList().front();
            family.remove(QLatin1Char(';')); family.remove(QLatin1Char('|')); family.remove(QLatin1Char('\\'));
            if (!f.property(QTextFormat::FontFamilies).toStringList().isEmpty() || f.fontWeight() >= QFont::Bold || f.fontItalic()) {
                result += QStringLiteral("\\f%1|b%2|i%3;").arg(family).arg(f.fontWeight() >= QFont::Bold ? 1 : 0).arg(f.fontItalic() ? 1 : 0);
            }
            if (f.fontUnderline()) result += QStringLiteral("\\L");
            if (f.fontOverline()) result += QStringLiteral("\\O");
            if (f.fontStrikeOut()) result += QStringLiteral("\\K");
            if (f.hasProperty(QTextFormat::ForegroundBrush)) {
                const auto c = f.foreground().color();
                result += QStringLiteral("\\c%1;").arg((c.red() << 16) | (c.green() << 8) | c.blue());
            }
            result += escaped(fragment.text());
            result += QLatin1Char('}');
        }
    }
    return result;
}

void MTextEditor::keyPressEvent(QKeyEvent* event) {
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
        (event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier))) {
        accept();
        return;
    }
    QDialog::keyPressEvent(event);
}
} // namespace musacad::ui
