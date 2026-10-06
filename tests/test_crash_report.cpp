// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// The crash report: the recent activity it keeps, the report it writes, and the report a
// crash leaves for the next start.

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "musacad/core/crash_report.hpp"

#if !defined(_WIN32)
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace crash = musacad::core::crash;
namespace fs = std::filesystem;

namespace {
fs::path scratch_dir(const std::string& name) {
    const fs::path d = fs::temp_directory_path() /
                       ("musacad-crash-test-" + name + "-" +
                        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(d);
    return d;
}

std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
} // namespace

TEST_CASE("Crash report: the recent activity keeps the last 96 lines, oldest first, a line each") {
    for (int i = 0; i < 120; ++i) {
        crash::note("line " + std::to_string(i));
    }
    crash::note("first\nsecond\r\n\n   ");
    const auto r = crash::recent();
    REQUIRE(r.size() == 96);
    REQUIRE(r.back() == "second");
    REQUIRE(r[r.size() - 2] == "first");
    REQUIRE(r.front() == "line 26");
    crash::note(std::string(500, 'x')); // a long line is cut, not dropped
    REQUIRE(crash::recent().back().size() == 200);
}

TEST_CASE("Crash report: a report on request carries the system, the drawing and the activity") {
    crash::set_document("/drawings/bracket.musa, 1 drawing open");
    crash::note("Command: LINE");
    crash::note("> 0,0");
    const std::string r = crash::live_report("Musa CAD 9.9.9\nSystem: Test OS\n");
    REQUIRE(r.rfind("Musa CAD bug report", 0) == 0);
    REQUIRE(r.find("System: Test OS") != std::string::npos);
    REQUIRE(r.find("nothing crashed") != std::string::npos);
    REQUIRE(r.find("Drawing: /drawings/bracket.musa, 1 drawing open") != std::string::npos);
    REQUIRE(r.find("  Command: LINE\n  > 0,0\n") != std::string::npos);
    REQUIRE(crash::report_time(r).has_value());
}

TEST_CASE("Crash report: the report a crash writes, and how the next start takes it") {
    const fs::path dir = scratch_dir("write");
    crash::set_document("");
    crash::note("Command: TRIM");
    const fs::path pending = dir / "crash-pending-1234.txt";
    REQUIRE(crash::write_report(pending.string(), SIGSEGV));
    const std::string raw = read(pending);
    REQUIRE(raw.rfind("Musa CAD crash report", 0) == 0);
    REQUIRE(raw.find("What happened: SIGSEGV (invalid memory access)") != std::string::npos);
    REQUIRE(raw.find("Drawing: (none, or not saved yet)") != std::string::npos);
    REQUIRE(raw.find("  Command: TRIM\n") != std::string::npos);
    REQUIRE(raw.find("Call stack:") != std::string::npos);
    const auto t = crash::report_time(raw);
    REQUIRE(t.has_value());

    const auto taken = crash::take_pending(dir.string());
    REQUIRE(taken.size() == 1);
    REQUIRE_FALSE(fs::exists(pending)); // offered once
    REQUIRE(fs::exists(taken[0].path));
    REQUIRE(fs::path(taken[0].path).filename().string().rfind("musacad-crash-", 0) == 0);
    REQUIRE(taken[0].text.find(" local time, " + std::to_string(*t) + " seconds since") != std::string::npos);
    REQUIRE(crash::report_time(taken[0].text) == t); // still readable by a program
    REQUIRE(crash::take_pending(dir.string()).empty());
    fs::remove_all(dir);
}

TEST_CASE("Crash report: signal names and report times") {
    REQUIRE(crash::signal_name(SIGABRT).rfind("SIGABRT", 0) == 0);
    REQUIRE(crash::signal_name(12345) == "signal 12345");
    REQUIRE(crash::report_time("x\nTime: 1759700000 seconds since 1970-01-01 UTC\n") == 1759700000);
    REQUIRE(crash::report_time("Time: 2026-10-06 01:02:03 local time, 1759700000 seconds since 1970-01-01 UTC") ==
            1759700000);
    REQUIRE(crash::report_time("no time here") == std::nullopt);
    REQUIRE(crash::report_time("Overtime: 5 seconds since 1970-01-01 UTC") == std::nullopt); // a line of its own
}

#if !defined(_MSC_VER)
TEST_CASE("Crash report: the call stack's C++ names are demangled") {
    const std::string s = crash::demangle_stack(
        "./musacad_app(_ZN7musacad4core5crash4noteESt17basic_string_viewIcSt11char_traitsIcEE+0x1a) [0x55d0]\n"
        "./musacad_app(+0x1234) [0x55d1]\nkeep_Zthis\n");
    REQUIRE(s.find("musacad::core::crash::note(") != std::string::npos);
    REQUIRE(s.find("+0x1a) [0x55d0]") != std::string::npos);
    REQUIRE(s.find("(+0x1234)") != std::string::npos);
    REQUIRE(s.find("keep_Zthis") != std::string::npos); // not the start of a name
}
#endif

#if !defined(_WIN32)
namespace {
void throw_boom() { throw std::runtime_error("boom in the geometry thread"); }
void (*volatile g_thrower)() = &throw_boom;
[[noreturn]] void die_with_unhandled_exception() noexcept {
    g_thrower(); // out of a noexcept function: std::terminate
    std::_Exit(3);
}
} // namespace

TEST_CASE("Crash report: an unhandled exception leaves a report and the usual end") {
    const fs::path dir = scratch_dir("fork");
    const pid_t child = ::fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        const rlimit no_core{0, 0};
        (void)::setrlimit(RLIMIT_CORE, &no_core); // the crash is the point; no core file
        crash::install(dir.string(), "Musa CAD test child\n");
        crash::note("Command: OFFSET");
        die_with_unhandled_exception();
    }
    int status = 0;
    REQUIRE(::waitpid(child, &status, 0) == child);
    REQUIRE(WIFSIGNALED(status));
    REQUIRE(WTERMSIG(status) == SIGABRT);
    const fs::path pending = dir / ("crash-pending-" + std::to_string(child) + ".txt");
    REQUIRE(fs::exists(pending));
    const std::string r = read(pending);
    REQUIRE(r.find("Musa CAD test child") != std::string::npos);
    REQUIRE(r.find("What happened: SIGABRT") != std::string::npos);
    REQUIRE(r.find("Note: Unhandled exception: boom in the geometry thread") != std::string::npos);
    REQUIRE(r.find("  Command: OFFSET\n") != std::string::npos);
    REQUIRE(r.find("Call stack:") != std::string::npos);
    fs::remove_all(dir);
}
#endif
