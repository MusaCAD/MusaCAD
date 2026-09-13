// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
#pragma once

// std::jthread / std::stop_token where the standard library has them (libstdc++, MSVC);
// a small equivalent where it does not (libc++ keeps them experimental, so Apple's
// toolchains have neither). Code uses threading::jthread / threading::stop_token and
// wait_or_stop() for the stop-aware condition wait, and never sees the difference.
#include <condition_variable>
#include <utility>
#include <version>

// MUSACAD_FORCE_PORTABLE_SHIMS: use the shim even where std::jthread exists (the shim
// self-test does this on Linux, where Apple's path is otherwise unbuilt).
#if defined(__cpp_lib_jthread) && !defined(MUSACAD_FORCE_PORTABLE_SHIMS)
#include <stop_token>
#include <thread>

namespace musacad::core::threading {

using jthread = std::jthread;
using stop_token = std::stop_token;

/// cv.wait(lock, token, pred): true when `pred` came to hold, false when the stop came
/// first (the queue is then left as it is).
template <class Lock, class Pred>
bool wait_or_stop(std::condition_variable_any& cv, Lock& lock, const stop_token& token, Pred pred) {
    return cv.wait(lock, token, std::move(pred));
}

} // namespace musacad::core::threading

#else
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace musacad::core::threading {

namespace detail {
/// The shared stop state: a flag plus the callbacks a stop runs (a waiting condition
/// variable registers one so the stop wakes it).
struct StopState {
    std::atomic<bool> requested{false};
    std::mutex mutex;
    std::vector<std::pair<std::uint64_t, std::function<void()>>> callbacks;
    std::uint64_t next_id = 1;

    void request() {
        std::vector<std::function<void()>> run;
        {
            std::lock_guard lock(mutex);
            if (requested.exchange(true)) {
                return;
            }
            for (auto& [id, fn] : callbacks) {
                run.push_back(fn);
            }
        }
        for (auto& fn : run) {
            fn();
        }
    }
    std::uint64_t add(std::function<void()> fn) {
        std::lock_guard lock(mutex);
        const std::uint64_t id = next_id++;
        callbacks.emplace_back(id, std::move(fn));
        return id;
    }
    void remove(std::uint64_t id) {
        std::lock_guard lock(mutex);
        for (auto it = callbacks.begin(); it != callbacks.end(); ++it) {
            if (it->first == id) {
                callbacks.erase(it);
                return;
            }
        }
    }
};
} // namespace detail

class stop_token {
public:
    stop_token() = default;
    explicit stop_token(std::shared_ptr<detail::StopState> state) : state_(std::move(state)) {}
    [[nodiscard]] bool stop_requested() const noexcept {
        return state_ != nullptr && state_->requested.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool stop_possible() const noexcept { return state_ != nullptr; }
    [[nodiscard]] const std::shared_ptr<detail::StopState>& state() const noexcept { return state_; }

private:
    std::shared_ptr<detail::StopState> state_;
};

/// A thread that requests its stop and joins on destruction and on move-assignment; a
/// callable taking a stop_token first receives the thread's token, as std::jthread does.
class jthread {
public:
    jthread() noexcept = default;
    template <class F, class... Args>
    explicit jthread(F&& f, Args&&... args) : state_(std::make_shared<detail::StopState>()) {
        if constexpr (std::is_invocable_v<std::decay_t<F>, stop_token, std::decay_t<Args>...>) {
            thread_ = std::thread(std::forward<F>(f), stop_token{state_}, std::forward<Args>(args)...);
        } else {
            thread_ = std::thread(std::forward<F>(f), std::forward<Args>(args)...);
        }
    }
    ~jthread() {
        if (joinable()) {
            request_stop();
            join();
        }
    }
    jthread(const jthread&) = delete;
    jthread& operator=(const jthread&) = delete;
    jthread(jthread&&) noexcept = default;
    jthread& operator=(jthread&& other) noexcept {
        if (this != &other) {
            if (joinable()) {
                request_stop();
                join();
            }
            thread_ = std::move(other.thread_);
            state_ = std::move(other.state_);
        }
        return *this;
    }
    [[nodiscard]] bool joinable() const noexcept { return thread_.joinable(); }
    void join() { thread_.join(); }
    bool request_stop() noexcept {
        if (state_ == nullptr) {
            return false;
        }
        state_->request();
        return true;
    }
    [[nodiscard]] stop_token get_stop_token() const noexcept { return stop_token{state_}; }

private:
    std::thread thread_;
    std::shared_ptr<detail::StopState> state_;
};

/// cv.wait(lock, token, pred) without std::stop_token: the stop's callback takes the
/// waiter's mutex before notifying, so a stop requested between the predicate check and
/// the wait cannot be lost. True when `pred` came to hold, false when the stop came first.
template <class Lock, class Pred>
bool wait_or_stop(std::condition_variable_any& cv, Lock& lock, const stop_token& token, Pred pred) {
    if (!token.stop_possible()) {
        cv.wait(lock, std::move(pred));
        return true;
    }
    auto* mutex = lock.mutex();
    const std::uint64_t id = token.state()->add([&cv, mutex] {
        std::lock_guard guard(*mutex);
        cv.notify_all();
    });
    cv.wait(lock, [&] { return token.stop_requested() || pred(); });
    token.state()->remove(id);
    return pred();
}

} // namespace musacad::core::threading
#endif
