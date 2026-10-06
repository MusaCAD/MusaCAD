// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The crash report: when Musa CAD goes down (a fatal signal on Linux and macOS, an
// unhandled exception on Windows, std::terminate anywhere) it writes what it knows to a
// text file -- the version and system, the drawing, the last things done at the command
// line, the call stack -- and the next start offers the file to save and send with an
// issue. Qt-free: the command layer and the tests use it as well as the application.
namespace musacad::core::crash {

/// Install the handlers. The report goes to `dir`/crash-pending-<process id>.txt (the
/// directory is created); `about` opens it as given (version, system, display). Calling it
/// again moves the reports to another directory and replaces `about`.
void install(const std::string& dir, const std::string& about);

/// Replace the opening lines.
void set_about(std::string_view about);

/// The graphics driver, for the report's opening ("" until the window has drawn).
void set_graphics(std::string_view renderer);

/// The drawing open when it happened ("" = none, or not saved yet).
void set_document(std::string_view path);

/// One line of recent activity: a command, a typed value, a pick, a message. The last 96
/// are kept; a report lists them oldest first.
void note(std::string_view line);

/// The recent activity, oldest first.
[[nodiscard]] std::vector<std::string> recent();

/// A report written on request ("Save Bug Report…" with nothing crashed): `about`, the
/// drawing and the recent activity, laid out as a crash report is.
[[nodiscard]] std::string live_report(std::string_view about);

/// Write the report for `sig` (a signal number; 0 = none) to `path` now, as the handler
/// does. Returns false when the file could not be written.
bool write_report(const std::string& path, int sig);

/// A report left by a crash, moved to its permanent name
/// (musacad-crash-<yyyy-mm-dd-hhmmss>.txt, the time of the crash) with its call stack
/// demangled.
struct PendingReport {
    std::string path;
    std::string text;
};
/// The reports crashes left in `dir`, newest last, each renamed so it is offered once.
[[nodiscard]] std::vector<PendingReport> take_pending(const std::string& dir);

/// The time a report records (seconds since 1970, UTC), if it has one.
[[nodiscard]] std::optional<std::int64_t> report_time(std::string_view text);

/// The signal's name and what it means ("SIGSEGV (invalid memory access)").
[[nodiscard]] std::string signal_name(int sig);

/// The call stack's C++ names demangled, where the platform can.
[[nodiscard]] std::string demangle_stack(std::string_view text);

} // namespace musacad::core::crash
