// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include "musacad/core/threading/jthread.hpp"
#include <utility>

namespace musacad::core {

/// Multi-producer, single-consumer queue (UI/command threads -> geometry
/// thread). The command rate is low, so a mutex + condition variable is the
/// right tool here: simple, correct, and sanitizer-clean. The latency-critical
/// path is the snapshot handoff (see TripleBuffer), which is lock-free.
template <class T>
class MpscQueue {
public:
    /// Enqueues an item (any producer thread).
    void push(T value) {
        {
            std::lock_guard lock(mutex_);
            queue_.push(std::move(value));
        }
        cv_.notify_one();
    }

    /// Non-blocking pop. Returns nullopt if empty.
    std::optional<T> try_pop() {
        std::lock_guard lock(mutex_);
        if (queue_.empty()) {
            return std::nullopt;
        }
        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

    /// Blocks until an item is available or `token` is stopped. Returns nullopt
    /// only when the queue is empty and a stop has been requested, which the
    /// consumer uses to exit its loop cleanly.
    std::optional<T> wait_pop(threading::stop_token token) {
        std::unique_lock lock(mutex_);
        const bool have = threading::wait_or_stop(cv_, lock, token, [this] { return !queue_.empty(); });
        if (!have) {
            return std::nullopt; // stop requested, queue empty
        }
        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

    [[nodiscard]] bool empty() const {
        std::lock_guard lock(mutex_);
        return queue_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable_any cv_;
    std::queue<T> queue_;
};

} // namespace musacad::core
