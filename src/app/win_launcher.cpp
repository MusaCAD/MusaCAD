// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

// musacad.exe -- the console front-end for musacad_app.exe on Windows.
//
// musacad_app.exe is a windowed program, so that the Start menu and Explorer open it
// without a console window. But cmd.exe and PowerShell do not wait for a windowed
// program: `musacad_app --check part.musa && echo ok` returns before the check has run,
// and a script can never see the exit code docs/CLI.md promises. This small console
// program runs musacad_app.exe from its own directory with the same command line,
// hands over the console (and any redirections), waits, and exits with the
// application's exit code -- so `musacad --check`, `--plot`, `--help` and `--version`
// behave in every Windows shell exactly as they do on Linux.
//
// Deliberately Win32-only and Qt-free: it must start instantly and carry no DLLs.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cwchar>
#include <string>

namespace {

// The command line minus its first token (our own program name), quotes preserved.
std::wstring arguments_after_program(const wchar_t* cmdline) {
    const wchar_t* p = cmdline;
    if (*p == L'"') {
        ++p;
        while (*p != L'\0' && *p != L'"') {
            ++p;
        }
        if (*p == L'"') {
            ++p;
        }
    } else {
        while (*p != L'\0' && *p != L' ' && *p != L'\t') {
            ++p;
        }
    }
    while (*p == L' ' || *p == L'\t') {
        ++p;
    }
    return std::wstring(p);
}

} // namespace

int wmain() {
    // musacad_app.exe lives next to this launcher.
    wchar_t self[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, self, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        std::fputs("musacad: cannot locate the application directory.\n", stderr);
        return 1;
    }
    std::wstring app(self, n);
    const std::size_t slash = app.find_last_of(L"\\/");
    app.erase(slash == std::wstring::npos ? 0 : slash + 1);
    app += L"musacad_app.exe";

    std::wstring cmdline = L"\"" + app + L"\"";
    const std::wstring rest = arguments_after_program(GetCommandLineW());
    if (!rest.empty()) {
        cmdline += L' ';
        cmdline += rest;
    }

    // The console's standard handles are passed down explicitly: a windowed child is not
    // attached to our console by CreateProcess, but with these it prints and reads through
    // it -- and a `> file` or `| more` on our command line reaches it unchanged.
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(app.c_str(), cmdline.data(), nullptr, nullptr, TRUE, 0, nullptr,
                        nullptr, &si, &pi)) {
        std::fwprintf(stderr, L"musacad: cannot start \"%ls\" (error %lu).\n", app.c_str(),
                      GetLastError());
        return 1;
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    if (!GetExitCodeProcess(pi.hProcess, &code)) {
        code = 1;
    }
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}
