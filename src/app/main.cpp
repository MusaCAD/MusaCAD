#include <QApplication>
#include <QTimer>

#include "musacad/ui/main_window.hpp"
#include "musacad/ui/theme.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // Centralized, swappable styling (the only place the look is defined).
    app.setStyleSheet(musacad::ui::dark_theme_qss());

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

    // Headless / CI smoke path: launch, render a few frames, quit. Lets the ASan
    // dev build verify a clean, leak-free startup/shutdown.
    if (qEnvironmentVariableIsSet("MUSACAD_SMOKE")) {
        QTimer::singleShot(1500, &app, &QApplication::quit);
    }

    return app.exec();
}
