// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <string>

namespace musacad::core {

/// One attribute of a block definition as the command layer needs it: INSERT prompts
/// for a value per attribute (`prompt`, falling back to the tag, with `def` offered),
/// skipping Constant and Preset ones. Published with the block names in the snapshot.
struct BlockAttDefInfo {
    std::string tag;
    std::string prompt;
    std::string def;
    std::uint8_t flags = 0; ///< kAttInvisible | kAttConstant | kAttVerify | kAttPreset
    double height = 2.5;    ///< text height of the value (BATTMAN's text option)
};

} // namespace musacad::core
