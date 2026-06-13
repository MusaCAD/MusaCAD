// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace musacad::core {

/// Lock-free single-producer / single-consumer triple buffer.
///
/// Used for the geometry -> render snapshot handoff. The renderer (consumer)
/// reads continuously at display refresh while geometry (producer) writes
/// infrequently and in bursts. A triple buffer suits this asymmetry: the
/// reader never blocks the writer and never retries -- it always has a complete,
/// internally-consistent buffer to read. The cost is one extra buffer.
///
/// Invariant: the three slot indices {0,1,2} are always partitioned among
/// `write_`, `read_`, and the index packed in `shared_`. Each publish/acquire
/// swaps one privately-owned index with the shared one, so the partition (a
/// permutation) is preserved and the producer's current write slot is never the
/// consumer's current read slot. Therefore no two threads ever touch the same
/// buffer at once.
///
/// Memory ordering: the producer's writes to buffer[i] are sequenced-before its
/// release `exchange` that publishes index i into `shared_`. The consumer's
/// acquire `exchange` that reads index i synchronizes-with that release, so all
/// of the producer's writes to buffer[i] are visible before the consumer reads
/// it. `read_`/`write_` are touched only by their owning thread, so they need
/// no atomics.
template <class T>
class TripleBuffer {
public:
    TripleBuffer() = default;

    // --- producer side ------------------------------------------------------

    /// The buffer the producer may freely mutate before publishing.
    [[nodiscard]] T& write_buffer() noexcept { return buffers_[write_]; }

    /// Publishes the current write buffer as the latest and takes ownership of
    /// the slot the consumer/last-published gave up.
    void publish() noexcept {
        const std::uint32_t published = write_ | kFreshBit;
        const std::uint32_t prev = shared_.exchange(published, std::memory_order_acq_rel);
        write_ = prev & kIndexMask;
    }

    // --- consumer side ------------------------------------------------------

    /// If a newer buffer has been published, swaps it in for reading and
    /// returns true. Otherwise leaves the current read buffer in place and
    /// returns false (the consumer keeps the last complete snapshot it had).
    bool acquire() noexcept {
        if ((shared_.load(std::memory_order_acquire) & kFreshBit) == 0u) {
            return false;
        }
        const std::uint32_t prev = shared_.exchange(read_, std::memory_order_acq_rel);
        read_ = prev & kIndexMask;
        return true;
    }

    /// The most recently acquired buffer (valid until the next acquire()).
    [[nodiscard]] const T& read_buffer() const noexcept { return buffers_[read_]; }

private:
    static constexpr std::uint32_t kIndexMask = 0x3u;
    static constexpr std::uint32_t kFreshBit = 0x4u;

    std::array<T, 3> buffers_{};
    std::uint32_t write_ = 0;
    std::uint32_t read_ = 1;
    std::atomic<std::uint32_t> shared_{2}; // index 2 packed, no fresh bit yet
};

} // namespace musacad::core
