// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace musacad::core {

/// DWGPROPS: the drawing's summary information and custom properties, saved with it.
struct DrawingProps {
    std::string title;
    std::string subject;
    std::string author;
    std::string keywords;
    std::string comments;
    std::string hyperlink_base;
    std::vector<std::pair<std::string, std::string>> custom; ///< name / value
    friend bool operator==(const DrawingProps&, const DrawingProps&) = default;
};

/// TIME / DWGPROPS Statistics: when the drawing was created and last saved (seconds
/// since the Unix epoch, 0 = unknown) and how long it has been edited in total.
struct DrawingTimes {
    std::int64_t created = 0;
    std::int64_t updated = 0;
    double edit_seconds = 0.0;
    friend bool operator==(const DrawingTimes&, const DrawingTimes&) = default;
};

} // namespace musacad::core
