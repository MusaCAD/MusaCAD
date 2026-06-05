#pragma once

#include <atomic>

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
};

} // namespace musacad::ui
