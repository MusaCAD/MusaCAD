// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

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

    // Headless / CI smoke path: launch, render a few frames, quit. Lets the ASan
    // dev build verify a clean, leak-free startup/shutdown.
    if (qEnvironmentVariableIsSet("MUSACAD_SMOKE")) {
        QTimer::singleShot(1500, &app, &QApplication::quit);
    }

    return app.exec();
}
