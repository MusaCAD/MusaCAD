// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Numbers under a locale that writes a decimal comma (#96). QApplication sets the C locale to
// the user's on Unix; with es_CO a clicked point came out as "51,19483418,44,51084184" and
// every pick failed with "Malformed coordinate". The application puts LC_NUMERIC back to "C",
// and the pick is written with a '.' even if something sets the locale again.

#include <clocale>
#include <cstdio>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/text/parse_double.hpp"

using namespace musacad::core;
using musacad::command::CommandOutput;
using musacad::command::CommandProcessor;
using Catch::Approx;

namespace {

/// LC_NUMERIC set to the first locale found that writes a decimal comma, for the scope; the
/// "C" locale again after. Empty when the system has none of them.
struct CommaLocale {
    std::string name;
    CommaLocale() {
        for (const char* n : {"es_CO.UTF-8", "es_CO.utf8", "de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "fr_FR.utf8",
                              "es_ES.UTF-8", "es_ES.utf8", "es_AR.utf8"}) {
            if (std::setlocale(LC_NUMERIC, n) != nullptr) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%.1f", 1.5);
                if (std::string(buf) == "1,5") {
                    name = n;
                    return;
                }
            }
        }
        std::setlocale(LC_NUMERIC, "C");
    }
    ~CommaLocale() { std::setlocale(LC_NUMERIC, "C"); }
    CommaLocale(const CommaLocale&) = delete;
    CommaLocale& operator=(const CommaLocale&) = delete;
};

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

TEST_CASE("#96 numbers are written with a '.' under a decimal-comma locale") {
    const CommaLocale comma;
    if (comma.name.empty()) {
        SKIP("no locale with a decimal comma on this system");
    }
    REQUIRE(format_double_g(51.19483418, 10) == "51.19483418");
    REQUIRE(format_double_g(-0.5, 10) == "-0.5");
    REQUIRE(format_double_g(1e-12, 10) == "1e-12");
    REQUIRE(format_double_g(1234567.0, 4) == "1.235e+06"); // as "%.4g" writes it
    double back = 0.0;
    REQUIRE(parse_double_all(format_double_g(44.51084184, 10), back));
    REQUIRE(back == Approx(44.51084184));
}

TEST_CASE("#96 a picked point reaches the command under a decimal-comma locale") {
    const CommaLocale comma;
    if (comma.name.empty()) {
        SKIP("no locale with a decimal comma on this system");
    }
    INFO(comma.name);
    H h;
    h.proc.submit_line("CIRCLE");
    h.proc.pick_point({51.19483418, 44.51084184}, std::nullopt);
    h.proc.pick_point({54.19483418, 44.51084184}, std::nullopt);
    for (const std::string& l : h.out.lines) {
        INFO(l);
        REQUIRE(l.find("Malformed") == std::string::npos);
    }
    const auto* c = h.last<AddCircleCommand>();
    REQUIRE(c != nullptr);
    REQUIRE(c->center.x == Approx(51.19483418));
    REQUIRE(c->center.y == Approx(44.51084184));
    REQUIRE(c->radius == Approx(3.0));
}
