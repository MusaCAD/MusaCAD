// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <string>

#include "musacad/app/cli.hpp"

namespace musacad::app {

/// `musacad --dump-wiki-data <out.json>`: the facts the documentation is generated from,
/// read from the application itself -- every command with its aliases, description, icon
/// and the transcript it opens with; which of them are system variables; the ribbon, tab by
/// tab, with the command each tool starts; the keyboard shortcuts; the native format's
/// version; the command line's help. Needs a QApplication (the window is built, never
/// shown, to read the ribbon off it). Returns kExitOk, or kExitOutput with `error` set.
[[nodiscard]] int run_dump_wiki_data(const CliOptions& opts, std::string& error);

} // namespace musacad::app
