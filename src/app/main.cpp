// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include <cstdio>

#include <QApplication>
#include <QIcon>
#include <QStringList>
#include <QTimer>

#include "musacad/ui/main_window.hpp"
#include "musacad/ui/theme.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Musa-CAD"));
    QCoreApplication::setApplicationName(QStringLiteral("musa_cad"));

    // Branding: window / taskbar icon from the embedded logo (Qt renders the SVG at every
    // size the desktop asks for). The .desktop entry + .ico cover launcher/Windows icons.
    app.setWindowIcon(QIcon(QStringLiteral(":/branding/musacad_logo.svg")));

    // Centralized, swappable styling: Fusion + dark palette + QSS, so the whole
    // UI -- including dialogs, message boxes and the file picker -- is consistent.
    musacad::ui::apply_dark_theme(app);

    musacad::ui::MainWindow window;
    window.show();

    // Headless structural check: dump the ribbon/frame widget tree and confirm
    // ribbon buttons fire existing commands, then quit.
    if (qEnvironmentVariableIsSet("MUSACAD_DUMP_UI")) {
        QTimer::singleShot(600, &window, [&window, &app] {
            window.dump_ui();
            if (const QString path = qEnvironmentVariable("MUSACAD_SCREENSHOT"); !path.isEmpty()) {
                window.grab().save(path);
            }
            app.quit();
        });
    }

    // Real-window self-test for the Delete-key route (Phase 9).
    if (qEnvironmentVariableIsSet("MUSACAD_SELFTEST")) {
        QTimer::singleShot(900, &window, [&window, &app] {
            const bool ok_delete = window.selftest_delete();
            const bool ok_modify = window.selftest_modify();
            const bool ok_dialog = window.selftest_dialog();
            const bool ok_persist = window.selftest_persist();
            const bool ok_theme = window.selftest_theme();
            const bool ok_layers = window.selftest_layers();
            const bool ok_annotation = window.selftest_annotation();
            const bool ok_grips = window.selftest_grips();
            const bool ok_mtext = window.selftest_mtext();
            const bool ok_props = window.selftest_properties();
            const bool ok_linetype = window.selftest_linetype();
            const bool ok_dimprops = window.selftest_dim_properties();
            const bool ok_dyn = window.selftest_dyn();
            const bool ok_pdlg = window.selftest_param_dialogs();
            const bool ok_dwg = window.selftest_dwg();
            const bool ok = ok_delete && ok_modify && ok_dialog && ok_persist && ok_theme &&
                            ok_layers && ok_annotation && ok_grips && ok_mtext && ok_props &&
                            ok_linetype && ok_dimprops && ok_dyn && ok_pdlg && ok_dwg;
            std::printf("[selftest] overall: %s\n", ok ? "PASS" : "FAIL");
            app.exit(ok ? 0 : 1);
        });
    }

    // Headless plot diagnosis: MUSACAD_PLOT_TEST="in.musa|out.pdf|area" (area 0/1/2 =
    // Display/Extents/Window) loads the file and plots it through the real app path, quits.
    if (qEnvironmentVariableIsSet("MUSACAD_PLOT_TEST")) {
        QTimer::singleShot(900, &window, [&window, &app] {
            const QStringList a = qEnvironmentVariable("MUSACAD_PLOT_TEST").split(QLatin1Char('|'));
            const QString in = a.value(0);
            const QString out = a.value(1, QStringLiteral("/tmp/plot_test.pdf"));
            const int area = a.value(2, QStringLiteral("1")).toInt();
            const bool ok = window.selftest_plot_file(in, out, area);
            app.exit(ok ? 0 : 1);
        });
    }

    // Headless REAL-GUI-path plot repro: MUSACAD_GUI_PLOT_TEST="in.musa|out.pdf" loads the
    // file and plots the DIALOG's initial spec (exactly as Ctrl+P), logging provenance.
    if (qEnvironmentVariableIsSet("MUSACAD_GUI_PLOT_TEST")) {
        QTimer::singleShot(900, &window, [&window, &app] {
            const QStringList a =
                qEnvironmentVariable("MUSACAD_GUI_PLOT_TEST").split(QLatin1Char('|'));
            const bool ok = window.selftest_gui_plot_file(
                a.value(0), a.value(1, QStringLiteral("/tmp/gui_plot.pdf")));
            app.exit(ok ? 0 : 1);
        });
    }

    // Real-window DYN capture: MUSACAD_DYN_SHOT="kind|out.png". kinds 0 REC / 1 LINE /
    // 2 CIRCLE drive a rubber-band (on-geometry value fields); 3 idle command entry;
    // 4 FILLET / 5 CHAMFER sub-prompts; 6 F12-OFF (classic bottom bar) / 7 F12-ON
    // (canvas-only). Prints the diagnostic + grabs the app region for eyes-on review.
    if (qEnvironmentVariableIsSet("MUSACAD_DYN_SHOT")) {
        QTimer::singleShot(900, &window, [&window, &app] {
            const QStringList a = qEnvironmentVariable("MUSACAD_DYN_SHOT").split(QLatin1Char('|'));
            const int kind = a.value(0, QStringLiteral("0")).toInt();
            const QString out = a.value(1, QStringLiteral("/tmp/dyn_shot.png"));
            const bool ok = window.dyn_shot(kind, out.toStdString());
            app.exit(ok ? 0 : 1);
        });
    }

    // OFFSET-polyline-fix + JOIN capture: MUSACAD_OFFSET_SHOT="kind|out.png" (kind 0
    // rectangle offset, 1 filleted-rectangle offset, 2 open-polyline offset, 3 over-large
    // failure message, 4 JOIN four lines -> closed polyline -> uniform offset).
    if (qEnvironmentVariableIsSet("MUSACAD_OFFSET_SHOT")) {
        QTimer::singleShot(900, &window, [&window, &app] {
            const QStringList a = qEnvironmentVariable("MUSACAD_OFFSET_SHOT").split(QLatin1Char('|'));
            const int kind = a.value(0, QStringLiteral("0")).toInt();
            const QString out = a.value(1, QStringLiteral("/tmp/offset_shot.png"));
            const bool ok = window.offset_shot(kind, out.toStdString());
            app.exit(ok ? 0 : 1);
        });
    }

    // Multi-document capture: MUSACAD_MULTIDOC_SHOT="kind|out.png" (kind 0 two tabs,
    // 1 per-tab view preserved, 2 close-dirty prompt, 3 open makes a new tab, 4 undo per tab).
    if (qEnvironmentVariableIsSet("MUSACAD_MULTIDOC_SHOT")) {
        QTimer::singleShot(900, &window, [&window, &app] {
            const QStringList a =
                qEnvironmentVariable("MUSACAD_MULTIDOC_SHOT").split(QLatin1Char('|'));
            const int kind = a.value(0, QStringLiteral("0")).toInt();
            const QString out = a.value(1, QStringLiteral("/tmp/multidoc_shot.png"));
            const bool ok = window.multidoc_shot(kind, out.toStdString());
            app.exit(ok ? 0 : 1);
        });
    }

    // Text-quality capture: MUSACAD_TEXT_SHOT="kind|out.png" builds a representative
    // title block (kind 0), saves it to "<out>.musa" for the plot-unchanged proof, and
    // grabs the app region for the before/after stroke-text comparison.
    if (qEnvironmentVariableIsSet("MUSACAD_TEXT_SHOT")) {
        QTimer::singleShot(900, &window, [&window, &app] {
            const QStringList a = qEnvironmentVariable("MUSACAD_TEXT_SHOT").split(QLatin1Char('|'));
            const int kind = a.value(0, QStringLiteral("0")).toInt();
            const QString out = a.value(1, QStringLiteral("/tmp/text_shot.png"));
            const bool ok = window.text_shot(kind, out.toStdString());
            app.exit(ok ? 0 : 1);
        });
    }

    // MATCHPROP / MA capture: MUSACAD_MATCHPROP_SHOT="kind|out.png" (kind 0 cross-kind
    // universal, 1 text family, 2 dim family, 3 Settings dialog, 4 skips inapplicable).
    if (qEnvironmentVariableIsSet("MUSACAD_MATCHPROP_SHOT")) {
        QTimer::singleShot(900, &window, [&window, &app] {
            const QStringList a =
                qEnvironmentVariable("MUSACAD_MATCHPROP_SHOT").split(QLatin1Char('|'));
            const int kind = a.value(0, QStringLiteral("0")).toInt();
            const QString out = a.value(1, QStringLiteral("/tmp/matchprop_shot.png"));
            const bool ok = window.matchprop_shot(kind, out.toStdString());
            app.exit(ok ? 0 : 1);
        });
    }

    // LTSCALE / CELTSCALE capture: MUSACAD_LTSCALE_SHOT="kind|out.png" (kind 0 LTSCALE 1.0,
    // 1 LTSCALE 0.5, 2 per-entity CELTSCALE, 3 the 22-unit-line case). Saves "<out>.musa".
    if (qEnvironmentVariableIsSet("MUSACAD_LTSCALE_SHOT")) {
        QTimer::singleShot(900, &window, [&window, &app] {
            const QStringList a =
                qEnvironmentVariable("MUSACAD_LTSCALE_SHOT").split(QLatin1Char('|'));
            const int kind = a.value(0, QStringLiteral("0")).toInt();
            const QString out = a.value(1, QStringLiteral("/tmp/ltscale_shot.png"));
            const bool ok = window.ltscale_shot(kind, out.toStdString());
            app.exit(ok ? 0 : 1);
        });
    }

    // DYN COMMAND CONTROL capture: MUSACAD_CMDCTL_SHOT="kind|out.png" (kind 0 Esc cancels a
    // LINE mid-rubber-band, 1 Enter ends a LINE, 2 Enter two-step commit-then-end, 3 a ribbon
    // click cancels the active command and starts the new one). Drives the keys through the
    // app-wide event filter and prints PASS/FAIL plus the winId for an `import` capture.
    if (qEnvironmentVariableIsSet("MUSACAD_CMDCTL_SHOT")) {
        QTimer::singleShot(900, &window, [&window, &app] {
            const QStringList a =
                qEnvironmentVariable("MUSACAD_CMDCTL_SHOT").split(QLatin1Char('|'));
            const int kind = a.value(0, QStringLiteral("0")).toInt();
            const QString out = a.value(1, QStringLiteral("/tmp/cmdctl_shot.png"));
            const bool ok = window.cmdctl_shot(kind, out.toStdString());
            app.exit(ok ? 0 : 1);
        });
    }

    // Headless / CI smoke path: launch, render a few frames, quit. Lets the ASan
    // dev build verify a clean, leak-free startup/shutdown.
    if (qEnvironmentVariableIsSet("MUSACAD_SMOKE")) {
        QTimer::singleShot(1500, &app, &QApplication::quit);
    }

    return app.exec();
}
