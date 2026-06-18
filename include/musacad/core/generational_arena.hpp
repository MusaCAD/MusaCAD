// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace musacad::core {

/// A data-oriented slot store with generational handles and free-list reuse.
///
/// Storage is Structure-of-Arrays friendly: `data_` is a contiguous vector of
/// T addressable as a span for batch processing, parallel to a `generation`
/// vector. A slot's generation encodes liveness by parity:
///   * even  -> empty/free
///   * odd   -> occupied (live)
/// Inserting bumps the generation to odd; erasing bumps it to even and returns
/// the index to the free list. A handle therefore stays valid only while its
/// stored (odd) generation matches the slot, so erasing one entity never
/// invalidates handles to others, and a reused index is detectably distinct.
///
/// No virtual dispatch: this is a concrete template used directly on hot paths.
template <class T>
class GenerationalArena {
public:
    struct Slot {
        std::uint32_t index;
        std::uint32_t generation;
    };

    /// Inserts a value, reusing a free slot if available. Returns its handle parts.
    Slot insert(const T& value) {
        std::uint32_t idx{};
        if (!free_.empty()) {
            idx = free_.back();
            free_.pop_back();
            data_[idx] = value;
            ++generation_[idx]; // even -> odd (now live)
        } else {
            idx = static_cast<std::uint32_t>(data_.size());
            data_.push_back(value);
            generation_.push_back(1u); // fresh slot starts live (odd)
        }
        ++live_;
        return Slot{idx, generation_[idx]};
    }

    /// Erases the slot if (index, generation) is currently valid. Returns true
    /// if an entity was removed.
    bool erase(std::uint32_t index, std::uint32_t generation) noexcept {
        if (!is_valid(index, generation)) {
            return false;
        }
        ++generation_[index]; // odd -> even (now free)
        free_.push_back(index);
        --live_;
        return true;
    }

    [[nodiscard]] bool is_valid(std::uint32_t index, std::uint32_t generation) const noexcept {
        return index < generation_.size() && generation_[index] == generation &&
               (generation & 1u) == 1u;
    }

    /// True if the slot currently holds a live entity (regardless of generation).
    [[nodiscard]] bool alive(std::uint32_t index) const noexcept {
        return index < generation_.size() && (generation_[index] & 1u) == 1u;
    }

    [[nodiscard]] T* get(std::uint32_t index, std::uint32_t generation) noexcept {
        return is_valid(index, generation) ? &data_[index] : nullptr;
    }
    [[nodiscard]] const T* get(std::uint32_t index, std::uint32_t generation) const noexcept {
        return is_valid(index, generation) ? &data_[index] : nullptr;
    }

    /// Contiguous view of all slots (including dead ones); pair with alive()/
    /// generations() to skip freed slots during batch iteration.
    [[nodiscard]] std::span<const T> data() const noexcept { return data_; }
    [[nodiscard]] std::span<const std::uint32_t> generations() const noexcept {
        return generation_;
    }

    [[nodiscard]] std::size_t slot_count() const noexcept { return data_.size(); }
    [[nodiscard]] std::size_t live_count() const noexcept { return live_; }

    void reserve(std::size_t n) {
        data_.reserve(n);
        generation_.reserve(n);
    }

    /// Drops all slots, returning to the empty state.
    void clear() noexcept {
        data_.clear();
        generation_.clear();
        free_.clear();
        live_ = 0;
    }

private:
    std::vector<T> data_;
    std::vector<std::uint32_t> generation_;
    std::vector<std::uint32_t> free_;
    std::size_t live_ = 0;
};

} // namespace musacad::core
