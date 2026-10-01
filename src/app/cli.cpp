// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/app/cli.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/native_kernel_2d.hpp"
#include "musacad/core/text/text_codes.hpp"
#include "musacad/core/text_check.hpp"
#include "musacad/core/version.hpp"

namespace musacad::app {

namespace {

/// Options we claim. Everything else that starts with a SINGLE dash belongs to
/// Qt (`-platform`, `-style`, `-qwindowgeometry`, …) and is forwarded untouched;
/// an unknown DOUBLE-dash option is a usage error, because Qt's are single-dash.
bool is_ours(std::string_view a) {
    return a == "--help" || a == "-h" || a == "--version" || a == "-v" || a == "--check" ||
           a == "--plot" || a == "--paper" || a == "--portrait" || a == "--landscape" ||
           a == "--scale" || a == "--fit" || a == "--window" || a == "--extents" ||
           a == "--monochrome" || a == "--json" || a == "--lines";
}

/// Parses "1:5" (or "1/5", or a bare "0.2") into plotted-mm per drawing-unit.
/// Returns false on anything that would not produce a usable scale.
bool parse_scale(std::string_view s, double& num, double& den) {
    const std::size_t sep = s.find_first_of(":/");
    const std::string a(sep == std::string_view::npos ? s : s.substr(0, sep));
    const std::string b(sep == std::string_view::npos ? "1" : std::string(s.substr(sep + 1)));
    try {
        num = std::stod(a);
        den = std::stod(b);
    } catch (...) {
        return false;
    }
    return num > 0.0 && den > 0.0;
}

/// Parses "x0,y0,x1,y1" into `out`, normalising the corners so min < max.
bool parse_window(std::string_view s, double (&out)[4]) {
    std::size_t start = 0;
    for (int i = 0; i < 4; ++i) {
        if (start > s.size()) {
            return false;
        }
        const std::size_t comma = s.find(',', start);
        const std::string tok(s.substr(start, comma == std::string_view::npos
                                                  ? std::string_view::npos
                                                  : comma - start));
        if (tok.empty() || (comma == std::string_view::npos && i != 3)) {
            return false;
        }
        try {
            out[i] = std::stod(tok);
        } catch (...) {
            return false;
        }
        start = comma == std::string_view::npos ? s.size() + 1 : comma + 1;
    }
    if (out[0] > out[2]) {
        std::swap(out[0], out[2]);
    }
    if (out[1] > out[3]) {
        std::swap(out[1], out[3]);
    }
    return true;
}

/// The Qt options that consume a FOLLOWING value (per the Qt 6 QGuiApplication /
/// QApplication documented command line). We must know these, or `musacad -platform
/// offscreen part.musa` would read "offscreen" as the drawing. Anything else starting
/// with a single dash is treated as a valueless flag and forwarded as-is.
bool qt_option_takes_value(std::string_view a) {
    constexpr std::string_view kWithValue[] = {
        "-display",      "-geometry",         "-graphicssystem",  "-platform",
        "-platformpluginpath", "-platformtheme", "-plugin",       "-qmljsdebugger",
        "-qwindowgeometry",    "-qwindowicon",   "-qwindowtitle", "-session",
        "-style",        "-stylesheet",
    };
    for (const std::string_view k : kWithValue) {
        if (a == k) {
            return true;
        }
    }
    return false;
}

} // namespace

bool path_is_dxf(const std::string& path) {
    if (path.size() < 4) {
        return false;
    }
    std::string tail = path.substr(path.size() - 4);
    std::transform(tail.begin(), tail.end(), tail.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return tail == ".dxf";
}

CliOptions parse_cli(int argc, const char* const* argv) {
    CliOptions o;
    if (argc > 0 && argv != nullptr && argv[0] != nullptr) {
        o.qt_args.emplace_back(argv[0]);
    }
    bool want_check = false;
    bool want_plot = false;
    bool have_input = false;
    // --plot takes TWO positionals (drawing, output); the second fills o.plot.output.
    const auto need_value = [&](int& i, std::string_view opt, std::string_view& value) {
        if (i + 1 >= argc || argv[i + 1] == nullptr) {
            o.error = std::string(opt) + " needs a value";
            return false;
        }
        value = argv[++i];
        return true;
    };

    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i] != nullptr ? argv[i] : "";
        if (a == "--help" || a == "-h") {
            o.mode = CliOptions::Mode::Help;
            return o; // an explicit help request wins over anything else on the line
        }
        if (a == "--version" || a == "-v") {
            o.mode = CliOptions::Mode::Version;
            return o;
        }
        if (a == "--check") {
            want_check = true;
            continue;
        }
        if (a == "--plot") {
            want_plot = true;
            continue;
        }
        if (a == "--portrait" || a == "--landscape") {
            o.plot.landscape = a == "--landscape";
            continue;
        }
        if (a == "--extents") {
            o.plot.area = PlotRequest::Area::Extents;
            continue;
        }
        if (a == "--fit") {
            o.plot.fit = true;
            continue;
        }
        if (a == "--monochrome") {
            o.plot.monochrome = true;
            continue;
        }
        if (a == "--json") {
            o.check_json = true;
            continue;
        }
        if (a == "--lines") {
            o.check_lines = true;
            continue;
        }
        if (a == "--paper") {
            std::string_view v;
            if (!need_value(i, a, v)) {
                return o;
            }
            o.plot.paper = std::string(v);
            continue;
        }
        if (a == "--scale") {
            std::string_view v;
            if (!need_value(i, a, v)) {
                return o;
            }
            if (!parse_scale(v, o.plot.scale_num, o.plot.scale_den)) {
                o.error = "--scale wants a ratio like 1:5 (got \"" + std::string(v) + "\")";
                return o;
            }
            o.plot.fit = false; // an explicit ratio means "not fit-to-paper"
            continue;
        }
        if (a == "--window") {
            std::string_view v;
            if (!need_value(i, a, v)) {
                return o;
            }
            if (!parse_window(v, o.plot.win)) {
                o.error = "--window wants x0,y0,x1,y1 (got \"" + std::string(v) + "\")";
                return o;
            }
            o.plot.area = PlotRequest::Area::Window;
            continue;
        }
        if (a.size() > 2 && a.substr(0, 2) == "--" && !is_ours(a)) {
            o.error = "unknown option: " + std::string(a);
            return o;
        }
        if (!a.empty() && a[0] == '-' && a != "-") {
            // A Qt option; carry it -- and its value, when it takes one -- across untouched,
            // so the value is never mistaken for the drawing argument.
            o.qt_args.emplace_back(a);
            if (qt_option_takes_value(a) && i + 1 < argc && argv[i + 1] != nullptr) {
                o.qt_args.emplace_back(argv[i + 1]);
                ++i;
            }
            continue;
        }
        if (!have_input) {
            o.input = std::string(a);
            have_input = true;
            continue;
        }
        if (want_plot && o.plot.output.empty()) {
            o.plot.output = std::string(a); // --plot's second positional is the destination
            continue;
        }
        o.error = "unexpected extra argument: " + std::string(a);
        return o;
    }

