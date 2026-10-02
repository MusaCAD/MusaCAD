// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// DIMSTYLE: named dimension styles, the current one every new dimension is drawn in, the
// primary units a style writes its values with (decimal separator, zero suppression),
// -DIMSTYLE Save / Restore / STatus / ?, and the dimension variables (DIMTXT ...).

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/dimension.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"

using namespace musacad::core;
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

struct CaptureOutput : musacad::command::CommandOutput {
    std::vector<std::string> lines;
    std::string prompt;
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompt = p; }
    [[nodiscard]] bool said(const std::string& sub) const {
        for (const std::string& l : lines) {
            if (l.find(sub) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
};

struct Harness {
    std::vector<Command> cmds;
    CaptureOutput out;
    musacad::command::CommandProcessor proc{[this](Command c) { cmds.push_back(std::move(c)); }, nullptr,
                                            out};
    template <class T>
    const T* last() const {
        const T* found = nullptr;
        for (const Command& c : cmds) {
            if (const auto* p = std::get_if<T>(&c)) {
                found = p;
            }
        }
        return found;
    }
    void run(std::initializer_list<const char*> lines) {
        for (const char* l : lines) {
            proc.submit_line(l);
        }
    }
};

DimStyle iso() {
    DimStyle s{"ISO-25"};
    s.text_height = 3.5;
    s.precision = 1;
    s.decimal_separator = ',';
    s.zero_suppression = kDimZinTrailing;
    return s;
}

} // namespace

TEST_CASE("DIMSTYLE primary units: separator, leading and trailing zeros") {
    DimStyle s;
    REQUIRE(format_dim_value(12.5, s) == "12.50");
    REQUIRE(format_dim_value(-0.001, s) == "0.00"); // never "-0.00"
    s.zero_suppression = kDimZinTrailing;
    REQUIRE(format_dim_value(12.5, s) == "12.5");
    REQUIRE(format_dim_value(12.0, s) == "12");
    s.zero_suppression = kDimZinLeading;
    REQUIRE(format_dim_value(0.5, s) == ".50");
    REQUIRE(format_dim_value(-0.5, s) == "-.50");
    REQUIRE(format_dim_value(10.5, s) == "10.50");
    s.zero_suppression = kDimZinLeading | kDimZinTrailing;
    REQUIRE(format_dim_value(0.5, s) == ".5");
    s.zero_suppression = 0;
    s.decimal_separator = ',';
    REQUIRE(format_dim_value(1234.5, s) == "1234,50");

    // The label a dimension shows, tolerance included.
    DimData d;
    d.type = DimType::Aligned;
    d.a = {0, 0};
    d.b = {12.5, 0};
    d.line_pt = {6, 5};
    REQUIRE(compose_dim_label(d, iso(), {}).line1 == "12,5");
    d.tol.mode = TolMode::Symmetric;
    d.tol.upper = 0.3;
    REQUIRE(compose_dim_label(d, iso(), {}).line1 == "12,5 ±0,3"); // the deviation written alike
}

TEST_CASE("Dimension variables set and read a style's values") {
    DimStyle s;
    REQUIRE(apply_dim_var(s, "DIMTXT", 3.5));
    REQUIRE(s.text_height == 3.5);
    REQUIRE(apply_dim_var(s, "dimasz", 3.0)); // any case
    REQUIRE(s.arrow_size == 3.0);
    REQUIRE(apply_dim_var(s, "DIMDEC", 1));
    REQUIRE_FALSE(apply_dim_var(s, "DIMDEC", 9));
    REQUIRE_FALSE(apply_dim_var(s, "DIMDEC", 1.5));
    REQUIRE(s.precision == 1);
    REQUIRE(apply_dim_var(s, "DIMDSEP", ','));
    REQUIRE(s.decimal_separator == ',');
    REQUIRE_FALSE(apply_dim_var(s, "DIMDSEP", ';'));
    REQUIRE(apply_dim_var(s, "DIMZIN", 12));
    REQUIRE(s.zero_suppression == (kDimZinLeading | kDimZinTrailing));
    REQUIRE(apply_dim_var(s, "DIMBLK", 1));
    REQUIRE(s.arrow_type == 1);
    REQUIRE(apply_dim_var(s, "DIMTAD", 0));
    REQUIRE_FALSE(s.text_above);
    REQUIRE(apply_dim_var(s, "DIMCLRD", 1)); // red: the dimension line and its arrowheads
    REQUIRE_FALSE(s.dim_color.by_layer);
    REQUIRE(s.dim_color.color == Rgb{255, 0, 0});
    REQUIRE(s.arrow_color == s.dim_color);
    REQUIRE(dim_var_value(s, "DIMCLRD") == Approx(1.0));
    REQUIRE(apply_dim_var(s, "DIMCLRT", 256));
    REQUIRE(s.text_color.by_layer);
    REQUIRE(dim_var_value(s, "DIMCLRT") == Approx(256.0));
    REQUIRE_FALSE(apply_dim_var(s, "DIMTXT", 0.0));
    REQUIRE_FALSE(apply_dim_var(s, "DIMNOPE", 1.0));
    REQUIRE_FALSE(dim_var_value(s, "DIMNOPE").has_value());
    for (const char* var : kDimVars) {
        INFO(var);
        REQUIRE(dim_var_value(s, var).has_value());
    }
}

TEST_CASE("The current dimension style: kept valid, never purged from under its dimensions") {
    GeometryStore store;
    store.add_dimstyle(DimStyle{"A"});
    store.add_dimstyle(DimStyle{"B"});
    REQUIRE(store.dimstyle_index("B") == 2);
    REQUIRE(store.dimstyle_index("missing") == 0xFFFF);
    store.set_current_dimstyle(2);
    REQUIRE(store.current_dimstyle() == 2);
    store.set_current_dimstyle(9); // out of range: ignored
    REQUIRE(store.current_dimstyle() == 2);
    REQUIRE_FALSE(store.remove_dimstyle(2)); // the current style stays
    REQUIRE(store.remove_dimstyle(1));       // A goes; B is now 1 and still current
    REQUIRE(store.current_dimstyle() == 1);
    REQUIRE(store.dimstyles()[1].name == "B");
    store.clear();
    REQUIRE(store.current_dimstyle() == 0);
}

TEST_CASE("Dimension styles and the current one survive .musa and DXF") {
    io::Document doc;
    doc.dimstyles = {DimStyle{"Standard"}, iso()};
    doc.current_dimstyle = 1;
    const std::filesystem::path p = std::filesystem::temp_directory_path() / "musacad_dimstyle.musa";
    REQUIRE(io::save_native(doc, p.string()).ok);
    io::Document back;
    REQUIRE(io::load_native(p.string(), back).ok);
    REQUIRE(back.dimstyles.size() == 2);
    REQUIRE(back.dimstyles[1] == iso());
    REQUIRE(back.current_dimstyle == 1);

    const std::filesystem::path q = std::filesystem::temp_directory_path() / "musacad_dimstyle.dxf";
    REQUIRE(io::save_dxf(doc, q.string()).ok);
    io::Document dx;
    REQUIRE(io::load_dxf(q.string(), dx).ok);
    REQUIRE(dx.current_dimstyle < dx.dimstyles.size());
    const DimStyle& got = dx.dimstyles[dx.current_dimstyle];
    REQUIRE(got.name == "ISO-25");
    REQUIRE(got.text_height == Approx(3.5));
    REQUIRE(got.precision == 1);
    REQUIRE(got.decimal_separator == ',');
    REQUIRE(got.zero_suppression == kDimZinTrailing);
    std::filesystem::remove(p);
    std::filesystem::remove(q);
}

TEST_CASE("The -DIMSTYLE command: ?, Restore, Save, Redefine and STatus") {
    Harness h;
    h.proc.set_dim_styles({DimStyle{"Standard"}, iso()}, 0);
    h.run({"-DIMSTYLE"});
    REQUIRE(h.out.said("Current dimension style: Standard"));
    REQUIRE(h.out.prompt == "Enter a dimension style option [Save/Restore/STatus/?] <Restore>: ");
    h.run({"?"});
    REQUIRE(h.out.said("Named dimension styles: Standard ISO-25"));
    h.run({"", "NOPE"}); // Restore (the default) a style that is not there
    REQUIRE(h.out.said("not found"));
    REQUIRE(h.proc.has_active_command());
    h.run({"ISO-25"});
    const auto* cur = h.last<SetCurrentDimStyleCommand>();
    REQUIRE(cur != nullptr);
    REQUIRE(cur->name == "ISO-25");
    REQUIRE_FALSE(cur->save);
    REQUIRE_FALSE(h.proc.has_active_command());

    Harness save;
    save.proc.set_dim_styles({DimStyle{"Standard"}, iso()}, 1);
    save.run({"-DIMSTYLE", "S", "MINE"});
    REQUIRE(save.last<SetCurrentDimStyleCommand>()->save);
    REQUIRE(save.last<SetCurrentDimStyleCommand>()->name == "MINE");
    Harness redefine;
    redefine.proc.set_dim_styles({DimStyle{"Standard"}, iso()}, 1);
    redefine.run({"-DIMSTYLE", "SAVE", "Standard"});
    REQUIRE(redefine.out.prompt == "That name is already in use, redefine it? [Yes/No] <N>: ");
    redefine.run({""}); // no: nothing sent
    REQUIRE(redefine.last<SetCurrentDimStyleCommand>() == nullptr);

    Harness status;
    status.proc.set_dim_styles({DimStyle{"Standard"}, iso()}, 1);
    status.run({"-DIMSTYLE", "ST"});
    REQUIRE(status.out.said("DIMSTYLE  ISO-25"));
    REQUIRE(status.out.said("DIMTXT    3.5000"));
    REQUIRE(status.out.said("DIMDSEP   ,"));

    // DIMSTYLE with no window to show the manager asks at the command line.
    Harness no_view;
    no_view.run({"DIMSTYLE"});
    REQUIRE(no_view.out.prompt == "Enter a dimension style option [Save/Restore/STatus/?] <Restore>: ");
}

TEST_CASE("Dimension variables at the command line; new dimensions in the current style") {
    Harness h;
    h.proc.set_dim_styles({DimStyle{"Standard"}, iso()}, 1);
    h.run({"DIMTXT"});
    REQUIRE(h.out.prompt == "Enter new value for DIMTXT <3.5000>: ");
    h.run({"-1"}); // out of range: said, and asked again
    REQUIRE(h.proc.has_active_command());
    h.run({"5"});
    const auto* v = h.last<SetDimVarCommand>();
    REQUIRE(v != nullptr);
    REQUIRE(v->dimvar == "DIMTXT");
    REQUIRE(v->setting == 5.0);
    h.run({"DIMDSEP", "."});
    REQUIRE(h.last<SetDimVarCommand>()->setting == 46.0);
    h.run({"DIMCLRT", "bylayer"});
    REQUIRE(h.last<SetDimVarCommand>()->setting == 256.0);

    h.run({"DLI", "0,0", "10,0", "5,5"});
    REQUIRE(h.last<AddDimensionCommand>()->style == 1);
    h.run({"DAL", "0,0", "10,10", "0,10"});
    REQUIRE(h.last<AddDimensionCommand>()->style == 1);
}

TEST_CASE("Engine: Set Current, Save, the variables, and Purge leaving the current style") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddDimStyleCommand{iso()});
    engine.submit(SetCurrentDimStyleCommand{"ISO-25", false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.current_dimstyle == 1; }));
    engine.submit(SetDimVarCommand{"DIMTXT", 5.0});
    engine.submit(SetDimVarCommand{"DIMDEC", 3.0}); // the second sees the first: no stale copy
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.dimstyles.size() == 2 && s.dimstyles[1].text_height == 5.0 && s.dimstyles[1].precision == 3;
    }));
    engine.submit(SetCurrentDimStyleCommand{"MINE", true}); // a copy of ISO-25, made current
    REQUIRE(wait_until(engine, [](const auto& s) { return s.current_dimstyle == 2; }));
    engine.consume_snapshot();
    REQUIRE(engine.snapshot().dimstyles[2].name == "MINE");
    REQUIRE(engine.snapshot().dimstyles[2].text_height == 5.0);
    REQUIRE(engine.snapshot().dimstyles[2].decimal_separator == ',');
    engine.submit(SetCurrentDimStyleCommand{"NOPE", false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("not found") != std::string::npos; }));
    // Unused, but current: not a purge candidate. ISO-25 (unused, not current) is.
    engine.submit(SetCursorCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) {
        const auto& c = s.purge.dimstyles;
        return std::find(c.begin(), c.end(), "ISO-25") != c.end() && std::find(c.begin(), c.end(), "MINE") == c.end();
    }));
    engine.stop();
}

