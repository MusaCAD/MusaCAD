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
    // Handles the parent passed in (pipe, redirect, or the console front-end) are already
    // wired to the C streams by the CRT: leave them alone.
    const bool have_out = has_handle(STD_OUTPUT_HANDLE);
    const bool have_err = has_handle(STD_ERROR_HANDLE);
    if (have_out && have_err) {
        return;
    }
    // No streams at all: only a console started us, if anything did.
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
