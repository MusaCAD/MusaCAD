// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
#include <cstdio>
#include <QApplication>
#include <QAction>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QTextCursor>
#include <QTextEdit>
#include "musacad/ui/mtext_editor.hpp"
#include "musacad/ui/qt_font_engine.hpp"
#include "musacad/core/text/mtext.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    bool ok = true;
    const auto check = [&](bool result, const char* label) {
        std::printf("[text-editor] %s: %s\n", label, result ? "PASS" : "FAIL");
        ok = ok && result;
    };
    using musacad::ui::MTextEditor;
    const QString raw = QStringLiteral("{\\LUpper\\l}\\P{\\H2x;\\c16711680;Red}");
    MTextEditor edited(raw, 2.5, {}, nullptr);
    check(!edited.source_mode(), "supported formatting opens visually");
    check(edited.editor()->toPlainText() == QStringLiteral("Upper\nRed"), "codes render as text");
    check(edited.text() == raw, "no-op keeps original source byte for byte");
    QTextCursor cursor = edited.editor()->textCursor();
    cursor.setPosition(0); cursor.setPosition(5, QTextCursor::KeepAnchor);
    edited.editor()->setTextCursor(cursor);
    auto* bold_action = edited.findChild<QAction*>(QStringLiteral("MTextBold"));
    if (!bold_action) {
        edited.dumpObjectTree();
        check(false, "bold action is accessible");
        return 1;
    }
    bold_action->trigger();
    const QString encoded = edited.text();
    check(encoded.contains(QStringLiteral("|b1")), "selection gets bold face flag");
    MTextEditor reopened(encoded, 2.5, {}, nullptr);
    check(!reopened.source_mode() && reopened.editor()->toPlainText() == QStringLiteral("Upper\nRed"), "formatted edit round-trips");
    cursor = reopened.editor()->textCursor(); cursor.setPosition(2);
    check(cursor.charFormat().fontUnderline() && cursor.charFormat().fontWeight() >= QFont::Bold, "underline and bold survive");
    cursor.setPosition(8);
    check(cursor.charFormat().foreground().color() == QColor(Qt::red) && cursor.charFormat().fontPointSize() == 28,
          "height and color survive outside selection");
    edited.editor()->undo();
    check(edited.text() == raw, "undo restores original source");
    for (const auto& value : {QStringLiteral("{\\S1/2;}"), QStringLiteral("\\W0.8;Narrow"),
                              QStringLiteral("%<\\AcVar Date>%"), QStringLiteral("\\pxt2;Paragraph"),
                              QStringLiteral("\\fArial|c0;Text"), QStringLiteral("\\U+2300 50")}) {
        MTextEditor source(value, 2.5, {}, nullptr);
        check(source.source_mode() && source.text() == value, "unsupported codes are lossless source");
        source.editor()->moveCursor(QTextCursor::End);
        source.editor()->insertPlainText(QStringLiteral("!"));
        check(source.text() == value + QLatin1Char('!'), "source edits keep codes");
    }
    MTextEditor plain(QStringLiteral("Path {A} C:\\temp"), 2.5, {}, nullptr);
    plain.editor()->moveCursor(QTextCursor::End);
    plain.editor()->insertPlainText(QStringLiteral(" Ω"));
    const auto plain_encoded = plain.text();
    check(QString::fromStdString(musacad::core::text::mtext_plain(plain_encoded.toStdString())) == QStringLiteral("Path {A} C:\\temp Ω"),
          "literal braces, backslashes and Unicode survive visual editing");
    check(!plain_encoded.contains(QStringLiteral("\\f")), "plain edits retain the object's base font");
    MTextEditor accepted({}, 2.5, {}, nullptr);
    QKeyEvent save(QEvent::KeyPress, Qt::Key_Return, Qt::ControlModifier);
    QApplication::sendEvent(&accepted, &save);
    check(accepted.result() == QDialog::Accepted, "Ctrl+Enter saves");
    MTextEditor cancelled({}, 2.5, {}, nullptr);
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&cancelled, &escape);
    check(cancelled.result() == QDialog::Rejected, "Escape cancels");
    musacad::ui::QtFontEngine fonts;
    const auto family = QFontDatabase::systemFont(QFontDatabase::GeneralFont).family().toStdString();
    check(fonts.is_outline_font(family + "|b1|i1"), "bold italic face resolves");
    std::vector<musacad::core::Vec2> regular, bold;
    fonts.glyph_fills(family, "Text", {}, 2.5, 0, regular);
    fonts.glyph_fills(family + "|b1", "Text", {}, 2.5, 0, bold);
    check(!bold.empty() && regular != bold, "bold uses different glyph geometry");
    if (qEnvironmentVariableIsSet("MUSACAD_TEXT_EDITOR_SHOT")) {
        reopened.show(); app.processEvents();
        reopened.grab().save(qEnvironmentVariable("MUSACAD_TEXT_EDITOR_SHOT"));
    }
    return ok ? 0 : 1;
}
