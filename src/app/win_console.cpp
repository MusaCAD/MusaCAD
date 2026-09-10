// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/app/win_console.hpp"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>

namespace musacad::app {

namespace {
bool has_handle(DWORD which) {
    const HANDLE h = GetStdHandle(which);
    return h != nullptr && h != INVALID_HANDLE_VALUE;
}
} // namespace

void attach_parent_console() {
    // Streams the parent passed in (a pipe, a `> file` redirect, or the musacad.exe
    // front-end) are already wired to the C streams by the CRT and must stay as they are.
    const bool have_out = has_handle(STD_OUTPUT_HANDLE);
    const bool have_err = has_handle(STD_ERROR_HANDLE);
    // Attach whenever the parent has a console, even when it handed the streams over
    // already: being attached is what makes a Qt start-up failure ("no platform plugin")
    // a message on stderr and a non-zero exit, instead of a modal message box that a
    // script can never click away -- and what routes Ctrl+C to us. Fails harmlessly when
    // there is no console (Start menu, Explorer).
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        return;
    }
    FILE* f = nullptr;
    if (!have_out) {
        (void)freopen_s(&f, "CONOUT$", "w", stdout);
    }
    if (!have_err) {
        (void)freopen_s(&f, "CONOUT$", "w", stderr);
    }
}

} // namespace musacad::app

#endif // _WIN32
