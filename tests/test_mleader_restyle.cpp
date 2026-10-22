// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Multileaders follow their multileader style (#61): MLEADER notes the style it draws with,
// the drawing keeps it, and a style modified redraws the multileaders drawn with it -- the
// arrowhead, the text height, the landing and the leader type -- as one undo step. A style
// with multileaders drawn with it is not deleted.

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/native_format.hpp"

using namespace musacad::core;
using musacad::command::CommandOutput;
using musacad::command::CommandProcessor;
using Catch::Approx;

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

io::Document dump(GeometryEngine& engine, const char* name) {
    const std::filesystem::path p = std::filesystem::temp_directory_path() / name;
    engine.consume_snapshot();
    const std::uint64_t before = engine.snapshot().document_version;
    engine.submit(SaveDocumentCommand{p.string(), false});
    REQUIRE(wait_until(engine, [before](const auto& s) { return s.document_version > before; }));
    io::Document doc;
    REQUIRE(io::load_native(p.string(), doc).ok);
    std::filesystem::remove(p);
    return doc;
}

struct Out : CommandOutput {
    std::vector<std::string> lines;
    std::string prompt;
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompt = p; }
};

struct H {
    std::vector<Command> cmds;
    Out out;
    CommandProcessor proc{[this](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
    void run(std::initializer_list<const char*> lines) {
        for (const char* l : lines) {
            proc.submit_line(l);
        }
    }
    template <class T>
    [[nodiscard]] const T* last() const {
        for (auto it = cmds.rbegin(); it != cmds.rend(); ++it) {
            if (const auto* c = std::get_if<T>(&*it)) {
                return c;
            }
        }
        return nullptr;
    }
};

// A multileader as MLEADER draws it with the Standard style: the arrowhead at (0, 0), the
// leader to (10, 10), a 5 landing, the text 1.25 beyond it.
AddMLeaderCommand note(std::uint16_t mlstyle, double y, std::uint64_t group) {
    AddMLeaderCommand m;
    m.vertices = {{0, y}, {10, y + 10}, {15, y + 10}};
    m.block.height = 2.5;
    m.block.attach = 3;
    m.block.pos = {16.25, y + 10};
    m.content = "NOTE";
    m.group = group;
    m.leader_flags = LeaderForm::kLanding;
    m.mlstyle = mlstyle;
    return m;
}

const io::DocMLeader* leader_from(const io::Document& doc, double y) {
    for (const io::DocMLeader& m : doc.mleaders) {
        if (!m.vertices.empty() && std::abs(m.vertices.front().y - y) < 1e-9) {
            return &m;
        }
    }
    return nullptr;
}
} // namespace

TEST_CASE("A multileader style modified redraws the multileaders drawn with it, as one undo step") {
    GeometryEngine engine;
    engine.start();
    MLeaderStyle notes;
    notes.name = "Notes";
    engine.submit(MLeaderStyleCommand{MLeaderStyleCommand::Op::Add, {}, notes});
    engine.submit(note(1, 0, 1));   // drawn with Notes
    engine.submit(note(0, 100, 2)); // with Standard
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    io::Document doc = dump(engine, "musacad_mlrestyle1.musa");
    REQUIRE(leader_from(doc, 0)->mlstyle == 1); // kept in the drawing
    REQUIRE(leader_from(doc, 100)->mlstyle == 0);

    MLeaderStyle big = notes;
    big.text_height = 5.0;
    big.landing_distance = 10.0;
    big.landing_gap = 2.0;
    big.arrow = 3;
    big.arrow_size = 4.0;
    big.leader_type = 1; // spline
    engine.submit(MLeaderStyleCommand{MLeaderStyleCommand::Op::Modify, "Notes", big, 3});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status == "Multileader style \"Notes\" changed. 1 multileader updated.";
    }));
    doc = dump(engine, "musacad_mlrestyle2.musa");
    const io::DocMLeader* m = leader_from(doc, 0);
    REQUIRE(m != nullptr);
    REQUIRE(m->vertices.size() == 3);
    REQUIRE(m->vertices[1] == Vec2{10, 10}); // the leader's own points stay
    REQUIRE(m->vertices[2].x == Approx(20.0)); // a 10 landing
    REQUIRE(m->block.height == Approx(5.0));
    REQUIRE(m->block.pos.x == Approx(22.0)); // a 2 gap
    REQUIRE(m->overrides.arrow_type == 3);
    REQUIRE(m->overrides.arrow_size == Approx(4.0));
    REQUIRE((m->leader_flags & LeaderForm::kSpline) != 0);
    REQUIRE((m->leader_flags & LeaderForm::kLanding) != 0);
    REQUIRE(m->mlstyle == 1);
    const io::DocMLeader* standard = leader_from(doc, 100);
    REQUIRE(standard->vertices[2].x == Approx(15.0)); // a Standard one as it was
    REQUIRE(standard->block.height == Approx(2.5));

    engine.submit(UndoLastGroupCommand{});
    doc = dump(engine, "musacad_mlrestyle3.musa");
    REQUIRE(leader_from(doc, 0)->vertices[2].x == Approx(15.0));
    engine.submit(RedoLastGroupCommand{});

    // Without a landing: the text beside the leader's last point.
    big.landing = false;
    big.leader_type = 0;
    engine.submit(MLeaderStyleCommand{MLeaderStyleCommand::Op::Modify, "Notes", big, 4});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status.find("1 multileader updated.") != std::string::npos; }));
    doc = dump(engine, "musacad_mlrestyle4.musa");
    m = leader_from(doc, 0);
    REQUIRE(m->vertices.size() == 2);
    REQUIRE(m->block.pos.x == Approx(12.0));
    REQUIRE((m->leader_flags & (LeaderForm::kLanding | LeaderForm::kSpline)) == 0);

    // In use: not deleted. Once its multileaders are gone it is, and those of a later style
    // keep theirs.
    MLeaderStyle third = notes;
    third.name = "Third";
    engine.submit(MLeaderStyleCommand{MLeaderStyleCommand::Op::Add, {}, third});
    engine.submit(note(2, 200, 5));
    engine.submit(MLeaderStyleCommand{MLeaderStyleCommand::Op::Delete, "Notes", {}});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status == "Multileader style \"Notes\" cannot be deleted: multileaders are drawn with it.";
    }));
    engine.submit(SelectPickCommand{{0, 0}, 1.0, false, false});
    engine.submit(EraseSelectionCommand{6});
    engine.submit(MLeaderStyleCommand{MLeaderStyleCommand::Op::Delete, "Notes", {}});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Multileader style \"Notes\" deleted."; }));
    doc = dump(engine, "musacad_mlrestyle5.musa");
    REQUIRE(doc.mleader_styles.size() == 2);
    REQUIRE(leader_from(doc, 0) == nullptr);
    REQUIRE(leader_from(doc, 200)->mlstyle == 1); // Third, now second
    engine.stop();
}

TEST_CASE("MLEADER notes the current multileader style on what it draws") {
    MLeaderStyle big;
    big.name = "Big";
    big.text_height = 5.0;
    H h;
    h.proc.set_mleader_styles({MLeaderStyle{}, big}, 1);
    h.run({"MLD", "0,0", "10,10", "NOTE"});
    const auto* m = h.last<AddMLeaderCommand>();
    REQUIRE(m != nullptr);
    REQUIRE(m->mlstyle == 1);
    H standard;
    standard.run({"MLD", "0,0", "10,10", "NOTE"});
    REQUIRE(standard.last<AddMLeaderCommand>()->mlstyle == 0);
}
