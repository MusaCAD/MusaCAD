// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// QDIM (#60): dimensions for selected geometry at once -- Continuous, Staggered,
// Baseline, Ordinate, Radius and Diameter.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/command/commands.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/dimension.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/native_format.hpp"

using namespace musacad::core;
using musacad::command::CommandOutput;
using musacad::command::CommandProcessor;
using Catch::Approx;

namespace {
template <class Pred>
bool wait_until(GeometryEngine& e, Pred pred) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        e.consume_snapshot();
        if (pred(e.snapshot())) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

io::Document dump(GeometryEngine& engine, const char* name) {
    const std::filesystem::path p = std::filesystem::temp_directory_path() / name;
    // Wait for THIS save: the status alone can be a stale "Saved" from the dump before,
    // when the command in between (an undo, say) reported nothing.
    engine.consume_snapshot();
    const std::uint64_t before = engine.snapshot().document_version;
    engine.submit(SaveDocumentCommand{p.string(), false});
    REQUIRE(wait_until(engine, [before](const auto& s) { return s.document_version > before; }));
    io::Document doc;
    REQUIRE(io::load_native(p.string(), doc).ok);
    std::filesystem::remove(p);
    return doc;
}

/// What a saved dimension measures, as the dimension itself works it out.
double measured(const io::DocDim& d) {
    DimData dd;
    dd.type = static_cast<DimType>(d.type);
    dd.a = d.a;
    dd.b = d.b;
    dd.line_pt = d.line_pt;
    dd.aux = d.aux;
    return dim_measure(dd);
}

/// The measurements, sorted, rounded to a thousandth (a diameter's point on the circle is
/// a normalised direction times the radius, so it carries rounding).
std::vector<double> values(const io::Document& doc) {
    std::vector<double> v;
    for (const io::DocDim& d : doc.dims) {
        v.push_back(std::round(measured(d) * 1000.0) / 1000.0);
    }
    std::sort(v.begin(), v.end());
    return v;
}

struct Out : CommandOutput {
    std::vector<std::string> lines;
    std::string prompt;
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompt = p; }
};

struct H {
    std::vector<Command> cmds;
    Out out;
    CommandProcessor proc{[this](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
    template <class T>
    [[nodiscard]] const T* last() const {
        for (auto it = cmds.rbegin(); it != cmds.rend(); ++it) {
            if (const auto* c = std::get_if<T>(&*it)) {
                return c;
            }
        }
        return nullptr;
    }
};
} // namespace

TEST_CASE("QDIM Continuous, Baseline and Staggered along a stepped outline") {
    GeometryEngine engine;
    engine.start();
    // A stepped profile: points at x = 0, 10, 25, 45 (each twice).
    engine.submit(AddPolylineCommand{{{0, 0}, {0, 10}, {10, 10}, {10, 20}, {25, 20}, {25, 30}, {45, 30}}, false, 1});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(QuickDimCommand{QuickDimCommand::Mode::Continuous, {20, 50}, 0.0, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "3 dimensions created."; }));
    io::Document doc = dump(engine, "musacad_qdim_continuous.musa");
    REQUIRE(values(doc) == std::vector<double>{10.0, 15.0, 20.0});
    for (const io::DocDim& d : doc.dims) {
        REQUIRE(d.line_pt.y == Approx(50.0)); // all on the one line
        REQUIRE(d.aux == Approx(kPi));        // horizontal
    }
    engine.submit(UndoLastGroupCommand{});

    engine.submit(SelectAllCommand{});
    engine.submit(QuickDimCommand{QuickDimCommand::Mode::Baseline, {20, 50}, 5.0, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "3 dimensions created."; }));
    doc = dump(engine, "musacad_qdim_baseline.musa");
    REQUIRE(values(doc) == std::vector<double>{10.0, 25.0, 45.0});
    std::vector<double> lines;
    for (const io::DocDim& d : doc.dims) {
        lines.push_back(d.line_pt.y);
    }
    std::sort(lines.begin(), lines.end());
    REQUIRE(lines == std::vector<double>{50.0, 55.0, 60.0}); // stacked outward
    engine.submit(UndoLastGroupCommand{});

    // Beside the outline: vertical, measured in y (points at y = 0, 10, 20, 30).
    engine.submit(SelectAllCommand{});
    engine.submit(QuickDimCommand{QuickDimCommand::Mode::Staggered, {-20, 15}, 5.0, 4});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "2 dimensions created."; }));
    doc = dump(engine, "musacad_qdim_staggered.musa");
    REQUIRE(values(doc) == std::vector<double>{10.0, 30.0}); // 10..20 inside, 0..30 outside
    for (const io::DocDim& d : doc.dims) {
        REQUIRE(d.aux == Approx(kHalfPi));
        REQUIRE(d.line_pt.x == Approx(measured(d) > 20.0 ? -25.0 : -20.0));
    }
    engine.stop();
}

TEST_CASE("QDIM Ordinate, Radius and Diameter") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddCircleCommand{{10, 0}, 3.0, 1});
    engine.submit(AddCircleCommand{{30, 0}, 5.0, 2});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
    engine.submit(QuickDimCommand{QuickDimCommand::Mode::Ordinate, {20, 20}, 0.0, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "2 dimensions created."; }));
    io::Document doc = dump(engine, "musacad_qdim_ordinate.musa");
    REQUIRE(values(doc) == std::vector<double>{10.0, 30.0}); // the centres' X
    engine.submit(UndoLastGroupCommand{});

    engine.submit(SelectAllCommand{});
    engine.submit(QuickDimCommand{QuickDimCommand::Mode::Diameter, {20, 20}, 0.0, 4});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "2 dimensions created."; }));
    doc = dump(engine, "musacad_qdim_diameter.musa");
    REQUIRE(values(doc) == std::vector<double>{6.0, 10.0});
    engine.submit(UndoLastGroupCommand{});

    engine.submit(AddLineCommand{{0, 50}, {10, 50}, 5});
    engine.submit(SelectWindowCommand{{-1, 49}, {11, 51}, false, false, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 1; }));
    engine.submit(QuickDimCommand{QuickDimCommand::Mode::Radius, {20, 20}, 0.0, 6});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "QDIM: no circles or arcs selected."; }));
    engine.stop();
}

TEST_CASE("The QDIM prompts; the kind is remembered") {
    H h;
    h.proc.submit_line("QDIM");
    REQUIRE(h.out.prompt == "Select geometry to dimension: ");
    h.proc.set_selection_count(2);
    h.proc.submit_line("");
    REQUIRE(h.out.prompt ==
            "Specify dimension line position, or [Continuous/Staggered/Baseline/Ordinate/Radius/Diameter] "
            "<Continuous>: ");
    h.proc.submit_line("B");
    REQUIRE(h.out.prompt ==
            "Specify dimension line position, or [Continuous/Staggered/Baseline/Ordinate/Radius/Diameter] "
            "<Baseline>: ");
    h.proc.submit_line("20,50");
    REQUIRE(h.last<QuickDimCommand>()->mode == QuickDimCommand::Mode::Baseline);
    REQUIRE(h.last<QuickDimCommand>()->at == Vec2{20, 50});
    REQUIRE_FALSE(h.proc.has_active_command());
    h.proc.submit_line("QDIM"); // a selection already there; Baseline still in force
    REQUIRE(h.out.prompt.find("<Baseline>") != std::string::npos);
    h.proc.submit_line("CONTINUOUS");
    h.proc.submit_line("");
    REQUIRE_FALSE(h.proc.has_active_command());
}
