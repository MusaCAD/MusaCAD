#include <QApplication>
#include <QTimer>

#include "musacad/ui/main_window.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    musacad::ui::MainWindow window;
    window.show();

    // Headless / CI smoke path: when MUSACAD_SMOKE is set, the app launches,
    // shows its window (the viewport render thread draws a few frames), then
    // quits. Lets the ASan dev build verify a clean, leak-free startup/shutdown.
    if (qEnvironmentVariableIsSet("MUSACAD_SMOKE")) {
        QTimer::singleShot(1500, &app, &QApplication::quit);
    }

    return app.exec();
}
