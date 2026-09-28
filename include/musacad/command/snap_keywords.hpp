// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "musacad/core/snap.hpp"

namespace musacad::command {

/// One object snap mode as the command line knows it: AutoCAD's three-letter keyword, the
/// name shown in lists and messages, the word that joins it to the prompt ("of" for a
/// point OF an object, "to" for a point found relative TO one) and its OSMODE bit.
struct SnapKeyword {
    const char* code;
    const char* name;
    core::SnapType type;
    const char* joiner;
    int osmode;
};

/// Every mode, in AutoCAD's OSMODE bit order.
[[nodiscard]] std::span<const SnapKeyword> snap_keywords() noexcept;

/// A list of modes as typed ("END,MID", "endp", "int cen", "NON", "ALL"): the mask of
/// their bits. NON / NONE / OFF clear it. `label` takes the names found, for messages
/// ("Endpoint" or "Endpoint, Midpoint"), and `joiner` the first mode's joining word.
/// False (with `unknown` set) when a word is no mode, so a caller can leave the text to
/// someone else.
bool parse_snap_list(std::string_view text, std::uint32_t& mask, std::string* label = nullptr,
                     std::string* joiner = nullptr, std::string* unknown = nullptr);

/// The modes of a mask by name ("Endpoint, Center"), or "none".
[[nodiscard]] std::string snap_list(std::uint32_t mask);
/// ... and by keyword, as -OSNAP offers its default ("End,Cen,Int,Ext"), or "None".
[[nodiscard]] std::string snap_codes(std::uint32_t mask);

/// OSMODE <-> the running snap mask (bit 16384, "osnaps off", is not part of the mask).
[[nodiscard]] int osmode_of(std::uint32_t mask) noexcept;
[[nodiscard]] std::uint32_t mask_of_osmode(int osmode) noexcept;

} // namespace musacad::command
