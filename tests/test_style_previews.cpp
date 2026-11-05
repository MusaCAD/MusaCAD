// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// The ribbon's style galleries (#78): the engine draws a dimension, a multileader and a table
// with each style, for the Dimension, Multileader and Table Style lists to show.

#include <chrono>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"

using namespace musacad::core;

namespace {
template <class Pred>
bool wait_until(GeometryEngine& e, Pred pred) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        e.consume_snapshot();
        if (pred(e.snapshot())) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}
} // namespace

TEST_CASE("#78 each dimension, multileader and table style has a picture for its gallery") {
    GeometryEngine engine;
    engine.start();
    engine.submit(SelectAllCommand{}); // the engine publishes once it has had a command
    REQUIRE(wait_until(engine, [](const auto& s) { return s.dimstyle_previews != nullptr; }));
    const RenderSnapshot& s = engine.snapshot();
    REQUIRE(s.dimstyle_previews->size() >= 1);
    REQUIRE(s.mleaderstyle_previews->size() >= 1);
    REQUIRE(s.tablestyle_previews->size() >= 1);
    for (const auto* list : {s.dimstyle_previews.get(), s.mleaderstyle_previews.get(), s.tablestyle_previews.get()}) {
        for (const std::vector<Vec2>& pairs : *list) {
            REQUIRE(pairs.size() >= 8); // drawn: more than a stroke or two
            REQUIRE(pairs.size() % 2 == 0);
        }
    }
    // A new style: a new picture.
    const std::size_t before = s.dimstyle_previews->size();
    DimStyle big;
    big.name = "Big";
    big.text_height = 10.0;
    engine.submit(AddDimStyleCommand{big});
    REQUIRE(wait_until(engine, [before](const auto& st) {
        return st.dimstyle_previews != nullptr && st.dimstyle_previews->size() == before + 1;
    }));
    engine.stop();
}
