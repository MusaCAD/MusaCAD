#include <cstdio>

#include <QApplication>
#include <QTimer>

#include "musacad/ui/main_window.hpp"
#include "musacad/ui/theme.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

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
            const bool ok = ok_delete && ok_modify && ok_dialog && ok_persist && ok_theme &&
                            ok_layers && ok_annotation && ok_grips && ok_mtext && ok_props &&
                            ok_linetype;
            std::printf("[selftest] overall: %s\n", ok ? "PASS" : "FAIL");
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
