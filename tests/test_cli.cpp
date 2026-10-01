// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

// The shipped binary's command line (issue #11). The parser is a Qt-free library so
// these assertions run in the normal unit-test binary; the end-to-end exit codes of
// the real executable are asserted separately by tests/cli_check.cmake.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "musacad/app/cli.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/native_format.hpp"

using namespace musacad;
using musacad::app::CliOptions;

namespace {

/// parse_cli takes (argc, argv); build one from a vector of literals.
CliOptions parse(const std::vector<const char*>& args) {
    return app::parse_cli(static_cast<int>(args.size()), args.data());
}

std::filesystem::path temp_file(const std::string& name) {
    return std::filesystem::temp_directory_path() / name;
}

} // namespace

TEST_CASE("parse_cli: bare invocation opens the GUI with no drawing") {
    const CliOptions o = parse({"musacad"});
    REQUIRE(o.error.empty());
    REQUIRE(o.mode == CliOptions::Mode::Gui);
    REQUIRE(o.input.empty());
    REQUIRE(o.qt_args.size() == 1); // argv[0] only
}

TEST_CASE("parse_cli: a positional file is the drawing to open") {
    const CliOptions o = parse({"musacad", "part.musa"});
    REQUIRE(o.error.empty());
    REQUIRE(o.mode == CliOptions::Mode::Gui);
    REQUIRE(o.input == "part.musa");
    REQUIRE_FALSE(o.input_is_dxf);

    const CliOptions d = parse({"musacad", "legacy.DXF"});
    REQUIRE(d.input_is_dxf); // extension detection is case-insensitive
}

TEST_CASE("parse_cli: --help / --version / --check select their modes") {
    REQUIRE(parse({"musacad", "--help"}).mode == CliOptions::Mode::Help);
    REQUIRE(parse({"musacad", "-h"}).mode == CliOptions::Mode::Help);
    REQUIRE(parse({"musacad", "--version"}).mode == CliOptions::Mode::Version);
    REQUIRE(parse({"musacad", "-v"}).mode == CliOptions::Mode::Version);

    const CliOptions c = parse({"musacad", "--check", "part.musa"});
    REQUIRE(c.error.empty());
    REQUIRE(c.mode == CliOptions::Mode::Check);
    REQUIRE(c.input == "part.musa");

    // --help wins over anything else on the line (so a broken line can still ask for help).
    REQUIRE(parse({"musacad", "--check", "--help"}).mode == CliOptions::Mode::Help);
}

TEST_CASE("parse_cli: malformed lines are usage errors, not silent defaults") {
    REQUIRE_FALSE(parse({"musacad", "--check"}).error.empty());              // no file
    REQUIRE_FALSE(parse({"musacad", "--nonsense"}).error.empty());           // unknown option
    REQUIRE_FALSE(parse({"musacad", "a.musa", "b.musa"}).error.empty());     // two drawings
}

TEST_CASE("parse_cli: single-dash options are forwarded to Qt untouched") {
    // Qt owns the single-dash namespace (-platform, -style, -qwindowgeometry…). Rejecting
    // them would break `musacad -platform offscreen`; claiming them would shadow Qt.
    const CliOptions o = parse({"musacad", "-platform", "offscreen", "part.musa"});
    REQUIRE(o.error.empty());
    REQUIRE(o.input == "part.musa");
    REQUIRE(o.qt_args.size() == 3); // argv[0], -platform, offscreen
    REQUIRE(o.qt_args[1] == "-platform");
    REQUIRE(o.qt_args[2] == "offscreen");
}

TEST_CASE("parse_cli: --plot takes a drawing, an output and the sheet options") {
    const CliOptions o = parse({"musacad", "--plot", "part.musa", "out.pdf", "--paper", "A3",
                                "--portrait", "--scale", "1:5"});
    REQUIRE(o.error.empty());
    REQUIRE(o.mode == CliOptions::Mode::Plot);
    REQUIRE(o.input == "part.musa");
    REQUIRE(o.plot.output == "out.pdf");
    REQUIRE(o.plot.paper == "A3");
    REQUIRE_FALSE(o.plot.landscape);
    REQUIRE_FALSE(o.plot.fit); // an explicit ratio turns fit-to-paper off
    REQUIRE(o.plot.scale_num == 1.0);
    REQUIRE(o.plot.scale_den == 5.0);
    REQUIRE(o.plot.area == musacad::app::PlotRequest::Area::Extents); // the default
}

TEST_CASE("parse_cli: --window takes four numbers and normalises the corners") {
    const CliOptions o = parse({"musacad", "--plot", "p.musa", "o.pdf", "--window",
                                "80,60,-20,-10"});
    REQUIRE(o.error.empty());
    REQUIRE(o.plot.area == musacad::app::PlotRequest::Area::Window);
    // Given back-to-front, the corners come out min-first so the area is never negative.
    REQUIRE(o.plot.win[0] == -20.0);
    REQUIRE(o.plot.win[1] == -10.0);
    REQUIRE(o.plot.win[2] == 80.0);
    REQUIRE(o.plot.win[3] == 60.0);
}

