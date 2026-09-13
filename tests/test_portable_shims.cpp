// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// The portable fallbacks (#2): parse_double without floating-point std::from_chars, and
// the jthread / stop_token shim with its stop-aware condition wait. Apple's libc++ builds
// these; on Linux this binary is compiled with MUSACAD_FORCE_PORTABLE_SHIMS so they run
// under the sanitizers here too.
#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "musacad/core/text/parse_double.hpp"
#include "musacad/core/threading/jthread.hpp"
#include "musacad/core/threading/mpsc_queue.hpp"

using namespace musacad::core;

#if !defined(MUSACAD_PARSE_DOUBLE_FALLBACK)
#error "this test binary must compile the parse_double fallback (MUSACAD_FORCE_PORTABLE_SHIMS)"
#endif

TEST_CASE("parse_double fallback: from_chars semantics -- no sign or space prefix, C-locale point, stop pointer") {
    double v = 0.0;
    CHECK(parse_double_all("12.5", v));
    CHECK(v == 12.5);
    CHECK(parse_double_all("-3e2", v));
    CHECK(v == -300.0);
    CHECK(parse_double_all("0", v));
    CHECK(v == 0.0);
    CHECK(parse_double_all(".5", v));
    CHECK(v == 0.5);
    CHECK(!parse_double_all("+1", v));   // from_chars rejects a leading '+'
    CHECK(!parse_double_all(" 1", v));   // ... and leading whitespace
    CHECK(!parse_double_all("1x", v));   // the whole string must be the number
    CHECK(!parse_double_all("", v));
    CHECK(!parse_double_all("abc", v));
    CHECK(!parse_double_all("1,5", v));  // never the locale's comma

    const std::string t = "7.25,9";
    const ParsedDouble r = parse_double(t.data(), t.data() + t.size(), v);
    CHECK(r.ok);
    CHECK(v == 7.25);
    CHECK(r.ptr == t.data() + 4); // stopped at the comma
    const ParsedDouble bad = parse_double(t.data() + 4, t.data() + t.size(), v);
    CHECK(!bad.ok);
    CHECK(bad.ptr == t.data() + 4);

    const std::string big(120, '9'); // longer than the stack buffer
    CHECK(parse_double_all(big, v));
    CHECK(v > 1e119);
}

TEST_CASE("jthread shim: a stop wakes a waiting pop, a plain callable runs, destruction and move-assignment join") {
    MpscQueue<int> q;
    std::atomic<int> popped{-1};
    threading::jthread w([&](threading::stop_token tok) {
        const auto got = q.wait_pop(tok);
        popped = got ? *got : -2;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CHECK(popped == -1); // still waiting
    CHECK(w.joinable());
    CHECK(!w.get_stop_token().stop_requested());
    CHECK(w.request_stop());
    w.join();
    CHECK(popped == -2); // stopped with an empty queue -> nullopt
    CHECK(w.get_stop_token().stop_requested());
    CHECK(!w.joinable());

    // An item arriving ends the wait with the item.
    popped = -1;
    threading::jthread w2([&](threading::stop_token tok) {
        const auto got = q.wait_pop(tok);
        popped = got ? *got : -2;
    });
    q.push(5);
    w2 = threading::jthread{}; // move-assignment joins the running thread
    CHECK(popped == 5);
    CHECK(!w2.joinable());

    // A stop requested before the wait returns at once.
    threading::jthread w3([&](threading::stop_token tok) {
        while (!tok.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto got = q.wait_pop(tok);
        popped = got ? *got : -3;
    });
    w3.request_stop();
    w3.join();
    CHECK(popped == -3);

    // A callable that takes no token runs too, and the destructor joins it.
    std::atomic<bool> ran{false};
    {
        threading::jthread plain([&] { ran = true; });
    }
    CHECK(ran);
    threading::jthread empty;
    CHECK(!empty.joinable());
    CHECK(!empty.request_stop());
    CHECK(!empty.get_stop_token().stop_possible());
}

TEST_CASE("wait_or_stop: no lost wake-up when the stop lands between the check and the wait") {
    // Many rounds: a waiter that re-checks under the lock races a stopper; every round must
    // end (the callback takes the waiter's mutex before notifying).
    for (int round = 0; round < 200; ++round) {
        MpscQueue<int> q;
        std::atomic<bool> done{false};
        threading::jthread waiter([&](threading::stop_token tok) {
            (void)q.wait_pop(tok);
            done = true;
        });
        std::this_thread::sleep_for(std::chrono::microseconds(round % 7 * 10));
        waiter.request_stop();
        waiter.join();
        REQUIRE(done);
    }
}