    if (want_check && want_plot) {
        o.error = "--check and --plot are mutually exclusive";
        return o;
    }
    if ((o.check_json || o.check_lines) && !want_check) {
        o.error = std::string(o.check_json ? "--json" : "--lines") + " goes with --check";
        return o;
    }
    if (want_check) {
        if (!have_input) {
            o.error = "--check needs a drawing file";
            return o;
        }
        o.mode = CliOptions::Mode::Check;
    }
    if (want_plot) {
        if (!have_input || o.plot.output.empty()) {
            o.error = "--plot needs a drawing file and an output PDF";
            return o;
        }
        o.mode = CliOptions::Mode::Plot;
    }
    o.input_is_dxf = path_is_dxf(o.input);
    return o;
}

std::string help_text() {
    return "Musa CAD -- 2D CAD engine\n"
           "\n"
           "Usage:\n"
           "  musacad [<drawing>]              open a drawing in the GUI\n"
           "  musacad --check <drawing> [check options]\n"
           "                                   read a drawing, check its text, report, exit\n"
           "  musacad --plot <drawing> <out.pdf> [plot options]\n"
           "                                   plot to PDF headlessly (no display needed)\n"
           "  musacad --help                   show this help\n"
           "  musacad --version                show the version\n"
           "\n"
           "<drawing> is a .musa file, or a .dxf file (loaded through the DXF importer).\n"
           "\n"
           "Plot options:\n"
           "  --paper <name>    sheet: A4 A3 A2 A1 A0 Letter Tabloid   (default A4)\n"
           "  --landscape       sheet orientation                      (default)\n"
           "  --portrait        sheet orientation\n"
           "  --extents         plot the drawing's extents             (default)\n"
           "  --window x0,y0,x1,y1   plot an explicit world rectangle\n"
           "  --fit             scale the area to fill the sheet       (default)\n"
           "  --scale <n:m>     plot n mm per m drawing units, e.g. 1:5\n"
           "  --monochrome      plot everything black (the mono CTB style)\n"
           "\n"
           "Check options:\n"
           "  --window x0,y0,x1,y1   the frame text must stay inside (default: the\n"
           "                    drawing's largest rectangle)\n"
           "  --lines           also report text that a line crosses\n"
           "  --json            print the report as JSON\n"
           "\n"
           "Exit codes:\n"
           "  0  success\n"
           "  1  usage error (bad command line)\n"
           "  2  the drawing could not be read or parsed\n"
           "  3  the output could not be written\n"
           "  4  --check: text overlaps text, leaves the frame, crosses a line (--lines)\n"
           "     or uses a character the font has no glyph for\n"
           "\n"
           "Single-dash options are passed through to Qt (e.g. -platform offscreen).\n";
}

