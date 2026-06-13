// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

#include <array>
#include <cstddef>

namespace musacad::render {

/// Rolling frame-time / FPS tracker for the viewport overlay. Pure CPU; fed one
/// frame duration per presented frame.
class FrameStats {
public:
    void add_frame(double seconds) noexcept;
    void reset() noexcept;

    [[nodiscard]] double average_frame_seconds() const noexcept;
    [[nodiscard]] double average_frame_ms() const noexcept { return average_frame_seconds() * 1000.0; }
    [[nodiscard]] double fps() const noexcept;
    [[nodiscard]] double max_frame_ms() const noexcept;
    [[nodiscard]] std::size_t sample_count() const noexcept { return count_; }

private:
    static constexpr std::size_t kWindow = 120;
    std::array<double, kWindow> samples_{};
    std::size_t head_ = 0;
    std::size_t count_ = 0;
    double sum_ = 0.0;
};

} // namespace musacad::render
