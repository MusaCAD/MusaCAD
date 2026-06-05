// Scripted command-line demonstration: drives commands through the real
// CommandProcessor + GeometryEngine and prints AutoCAD-style transcripts plus
// the resulting snapshot entity counts. Used to produce the phase report.

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/geometry_engine.hpp"

using namespace musacad;

namespace {

struct ConsoleOutput : command::CommandOutput {
    std::string prompt = "Command: ";
    void append_line(const std::string& l) override { std::printf("      %s\n", l.c_str()); }
    void set_prompt(const std::string& p) override { prompt = p; }
};

struct DemoView : command::ViewControl {
    void zoom_extents() override { std::printf("      [view] zoom extents\n"); }
    void zoom_scale(double f) override { std::printf("      [view] zoom x%.3g\n", f); }
};

ConsoleOutput g_out;

void type(command::CommandProcessor& proc, const std::string& text) {
    std::printf("  %s%s\n", g_out.prompt.c_str(), text.c_str());
    proc.submit_line(text);
}
void esc(command::CommandProcessor& proc) {
    std::printf("  %s<ESC>\n", g_out.prompt.c_str());
    proc.cancel();
}

void settle(core::GeometryEngine& engine) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(120);
    while (std::chrono::steady_clock::now() < until) {
        engine.consume_snapshot();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto& s = engine.snapshot();
    std::printf("    -> snapshot: %zu line-verts, %zu points  (segments=%zu)\n\n",
                s.line_vertices.size(), s.points.size(), s.line_vertices.size() / 2);
}

} // namespace

int main() {
    core::GeometryEngine engine;
    engine.start();
    DemoView view;
    command::CommandProcessor proc(
        [&engine](core::Command c) { engine.submit(std::move(c)); }, &view, g_out);

    std::printf("== LINE (canonical: L, 0,0, @100,0, ESC) ==\n");
    type(proc, "L");
    type(proc, "0,0");
    type(proc, "@100,0");
    esc(proc);
    settle(engine);

    std::printf("== CIRCLE (center + radius) ==\n");
    type(proc, "C");
    type(proc, "50,50");
    type(proc, "25");
    settle(engine);

    std::printf("== RECTANGLE (two corners) ==\n");
    type(proc, "REC");
    type(proc, "0,0");
    type(proc, "40,20");
    settle(engine);

    std::printf("== PLINE (polar segments, then Close) ==\n");
    type(proc, "PL");
    type(proc, "100,0");
    type(proc, "@30<0");
    type(proc, "@30<120");
    type(proc, "C");
    settle(engine);

    std::printf("== ARC (three points) ==\n");
    type(proc, "A");
    type(proc, "0,0");
    type(proc, "1,1");
    type(proc, "2,0");
    settle(engine);

    std::printf("== bad input re-prompts (does not abort) ==\n");
    type(proc, "L");
    type(proc, "0,0");
    type(proc, "wat");      // rejected
    type(proc, "@10,10");   // accepted
    esc(proc);
    settle(engine);

    std::printf("== U (undo the last command: the LINE above) ==\n");
    type(proc, "U");
    settle(engine);

    std::printf("== ERASE All, then U restores ==\n");
    type(proc, "ERASE");
    type(proc, "ALL");
    settle(engine);
    type(proc, "U");
    settle(engine);

    std::printf("== ENTER repeats the last command (CIRCLE) ==\n");
    type(proc, "C");
    type(proc, "10,10");
    type(proc, "5");
    settle(engine);
    type(proc, "");          // ENTER on empty -> repeat CIRCLE
    type(proc, "20,20");
    type(proc, "5");
    settle(engine);

    engine.stop();
    std::printf("done.\n");
    return 0;
}
