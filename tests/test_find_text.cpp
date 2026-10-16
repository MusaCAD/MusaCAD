// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// FIND (#62) and TCASE (#77).

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/command/commands.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/native_format.hpp"

using namespace musacad::core;
using musacad::command::CommandOutput;
using musacad::command::CommandProcessor;

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

AddTextCommand text_at(Vec2 p, const char* s, std::uint64_t group) {
    AddTextCommand t;
    t.pos = p;
    t.content = s;
    t.group = group;
    return t;
}

bool has_text(const io::Document& doc, const std::string& s) {
    for (const auto& t : doc.texts) {
        if (t.content == s) {
            return true;
        }
    }
    return false;
}

struct Out : CommandOutput {
    std::vector<std::string> lines;
    std::string prompt;
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompt = p; }
};
} // namespace

TEST_CASE("FIND selects the objects whose text matches; Replace All changes them in one step") {
    GeometryEngine engine;
    engine.start();
    engine.submit(text_at({0, 0}, "Steel beam", 1));
    engine.submit(text_at({0, 10}, "STEEL column, steelwork", 2));
    AddMTextCommand m;
    m.block.pos = {0, 20};
    m.content = "No steel here?\nsteel";
    m.group = 3;
    engine.submit(m);
    AddDimensionCommand d{static_cast<std::uint8_t>(DimType::Linear), {0, 30}, {10, 30}, {5, 35}, 0, 4};
    d.text_override = "<> steel";
    engine.submit(d);
    engine.submit(FindTextCommand{"steel", {}, false, false, false, false, 5});
    REQUIRE(wait_until(engine, [](const auto& s) {
        return s.status == "Found 6 matches in 4 objects; they are selected." && s.selection.size() == 4;
    }));
    engine.submit(FindTextCommand{"steel", {}, false, true, true, false, 6}); // as typed, whole words
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Found 3 matches in 2 objects; they are selected."; }));
    engine.submit(FindTextCommand{"steel", "timber", true, false, true, false, 7});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "Replaced 5 matches in 4 objects."; }));
    io::Document doc = dump(engine, "musacad_find_replace.musa");
    REQUIRE(has_text(doc, "timber beam"));
    REQUIRE(has_text(doc, "timber column, steelwork"));
    REQUIRE(doc.mtexts[0].content == "No timber here?\ntimber");
    REQUIRE(doc.dims[0].text_override == "<> timber");
    engine.submit(UndoLastGroupCommand{});
    doc = dump(engine, "musacad_find_replace_undo.musa");
    REQUIRE(has_text(doc, "Steel beam"));
    engine.submit(FindTextCommand{"nothing", {}, false, false, false, false, 8});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "No matches found for \"nothing\"."; }));
    engine.stop();
}

TEST_CASE("TCASE changes the case of the selected text") {
    using M = TextCaseCommand::Mode;
    GeometryEngine engine;
    engine.start();
    engine.submit(text_at({0, 0}, "second LINE here. and more", 1));
    engine.submit(text_at({0, 10}, "first line", 2));
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.selection.size() == 2; }));
    engine.submit(TextCaseCommand{M::Sentence, 3});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.status == "2 text objects changed."; }));
    io::Document doc = dump(engine, "musacad_tcase.musa");
    REQUIRE(has_text(doc, "Second line here. And more"));
    REQUIRE(has_text(doc, "First line"));
    engine.submit(SelectAllCommand{});
    engine.submit(TextCaseCommand{M::Title, 4});
    engine.submit(SelectAllCommand{});
    engine.submit(TextCaseCommand{M::Toggle, 5});
    doc = dump(engine, "musacad_tcase2.musa");
    REQUIRE(has_text(doc, "fIRST lINE"));
    REQUIRE(has_text(doc, "sECOND lINE hERE. aND mORE"));
    engine.stop();

    std::vector<Command> cmds;
    Out out;
    CommandProcessor proc{[&cmds](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
    proc.submit_line("FIND");
    REQUIRE(out.prompt == "Enter the text to find: ");
    REQUIRE(proc.wants_free_text());
    proc.submit_line("old name");
    REQUIRE(out.prompt == "Enter the replacement text, or Enter to find only: ");
    proc.submit_line("new name");
    proc.submit_line("Y");
    REQUIRE(out.prompt == "Find whole words only [Yes/No] <No>: ");
    proc.submit_line("");
    const auto* f = std::get_if<FindTextCommand>(&cmds.back());
    REQUIRE(f != nullptr);
    REQUIRE(f->find == "old name");
    REQUIRE(f->replace == "new name");
    REQUIRE(f->do_replace);
    REQUIRE(f->match_case);
    REQUIRE_FALSE(f->whole_word);
    proc.set_selection_count(2);
    proc.submit_line("TCASE");
    REQUIRE(out.prompt == "Enter case [Sentence/Lower/Upper/Title/tOggle] <Upper>: ");
    proc.submit_line("T");
    REQUIRE(std::get_if<TextCaseCommand>(&cmds.back())->mode == M::Title);
}

TEST_CASE("ADDSELECTED draws the kind of object picked, on its layer and in its colour") {
    GeometryEngine engine;
    engine.start();
    Layer walls;
    walls.name = "Walls";
    engine.submit(AddLayerCommand{walls});
    EntityProps red;
    red.layer = 1;
    red.color = Rgb{255, 0, 0};
    red.set_color_by_layer(false);
    engine.submit(AddLineCommand{{0, 0}, {10, 0}, 1, red});
    REQUIRE(wait_until(engine, [](const auto& s) { return !s.line_vertices.empty(); }));
    engine.submit(PropsFromPickCommand{{5, 0}, 0.5, false});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.current_layer == 1; }));
    engine.submit(AddLineCommand{{0, 5}, {10, 5}, 2});
    engine.submit(PropsFromPickCommand{{}, 0.0, true});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.current_layer == 0; }));
    const io::Document doc = dump(engine, "musacad_addselected.musa");
    REQUIRE(doc.lines.size() == 2);
    for (const io::DocLine& l : doc.lines) {
        REQUIRE(l.props.layer == 1);
        REQUIRE(l.props.color == Rgb{255, 0, 0});
    }
    engine.stop();

    std::vector<Command> cmds;
    Out out;
    CommandProcessor proc{[&cmds](Command c) { cmds.push_back(std::move(c)); }, nullptr, out};
    proc.submit_line("ADDSELECTED");
    REQUIRE(out.prompt == "Select object: ");
    proc.set_hovered_kind(EntityKind::Line);
    proc.submit_line("5,0");
    proc.set_hovered_kind(std::nullopt);
    REQUIRE(std::get_if<PropsFromPickCommand>(&cmds.back())->pick == Vec2{5, 0});
    REQUIRE(out.prompt.rfind("Specify first point", 0) == 0);
    proc.submit_line("0,5");
    proc.submit_line("10,5");
    proc.submit_line("");
    REQUIRE(std::get_if<PropsFromPickCommand>(&cmds.back())->restore);
    REQUIRE_FALSE(proc.has_active_command());
}
