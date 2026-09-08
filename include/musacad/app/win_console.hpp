// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

namespace musacad::app {

/// Windows only. musacad_app.exe is a windowed program (GUI subsystem), so launching it
/// from the Start menu or Explorer opens no console window -- but a windowed program
/// started from a console gets no standard streams, and --help / --version / --check /
/// --plot would print nothing. This attaches the process to the parent's console and
/// rebinds stdout / stderr to it. Streams the parent already handed over (a pipe, a
/// `> file` redirect, or the musacad.exe console front-end) are left untouched, so
/// redirection keeps working. A no-op when there is no parent console, and on other
/// platforms.
#ifdef _WIN32
void attach_parent_console();
#else
inline void attach_parent_console() {}
#endif

} // namespace musacad::app
