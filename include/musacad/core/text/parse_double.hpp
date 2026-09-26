// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
#pragma once

#include <charconv>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <system_error>
#include <version>

// MUSACAD_FORCE_PORTABLE_SHIMS: compile the fallback on a toolchain that has from_chars
// too (the shim self-test does this on Linux, where Apple's path is otherwise unbuilt).
#if defined(MUSACAD_FORCE_PORTABLE_SHIMS) || !(defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L)
#define MUSACAD_PARSE_DOUBLE_FALLBACK 1
#include <clocale>
#include <cstdlib>
#if defined(__APPLE__)
#include <xlocale.h>
#endif
#endif

namespace musacad::core {

/// Where parse_double stopped and whether it read a number.
struct ParsedDouble {
    const char* ptr = nullptr;
    bool ok = false;
};

/// Parses a decimal number from [begin, end) the way std::from_chars does: no leading
/// whitespace or '+', the C locale's '.', `ptr` at the first character not consumed.
/// libc++ before version 20 (Apple's toolchains included) has no floating-point
/// from_chars; there the parse goes through strtod_l in the C locale on a bounded copy,
/// so the application's locale (QApplication sets one) never changes what a file means.
inline ParsedDouble parse_double(const char* begin, const char* end, double& out) noexcept {
#if !defined(MUSACAD_PARSE_DOUBLE_FALLBACK)
    const auto [ptr, ec] = std::from_chars(begin, end, out);
    return ParsedDouble{ptr, ec == std::errc{}};
#else
    if (begin == end || *begin == '+' || *begin == ' ' || *begin == '\t') {
        return ParsedDouble{begin, false};
    }
#if defined(_MSC_VER)
    // The MSVC CRT spells the per-call locale API with a leading underscore.
    static const _locale_t c_locale = _create_locale(LC_ALL, "C");
#else
    static const locale_t c_locale = newlocale(LC_ALL_MASK, "C", static_cast<locale_t>(nullptr));
#endif
    char small[96];
    std::string big;
    const char* text = nullptr;
    const std::size_t n = static_cast<std::size_t>(end - begin);
    if (n < sizeof(small)) {
        std::memcpy(small, begin, n);
        small[n] = '\0';
        text = small;
    } else {
        big.assign(begin, n);
        text = big.c_str();
    }
    char* stop = nullptr;
#if defined(_MSC_VER)
    const double v = _strtod_l(text, &stop, c_locale);
#else
    const double v = strtod_l(text, &stop, c_locale);
#endif
    if (stop == text) {
        return ParsedDouble{begin, false};
    }
    out = v;
    return ParsedDouble{begin + (stop - text), true};
#endif
}

/// The whole of `s` as a number, or false.
inline bool parse_double_all(std::string_view s, double& out) noexcept {
    const ParsedDouble r = parse_double(s.data(), s.data() + s.size(), out);
    return r.ok && r.ptr == s.data() + s.size();
}

} // namespace musacad::core
