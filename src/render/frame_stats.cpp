// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#include "musacad/render/frame_stats.hpp"

namespace musacad::render {

void FrameStats::add_frame(double seconds) noexcept {
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    if (count_ == kWindow) {
        sum_ -= samples_[head_];
    } else {
        ++count_;
    }
    samples_[head_] = seconds;
    sum_ += seconds;
    head_ = (head_ + 1) % kWindow;
}

void FrameStats::reset() noexcept {
    head_ = 0;
    count_ = 0;
    sum_ = 0.0;
}

double FrameStats::average_frame_seconds() const noexcept {
    return count_ == 0 ? 0.0 : sum_ / static_cast<double>(count_);
}

double FrameStats::fps() const noexcept {
    const double avg = average_frame_seconds();
    return avg > 0.0 ? 1.0 / avg : 0.0;
}

double FrameStats::max_frame_ms() const noexcept {
    double m = 0.0;
    for (std::size_t i = 0; i < count_; ++i) {
        if (samples_[i] > m) {
            m = samples_[i];
        }
    }
    return m * 1000.0;
}

} // namespace musacad::render
