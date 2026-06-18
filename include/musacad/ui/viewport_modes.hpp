// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <atomic>
#include <cstdint>

#include "musacad/core/snap.hpp"

namespace musacad::ui {

/// Drawing-aid toggles shared between the GUI thread (input) and the viewport's
/// render thread (grid visibility, snap aperture). Atomic so either thread reads
/// them without locking.
struct ViewportModes {
    std::atomic<bool> osnap{true};  // F3 - object snap
    std::atomic<bool> grid{true};   // F7 - grid display
    std::atomic<bool> ortho{false}; // F8 - ortho constraint
    std::atomic<bool> snap{false};  // F9 - grid snap
    std::atomic<bool> polar{false}; // F10 - polar tracking
    std::atomic<std::uint32_t> snap_mask{core::kAllSnaps}; // running-osnap type mask
};

} // namespace musacad::ui
