// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/app/win_gpu.hpp"

#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// Read by the NVIDIA and AMD drivers when the process starts: on a machine with two GPUs, the
// dedicated one draws. They must be exported from the executable itself (not a DLL).
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

namespace musacad::app {

void prefer_high_performance_gpu() noexcept {
    // The full path of musacad_app.exe: the name Windows keeps the preference under.
    std::wstring exe(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
        if (n == 0) {
            return;
        }
        if (n < exe.size()) {
            exe.resize(n);
            break;
        }
        if (exe.size() >= 32768) {
            return;
        }
        exe.resize(exe.size() * 2);
    }
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectX\\UserGpuPreferences", 0, nullptr, 0,
                        KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return;
    }
    // Only when there is no preference yet: one chosen in Settings (even "Let Windows decide",
    // which is stored as GpuPreference=0;) is kept.
    if (RegQueryValueExW(key, exe.c_str(), nullptr, nullptr, nullptr, nullptr) == ERROR_FILE_NOT_FOUND) {
        static const wchar_t kHighPerformance[] = L"GpuPreference=2;";
        RegSetValueExW(key, exe.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(kHighPerformance),
                       sizeof(kHighPerformance));
    }
    RegCloseKey(key);
}

} // namespace musacad::app