TEST_CASE("parse_cli: malformed plot options are usage errors") {
    REQUIRE_FALSE(parse({"musacad", "--plot", "p.musa"}).error.empty());          // no output
    REQUIRE_FALSE(parse({"musacad", "--plot"}).error.empty());                    // nothing
    REQUIRE_FALSE(parse({"musacad", "--plot", "p.musa", "o.pdf", "--scale", "x"}).error.empty());
    REQUIRE_FALSE(parse({"musacad", "--plot", "p.musa", "o.pdf", "--window", "1,2,3"}).error.empty());
    REQUIRE_FALSE(parse({"musacad", "--plot", "p.musa", "o.pdf", "--paper"}).error.empty()); // no value
    REQUIRE_FALSE(parse({"musacad", "--check", "--plot", "a", "b"}).error.empty()); // exclusive
}

TEST_CASE("help/version text is non-empty and names the exit codes") {
    const std::string h = app::help_text();
    REQUIRE(h.find("--check") != std::string::npos);
    REQUIRE(h.find("Exit codes") != std::string::npos);
    REQUIRE(app::version_text().find("Musa CAD") == 0);
}

TEST_CASE("parse_cli: --check takes --json, --lines and --window (#80)") {
    const CliOptions o = parse({"musacad", "--check", "--json", "--lines", "--window", "0,0,420,297", "a.musa"});
    REQUIRE(o.error.empty());
    REQUIRE(o.mode == CliOptions::Mode::Check);
    REQUIRE(o.check_json);
    REQUIRE(o.check_lines);
    REQUIRE(o.plot.area == musacad::app::PlotRequest::Area::Window);
    REQUIRE(o.plot.win[2] == 420.0);
    // They mean nothing without --check.
    REQUIRE_FALSE(parse({"musacad", "--json", "a.musa"}).error.empty());
    REQUIRE_FALSE(parse({"musacad", "--plot", "a.musa", "o.pdf", "--lines"}).error.empty());
}

TEST_CASE("check_drawing reports text problems and exits 4; a clean drawing exits 0 (#80)") {
    core::io::Document doc;
    doc.layers.push_back(core::Layer{"0", {255, 255, 255}, core::Linetype::Continuous, 25, true, false, false});
    core::io::DocText a;
    a.pos = {0, 0};
    a.height = 2.5;
    a.content = "R1 750";
    core::io::DocText b = a;
    b.pos = {4, 0.5};
    b.content = "C3";
    doc.texts = {a, b};
    const std::filesystem::path bad = temp_file("musacad_cli_text_overlap.musa");
    REQUIRE(core::io::save_native(doc, bad.string()).ok);
    CliOptions o = parse({"musacad", "--check", bad.string().c_str()});
    REQUIRE(o.error.empty());
    std::string out;
    std::string err;
    REQUIRE(app::check_drawing(o, out, err) == app::kExitProblems);
    REQUIRE(out.find("overlap: TEXT \"R1 750\"") != std::string::npos);
    REQUIRE(out.find("2 texts checked, 1 problem") != std::string::npos);
    o.check_json = true;
    REQUIRE(app::check_drawing(o, out, err) == app::kExitProblems);
    REQUIRE(out.find("\"kind\": \"overlap\"") != std::string::npos);

    doc.texts = {a};
    const std::filesystem::path good = temp_file("musacad_cli_text_clean.musa");
    REQUIRE(core::io::save_native(doc, good.string()).ok);
    const CliOptions g = parse({"musacad", "--check", good.string().c_str()});
    REQUIRE(app::check_drawing(g, out, err) == app::kExitOk);
    REQUIRE(out.find("no problems") != std::string::npos);

    const CliOptions missing = parse({"musacad", "--check", temp_file("musacad_cli_absent2.musa").string().c_str()});
    REQUIRE(app::check_drawing(missing, out, err) == app::kExitLoad);
    REQUIRE_FALSE(err.empty());
    std::error_code ec;
    std::filesystem::remove(bad, ec);
    std::filesystem::remove(good, ec);
}

TEST_CASE("check_drawing accepts a real serialized drawing and rejects a broken one") {
    // A genuine document through the real writer -- not a hand-typed fixture, so this
    // cannot rot when the format version bumps.
    core::io::Document doc;
    doc.layers.push_back(core::Layer{"0", {255, 255, 255}, core::Linetype::Continuous, 25, true,
                                     false, false});
    doc.lines.push_back(core::io::DocLine{{0.0, 0.0}, {100.0, 0.0}, {}});
    doc.lines.push_back(core::io::DocLine{{100.0, 0.0}, {100.0, 50.0}, {}});

    const std::filesystem::path good = temp_file("musacad_cli_good.musa");
    {
        std::ofstream out(good, std::ios::binary);
        out << core::io::serialize_native(doc);
    }

    std::string message;
    REQUIRE(app::check_drawing(good.string(), false, message) == app::kExitOk);
    REQUIRE_FALSE(message.empty()); // the loader reports what it read

    const std::filesystem::path bad = temp_file("musacad_cli_bad.musa");
    {
        std::ofstream out(bad, std::ios::binary);
        out << "MUSACAD 14\nLINE not a number\nEND\n";
    }
    REQUIRE(app::check_drawing(bad.string(), false, message) == app::kExitLoad);
    REQUIRE_FALSE(message.empty()); // and says why

    // A missing file is a load failure, not a crash or a silent success.
    REQUIRE(app::check_drawing(temp_file("musacad_cli_absent.musa").string(), false, message) ==
            app::kExitLoad);

    std::error_code ec;
    std::filesystem::remove(good, ec);
    std::filesystem::remove(bad, ec);
}