TEST_CASE("Engine: Rename and Delete keep Standard, the current style and styles in use") {
    GeometryEngine engine;
    engine.start();
    engine.submit(AddDimStyleCommand{iso()});
    engine.submit(AddDimStyleCommand{DimStyle{"SPARE"}});
    engine.submit(RenameDimStyleCommand{"Standard", "X"});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Standard keeps its name."; }));
    engine.submit(RenameDimStyleCommand{"SPARE", "ISO-25"});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("already a dimension style") != std::string::npos; }));
    engine.submit(RenameDimStyleCommand{"SPARE", "SPARE-2"});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.dimstyles.size() == 3 && s.dimstyles[2].name == "SPARE-2"; }));
    // ISO-25 in use by a dimension: kept. SPARE-2 unused: deleted.
    engine.submit(AddDimensionCommand{.type = 1, .a = {0, 0}, .b = {10, 0}, .line_pt = {5, 5}, .style = 1, .group = 9});
    engine.submit(DeleteDimStyleCommand{"ISO-25"});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("was not deleted") != std::string::npos; }));
    engine.submit(DeleteDimStyleCommand{"SPARE-2"});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.dimstyles.size() == 2; }));
    engine.submit(DeleteDimStyleCommand{"Standard"});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("was not deleted") != std::string::npos; }));
    engine.stop();
}
