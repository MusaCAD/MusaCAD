// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include <QApplication>
#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QStringList>
#include <QTimer>

#include "musacad/app/cli.hpp"
#include "musacad/app/plot_cli.hpp"
#include "musacad/app/win_console.hpp"
#include "musacad/ui/main_window.hpp"
#include "musacad/ui/theme.hpp"

namespace {
/// Rebuild an argv for Qt from the arguments the CLI parser did not claim.
std::vector<char*> qt_argv(const std::vector<std::string>& args) {
    std::vector<char*> v;
    v.reserve(args.size());
    for (const std::string& a : args) {
        v.push_back(const_cast<char*>(a.c_str()));
    }
    return v;
}
} // namespace

int main(int argc, char* argv[]) {
    using namespace musacad::app;
    // Windows: a windowed program started from a console has no stdout/stderr; attach to
    // the parent's console first so the CLI modes below can print. No-op elsewhere.
    attach_parent_console();
    // The command line is parsed BEFORE any Qt object exists, so --help/--version/
    // --check need no display, no windowing system and no GL context.
    const CliOptions opts = parse_cli(argc, argv);
    if (!opts.error.empty()) {
        std::fprintf(stderr, "musacad: %s\n", opts.error.c_str());
        std::fprintf(stderr, "Try 'musacad --help'.\n");
        return kExitUsage;
    }
    switch (opts.mode) {
    case CliOptions::Mode::Help:
        std::fputs(help_text().c_str(), stdout);
        return kExitOk;
    case CliOptions::Mode::Version:
        std::printf("%s\n", version_text().c_str());
        return kExitOk;
    case CliOptions::Mode::Check: {
        std::string message;
        const int rc = check_drawing(opts.input, opts.input_is_dxf, message);
        if (rc == kExitOk) {
            std::printf("%s: %s\n", opts.input.c_str(), message.c_str());
        } else {
            std::fprintf(stderr, "musacad: %s: %s\n", opts.input.c_str(), message.c_str());
        }
        return rc;
    }
    case CliOptions::Mode::Plot: {
        // Headless plotting is the QPainter/QPdfWriter route -- no GL context, no widgets,
        // no renderer. A QGuiApplication is enough, and the offscreen platform plugin means
        // it needs no DISPLAY/WAYLAND_DISPLAY.
        //
        // Force offscreen unless the user asked for a platform ON THE COMMAND LINE. An
        // INHERITED QT_QPA_PLATFORM must not win here: a desktop session exports e.g.
        // "wayland;xcb", and a batch plot launched from cron/CI/ssh with that in its
        // environment would abort with "no Qt platform plugin could be initialized" --
        // exactly the unattended use this option exists for.
        const bool explicit_platform =
            std::find(opts.qt_args.begin(), opts.qt_args.end(), "-platform") != opts.qt_args.end();
        if (!explicit_platform) {
            qputenv("QT_QPA_PLATFORM", "offscreen");
        }
        std::vector<char*> pargv = qt_argv(opts.qt_args);
        int pargc = static_cast<int>(pargv.size());
        QGuiApplication gui(pargc, pargv.data());
        std::string error;
        const int rc = run_plot(opts, error);
        if (rc != kExitOk) {
            std::fprintf(stderr, "musacad: %s\n", error.c_str());
        }
        return rc;
    }
    case CliOptions::Mode::Gui:
        break;
    }

    // Only the options Qt understands reach QApplication; ours are already consumed.
    std::vector<char*> qargv = qt_argv(opts.qt_args);
    int qargc = static_cast<int>(qargv.size());
    QApplication app(qargc, qargv.data());
    QCoreApplication::setOrganizationName(QStringLiteral("Musa-CAD"));
    QCoreApplication::setApplicationName(QStringLiteral("musa_cad"));

    // Branding: window / taskbar icon from the embedded logo (Qt renders the SVG at every
    // size the desktop asks for). The .desktop entry + .ico cover launcher/Windows icons.
    app.setWindowIcon(QIcon(QStringLiteral(":/branding/musacad_logo.svg")));

    // Centralized, swappable styling: Fusion + dark palette + QSS, so the whole
    // UI -- including dialogs, message boxes and the file picker -- is consistent.
    musacad::ui::apply_dark_theme(app);

    musacad::ui::MainWindow window;
    // Launch maximized (full screen) like every desktop CAD -- EXCEPT under the headless capture/
    // self-test harnesses, which set their own window geometry (a fixed width per shot kind) and
    // would be broken by a maximized window swallowing the resize(). Those all set a MUSACAD_* env
    // var below, so detect any of them and fall back to a plain show().
    const bool harness =
        qEnvironmentVariableIsSet("MUSACAD_DUMP_UI") || qEnvironmentVariableIsSet("MUSACAD_SELFTEST") ||
        qEnvironmentVariableIsSet("MUSACAD_PLOT_TEST") ||
        qEnvironmentVariableIsSet("MUSACAD_GUI_PLOT_TEST") ||
        qEnvironmentVariableIsSet("MUSACAD_DYN_SHOT") || qEnvironmentVariableIsSet("MUSACAD_OFFSET_SHOT") ||
        qEnvironmentVariableIsSet("MUSACAD_MULTIDOC_SHOT") ||
        qEnvironmentVariableIsSet("MUSACAD_TEXT_SHOT") ||
        qEnvironmentVariableIsSet("MUSACAD_MATCHPROP_SHOT") ||
        qEnvironmentVariableIsSet("MUSACAD_LTSCALE_SHOT") || qEnvironmentVariableIsSet("MUSACAD_MLT_SHOT") ||
        qEnvironmentVariableIsSet("MUSACAD_HATCH_SHOT") ||
        qEnvironmentVariableIsSet("MUSACAD_RIBBON_SHOT") ||
        qEnvironmentVariableIsSet("MUSACAD_CMDCTL_SHOT") ||
        qEnvironmentVariableIsSet("MUSACAD_STRETCH_SHOT") || qEnvironmentVariableIsSet("MUSACAD_SMOKE");
    if (harness) {
        window.show();
    } else {
        window.showMaximized();
    }

    // `musacad drawing.musa` -- the file argument rides the EXISTING OpenDocumentCommand
    // (the same geometry-thread path File ▸ Open uses). Submitting before exec() is safe:
    // the command sits on the MPSC queue until the geometry worker picks it up.
    if (!opts.input.empty()) {
        window.open_from(QString::fromStdString(opts.input), opts.input_is_dxf);
    }

    // Headless structural check: dump the ribbon/frame widget tree and confirm
    // ribbon buttons fire existing commands, then quit.
    if (qEnvironmentVariableIsSet("MUSACAD_DUMP_UI")) {
        // MUSACAD_SCREENSHOT_DELAY_MS lets a capture wait for a drawing to load and settle.
        bool delay_ok = false;
        int delay_ms = qEnvironmentVariable("MUSACAD_SCREENSHOT_DELAY_MS").toInt(&delay_ok);
        if (!delay_ok || delay_ms < 0) {
            delay_ms = 600;
        }
        QTimer::singleShot(delay_ms, &window, [&window, &app] {
            window.dump_ui();
            if (const QString path = qEnvironmentVariable("MUSACAD_SCREENSHOT"); !path.isEmpty()) {
                window.grab().save(path);
                // The GL viewport as rendered (a window grab of a GL surface is black under
                // some compositors): <path>.viewport.png, written by the render thread.
                window.request_viewport_capture((path + ".viewport.png").toStdString());
            }
            QTimer::singleShot(400, &app, [&app] { app.quit(); });
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
            const bool ok_cmds = window.selftest_commands();
            const bool ok = ok_delete && ok_modify && ok_dialog && ok_persist && ok_theme &&
                            ok_layers && ok_annotation && ok_grips && ok_mtext && ok_props &&
                            ok_linetype && ok_dimprops && ok_dyn && ok_pdlg && ok_dwg &&
                            ok_cmds;
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
            const QString out = a.value(1, QDir::temp().filePath(QStringLiteral("plot_test.pdf")));
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
                a.value(0), a.value(1, QDir::temp().filePath(QStringLiteral("gui_plot.pdf"))));
            app.exit(ok ? 0 : 1);
        });
    }

    // Real-mouse STRETCH capture: MUSACAD_STRETCH_SHOT="out_dir". Drives the whole gesture
    // through synthetic mouse events and grabs a PNG at each stage (crossing drag, selected,
    // live preview, committed).
    if (qEnvironmentVariableIsSet("MUSACAD_STRETCH_SHOT")) {
        QTimer::singleShot(900, &window, [&window, &app] {
            const QString out = qEnvironmentVariable("MUSACAD_STRETCH_SHOT");
            app.exit(window.stretch_shot(out.toStdString()) ? 0 : 1);
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
            const QString out = a.value(1, QDir::temp().filePath(QStringLiteral("dyn_shot.png")));
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
            const QString out = a.value(1, QDir::temp().filePath(QStringLiteral("offset_shot.png")));
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
            const QString out = a.value(1, QDir::temp().filePath(QStringLiteral("multidoc_shot.png")));
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
            const QString out = a.value(1, QDir::temp().filePath(QStringLiteral("text_shot.png")));
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
            const QString out = a.value(1, QDir::temp().filePath(QStringLiteral("matchprop_shot.png")));
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
            const QString out = a.value(1, QDir::temp().filePath(QStringLiteral("ltscale_shot.png")));
            const bool ok = window.ltscale_shot(kind, out.toStdString());
            app.exit(ok ? 0 : 1);
        });
    }

    // MLeader text-family + text control codes capture: MUSACAD_MLT_SHOT="kind|out.png"
    // (0 MLeader PR Text section + Color row, 1 MA TEXT->MLeader, 2 MA MLeader->MLeader,
    // 3 TEXT %%c/%%p codes, 4 MTEXT \U+ escape, 5 TEXT overline).
    if (qEnvironmentVariableIsSet("MUSACAD_MLT_SHOT")) {
        QTimer::singleShot(900, &window, [&window, &app] {
            const QStringList a = qEnvironmentVariable("MUSACAD_MLT_SHOT").split(QLatin1Char('|'));
            const int kind = a.value(0, QStringLiteral("0")).toInt();
            const QString out = a.value(1, QDir::temp().filePath(QStringLiteral("mlt_shot.png")));
            const bool ok = window.mleader_text_shot(kind, out.toStdString());
            app.exit(ok ? 0 : 1);
        });
    }

    // HATCH capture: MUSACAD_HATCH_SHOT="kind|out.png" (kind 0 SOLID from a closed polyline).
    if (qEnvironmentVariableIsSet("MUSACAD_HATCH_SHOT")) {
        QTimer::singleShot(900, &window, [&window, &app] {
            const QStringList a = qEnvironmentVariable("MUSACAD_HATCH_SHOT").split(QLatin1Char('|'));
            const int kind = a.value(0, QStringLiteral("0")).toInt();
            const QString out = a.value(1, QDir::temp().filePath(QStringLiteral("hatch_shot.png")));
            const bool ok = window.hatch_shot(kind, out.toStdString());
            app.exit(ok ? 0 : 1);
        });
    }

    // RIBBON capture (Phase A): MUSACAD_RIBBON_SHOT="kind|out.png" (kind 0 the ribbon with real
    // SVG icons; 1 a forced LINE-button tooltip). Prints the winId for an `import` capture.
    if (qEnvironmentVariableIsSet("MUSACAD_RIBBON_SHOT")) {
        QTimer::singleShot(900, &window, [&window, &app] {
            const QStringList a = qEnvironmentVariable("MUSACAD_RIBBON_SHOT").split(QLatin1Char('|'));
            const int kind = a.value(0, QStringLiteral("0")).toInt();
            const QString out = a.value(1, QDir::temp().filePath(QStringLiteral("ribbon_shot.png")));
            const bool ok = window.ribbon_shot(kind, out.toStdString());
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
            const QString out = a.value(1, QDir::temp().filePath(QStringLiteral("cmdctl_shot.png")));
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