std::string version_text() {
    return std::string(core::app_name()) + " " + std::string(core::version_string());
}

int check_drawing(const std::string& path, bool dxf, std::string& message) {
    // The SAME loaders GeometryEngine::load_document_replace calls -- one parse
    // path, so `--check` can never disagree with what opening the file would do.
    core::io::Document doc;
    const core::io::IoResult r =
        dxf ? core::io::load_dxf(path, doc) : core::io::load_native(path, doc);
    message = r.message;
    return r.ok ? kExitOk : kExitLoad;
}

int check_drawing(const CliOptions& o, std::string& out, std::string& err) {
    core::io::Document doc;
    const core::io::IoResult r = o.input_is_dxf ? core::io::load_dxf(o.input, doc) : core::io::load_native(o.input, doc);
    if (!r.ok) {
        err = o.input + ": " + r.message + "\n";
        return kExitLoad;
    }
    // The store as --plot builds it (with no font engine: the command line has no Qt, so
    // letters are measured with the stroke font), FIELD values resolved for this file.
    core::GeometryStore store;
    core::io::populate_store(store, doc);
    core::text::set_field_context(core::text::make_field_context(o.input));
    core::NativeKernel2D kernel;
    core::TextCheckOptions options;
    options.lines = o.check_lines;
    if (o.plot.area == PlotRequest::Area::Window) {
        options.window = std::pair<core::Vec2, core::Vec2>{{o.plot.win[0], o.plot.win[1]}, {o.plot.win[2], o.plot.win[3]}};
    }
    const core::TextCheckReport report = core::check_text(store, kernel, options);
    if (o.check_json) {
        out = core::text_check_json(report, o.input);
    } else {
        out = o.input + ": " + r.message + "\n" + core::text_check_text(report, o.input);
        out += o.input + ": " + std::to_string(report.runs.size()) + (report.runs.size() == 1 ? " text" : " texts") +
               " checked, " +
               (report.problems.empty() ? std::string("no problems")
                                        : std::to_string(report.problems.size()) +
                                              (report.problems.size() == 1 ? " problem" : " problems")) +
               "\n";
    }
    return report.problems.empty() ? kExitOk : kExitProblems;
}

} // namespace musacad::app
