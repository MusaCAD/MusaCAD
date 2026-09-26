// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// "Select objects:" as every edit command's first step (issue #46), ERASE / OOPS,
// TRIM's per-pick Undo, MATCHPROP by window, -GROUP / GROUPEDIT / PICKSTYLE, -PURGE
// and -UNITS (issues #53, #67) -- the command-layer flows.

#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/command/commands.hpp"
#include "musacad/core/command.hpp"

using namespace musacad::command;
using namespace musacad::core;
using Catch::Approx;

namespace {
struct Out : CommandOutput {
    std::vector<std::string> lines;
    std::string prompt;
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompt = p; }
    [[nodiscard]] bool any_contains(const std::string& sub) const {
        for (const auto& l : lines) {
            if (l.find(sub) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
};
struct View : ViewControl {
    int pickbox = 10;
    bool dialog_opened = false;
    void zoom_extents() override {}
    void zoom_scale(double) override {}
    int selection_setting(const std::string& name) const override { return name == "PICKBOX" ? pickbox : 0; }
    bool set_selection_setting(const std::string& name, int v) override {
        if (name == "PICKBOX" && v >= 0 && v <= 50) {
            pickbox = v;
            return true;
        }
        return false;
    }
    bool quick_select_dialog(bool) override {
        dialog_opened = true;
        return true;
    }
};
struct H {
    std::vector<Command> cmds;
    Out out;
    View view;
    CommandProcessor proc{[this](Command c) { cmds.push_back(std::move(c)); }, &view, out};
    template <class T>
    [[nodiscard]] const T* last() const {
        for (auto it = cmds.rbegin(); it != cmds.rend(); ++it) {
            if (const auto* c = std::get_if<T>(&*it)) {
                return c;
            }
        }
        return nullptr;
    }
    template <class T>
    [[nodiscard]] std::size_t count() const {
        std::size_t n = 0;
        for (const auto& c : cmds) {
            n += std::holds_alternative<T>(c) ? 1 : 0;
        }
        return n;
    }
};
} // namespace

TEST_CASE("#46 MOVE with nothing selected asks Select objects and works on what was gathered") {
    H h;
    h.proc.set_selection_count(0);
    h.proc.submit_line("M");
    REQUIRE(h.proc.in_selection_phase());
    REQUIRE(h.out.prompt == "Select objects: ");
    h.proc.submit_line("5,5"); // a typed pick accumulates and announces
    const auto* pick = h.last<SelectPickCommand>();
    REQUIRE(pick != nullptr);
    REQUIRE(pick->additive);
    REQUIRE(pick->announce);
    h.proc.set_selection_count(1);
    h.proc.submit_line(""); // Enter ends the step
    REQUIRE(!h.proc.in_selection_phase());
    REQUIRE(h.out.prompt.find("Specify base point") != std::string::npos);
    h.proc.submit_line("0,0");
    h.proc.submit_line("10,0");
    const auto* mv = h.last<MoveSelectionCommand>();
    REQUIRE(mv != nullptr);
    REQUIRE(mv->delta == Vec2{10, 0});
    REQUIRE(!h.proc.has_active_command());
}

TEST_CASE("#46 Enter at Select objects with nothing chosen ends the command quietly") {
    H h;
    h.proc.set_selection_count(0);
    h.proc.submit_line("CO");
    REQUIRE(h.proc.in_selection_phase());
    h.proc.submit_line("");
    REQUIRE(!h.proc.has_active_command());
    REQUIRE(h.cmds.empty());
}

TEST_CASE("#46 the Select objects keywords: Window, Crossing, BOX, ALL, Fence, WPolygon, CPolygon, Last, Previous, Group, Undo") {
    H h;
    h.proc.set_selection_count(0);
    h.proc.submit_line("RO");
    REQUIRE(h.proc.in_selection_phase());

    h.proc.submit_line("W");
    REQUIRE(h.out.prompt == "Specify first corner: ");
    h.proc.submit_line("0,0");
    REQUIRE(h.out.prompt == "Specify opposite corner: ");
    h.proc.submit_line("10,10");
    const auto* w = h.last<SelectWindowCommand>();
    REQUIRE(w != nullptr);
    REQUIRE(!w->crossing);
    REQUIRE(w->additive);
    REQUIRE(w->announce);

    h.proc.submit_line("C");
    h.proc.submit_line("0,0");
    h.proc.submit_line("10,10");
    REQUIRE(h.last<SelectWindowCommand>()->crossing);

    h.proc.submit_line("BOX"); // direction decides: right-to-left is a crossing
    h.proc.submit_line("10,0");
    h.proc.submit_line("0,10");
    REQUIRE(h.last<SelectWindowCommand>()->crossing);

    h.proc.submit_line("ALL");
    REQUIRE(h.last<SelectAllCommand>() != nullptr);
    REQUIRE(h.last<SelectAllCommand>()->announce);

    h.proc.submit_line("F");
    h.proc.submit_line("0,5");
    h.proc.submit_line("20,5");
    h.proc.submit_line("20,15");
    h.proc.submit_line("U"); // drops the last fence point
    h.proc.submit_line("");  // ends the fence
    const auto* f = h.last<SelectFenceCommand>();
    REQUIRE(f != nullptr);
    REQUIRE(f->points.size() == 2);

    h.proc.submit_line("WP");
    h.proc.submit_line("0,0");
    h.proc.submit_line("10,0");
    h.proc.submit_line("10,10");
    h.proc.submit_line("");
    const auto* wp = h.last<SelectPolygonCommand>();
    REQUIRE(wp != nullptr);
    REQUIRE(!wp->crossing);
    REQUIRE(wp->points.size() == 3);

    h.proc.submit_line("CP");
    h.proc.submit_line("0,0");
    h.proc.submit_line("10,0");
    h.proc.submit_line("10,10");
    h.proc.submit_line("");
    REQUIRE(h.last<SelectPolygonCommand>()->crossing);

    h.proc.submit_line("L");
    REQUIRE(h.last<SelectLastCommand>() != nullptr);
    h.proc.submit_line("P");
    REQUIRE(h.last<SelectPreviousCommand>() != nullptr);
    h.proc.submit_line("G");
    REQUIRE(h.out.prompt == "Enter group name: ");
    h.proc.submit_line("FRAME");
    REQUIRE(h.last<SelectGroupCommand>()->name == "FRAME");
    h.proc.submit_line("U");
    REQUIRE(h.last<SelectUndoCommand>() != nullptr);
    REQUIRE(h.proc.in_selection_phase()); // still gathering
    h.proc.cancel();
}

TEST_CASE("#46 Remove and Add switch what a pick does; SIngle ends the step on the first pick") {
    H h;
    h.proc.set_selection_count(0);
    h.proc.submit_line("SC");
    h.proc.submit_line("R");
    REQUIRE(h.out.prompt == "Remove objects: ");
    REQUIRE(h.proc.selection_phase_removing());
    h.proc.submit_line("3,3");
    REQUIRE(h.last<SelectPickCommand>()->remove);
    h.proc.submit_line("A");
    REQUIRE(h.out.prompt == "Select objects: ");
    REQUIRE(!h.proc.selection_phase_removing());
    h.proc.submit_line("SI");
    h.proc.set_selection_count(1);
    h.proc.submit_line("4,4"); // the one pick SIngle allows
    REQUIRE(!h.last<SelectPickCommand>()->remove);
    REQUIRE(!h.proc.in_selection_phase());
    REQUIRE(h.out.prompt == "Specify base point: ");
    h.proc.cancel();
}

TEST_CASE("#46 a viewport gesture in SIngle mode ends the step too; the SELECT command leaves the set for the next command") {
    H h;
    h.proc.set_selection_count(0);
    h.proc.submit_line("MI");
    h.proc.submit_line("SI");
    h.proc.set_selection_count(2);
    h.proc.notify_selection_gesture(); // the viewport's window
    REQUIRE(!h.proc.in_selection_phase());
    REQUIRE(h.out.prompt == "Specify first point of mirror line: ");
    h.proc.cancel();

    h.proc.submit_line("SELECT");
    REQUIRE(h.proc.in_selection_phase());
    h.proc.submit_line("ALL");
    h.proc.submit_line("");
    REQUIRE(!h.proc.has_active_command());
    // Nothing was fired but the selection: the next command is noun-verb.
    h.proc.set_selection_count(3);
    h.proc.submit_line("M");
    REQUIRE(!h.proc.in_selection_phase());
    h.proc.cancel();
}

TEST_CASE("#53 ERASE gathers then erases; a pre-selection is erased at once; OOPS restores; Last and ALL still work") {
    H h;
    h.proc.set_selection_count(0);
    h.proc.submit_line("E");
    REQUIRE(h.proc.in_selection_phase());
    h.proc.submit_line("L");
    REQUIRE(h.last<SelectLastCommand>() != nullptr);
    h.proc.set_selection_count(1);
    h.proc.submit_line("");
    REQUIRE(h.last<EraseSelectionCommand>() != nullptr);
    REQUIRE(!h.proc.has_active_command());

    h.proc.set_selection_count(2);
    h.proc.submit_line("ERASE");
    REQUIRE(h.count<EraseSelectionCommand>() == 2);
    REQUIRE(!h.proc.has_active_command());

    h.proc.submit_line("OOPS");
    REQUIRE(h.last<musacad::core::OopsCommand>() != nullptr);

    h.proc.set_selection_count(0);
    h.proc.submit_line("E");
    h.proc.submit_line("ALL");
    REQUIRE(h.last<SelectAllCommand>() != nullptr);
    h.proc.set_selection_count(5);
    h.proc.submit_line("");
    REQUIRE(h.count<EraseSelectionCommand>() == 3);
}

TEST_CASE("#53 TRIM and EXTEND: every pick is its own undo step and Undo takes the last back") {
    H h;
    h.proc.submit_line("TR");
    REQUIRE(h.out.prompt == "Select object to trim or [Undo]: ");
    h.proc.submit_line("U");
    REQUIRE(h.out.any_contains("Nothing to undo"));
    h.proc.submit_line("1,1");
    h.proc.submit_line("2,2");
    const auto& picks = h.cmds;
    const auto* p1 = std::get_if<TrimPickCommand>(&picks[0]);
    const auto* p2 = std::get_if<TrimPickCommand>(&picks[1]);
    REQUIRE(p1 != nullptr);
    REQUIRE(p2 != nullptr);
    REQUIRE(p1->group != p2->group); // separate undo groups
    h.proc.submit_line("UNDO");
    REQUIRE(std::holds_alternative<UndoLastGroupCommand>(h.cmds.back()));
    h.proc.submit_line("");
    REQUIRE(!h.proc.has_active_command());

    h.proc.submit_line("EX");
    h.proc.submit_line("1,1");
    h.proc.submit_line("3,3");
    const auto* e1 = std::get_if<ExtendPickCommand>(&h.cmds[h.cmds.size() - 2]);
    const auto* e2 = std::get_if<ExtendPickCommand>(&h.cmds.back());
    REQUIRE(e1 != nullptr);
    REQUIRE(e2 != nullptr);
    REQUIRE(e1->group != e2->group);
    h.proc.cancel();
}

TEST_CASE("#53 MATCHPROP: the destinations are a Select objects step; a gesture applies to the set; the settings line is echoed") {
    H h;
    h.proc.set_selection_count(0);
    h.proc.submit_line("MA");
    h.proc.submit_line("1,1"); // the source
    REQUIRE(h.last<MatchPropPickSourceCommand>() != nullptr);
    REQUIRE(h.proc.in_selection_phase());
    REQUIRE(h.out.any_contains("Current active settings: Color Layer Ltype"));
    h.proc.notify_selection_gesture(); // the viewport's window of destinations
    REQUIRE(h.last<MatchPropApplySelectionCommand>() != nullptr);
    REQUIRE(h.proc.in_selection_phase()); // keeps going until Enter
    h.proc.submit_line("5,5"); // a typed destination: pick + apply
    REQUIRE(h.last<SelectPickCommand>() != nullptr);
    REQUIRE(h.count<MatchPropApplySelectionCommand>() == 2);
    h.proc.submit_line("");
    REQUIRE(!h.proc.has_active_command());
}

TEST_CASE("#53 -GROUP options, GROUPEDIT, GROUP ? and PICKSTYLE 0..3") {
    H h;
    h.proc.submit_line("-GROUP");
    h.proc.submit_line("?");
    REQUIRE(h.last<ListGroupsCommand>() != nullptr);
    h.proc.submit_line("REN");
    h.proc.submit_line("OLD");
    h.proc.submit_line("NEW");
    const auto* ren = h.last<musacad::core::GroupEditCommand>();
    REQUIRE(ren != nullptr);
    REQUIRE(ren->op == 2);
    REQUIRE(ren->name == "OLD");
    REQUIRE(ren->text == "NEW");
    REQUIRE(!h.proc.has_active_command());

    h.proc.submit_line("-GROUP");
    h.proc.submit_line("A");
    h.proc.submit_line("FRAME");
    REQUIRE(h.proc.in_selection_phase());
    h.proc.submit_line("1,1");
    h.proc.set_selection_count(1);
    h.proc.submit_line("");
    const auto* add = h.last<musacad::core::GroupEditCommand>();
    REQUIRE(add->op == 0);
    REQUIRE(add->name == "FRAME");

    h.proc.submit_line("-G");
    h.proc.submit_line("O");
    h.proc.submit_line("FRAME");
    h.proc.submit_line("2");
    h.proc.submit_line("0");
    const auto* ord = h.last<musacad::core::GroupEditCommand>();
    REQUIRE(ord->op == 5);
    REQUIRE(ord->from == 2);
    REQUIRE(ord->to == 0);

    h.proc.submit_line("-GROUP");
    h.proc.submit_line("S");
    h.proc.submit_line("FRAME");
    h.proc.submit_line("Y");
    REQUIRE(h.last<musacad::core::GroupEditCommand>()->op == 4);
    REQUIRE(!h.last<musacad::core::GroupEditCommand>()->flag);

    h.proc.submit_line("-GROUP");
    h.proc.submit_line("E");
    h.proc.submit_line("FRAME");
    REQUIRE(h.last<musacad::core::GroupEditCommand>()->op == 6);

    h.proc.submit_line("-GROUP");
    h.proc.submit_line(""); // Create
    h.proc.submit_line("WALLS");
    h.proc.submit_line("outer walls");
    REQUIRE(h.proc.in_selection_phase());
    h.proc.set_selection_count(2);
    h.proc.submit_line("");
    REQUIRE(h.last<CreateGroupCommand>()->name == "WALLS");
    REQUIRE(h.last<CreateGroupCommand>()->description == "outer walls");

    h.proc.submit_line("GROUPEDIT");
    h.proc.submit_line("3,3"); // a member pick names the group
    REQUIRE(h.out.prompt == "Enter an option [Add objects/Remove objects/REName]: ");
    h.proc.submit_line("R");
    REQUIRE(h.proc.in_selection_phase());
    h.proc.set_selection_count(1);
    h.proc.submit_line("");
    const auto* ge = h.last<musacad::core::GroupEditCommand>();
    REQUIRE(ge->by_pick);
    REQUIRE(ge->op == 1);

    h.proc.submit_line("PICKSTYLE");
    h.proc.submit_line("3");
    REQUIRE(h.last<SetPickStyleCommand>()->group_select);
    REQUIRE(h.last<SetPickStyleCommand>()->hatch_assoc);
    h.proc.submit_line("PICKSTYLE");
    h.proc.submit_line("4");
    REQUIRE(h.proc.has_active_command()); // refused, still asking
    h.proc.submit_line("0");
    REQUIRE(!h.last<SetPickStyleCommand>()->group_select);
}

TEST_CASE("#53 -PURGE: type, names, Verify per name; Zero-length and Empty text purge at once; No skips the questions") {
    H h;
    RenderSnapshot::PurgeCandidates cand;
    cand.layers = {"DIM", "HATCH", "TEMP"};
    cand.blocks = {"BOLT"};
    h.proc.set_purge_candidates(cand);

    h.proc.submit_line("-PURGE");
    h.proc.submit_line("LA");
    REQUIRE(h.out.prompt == "Enter name(s) to purge <*>: ");
    h.proc.submit_line("");
    REQUIRE(h.out.prompt == "Verify each name to be purged? [Yes/No] <Y>: ");
    h.proc.submit_line("");
    REQUIRE(h.out.prompt == "Purge layer \"DIM\"? [Yes/No/All] <N>: ");
    h.proc.submit_line("Y");
    const auto* p = h.last<musacad::core::PurgeCommand>();
    REQUIRE(p != nullptr);
    REQUIRE(p->what == 4);
    REQUIRE(p->name == "DIM");
    REQUIRE(h.out.prompt == "Purge layer \"HATCH\"? [Yes/No/All] <N>: ");
    h.proc.submit_line("N");
    REQUIRE(h.out.prompt == "Purge layer \"TEMP\"? [Yes/No/All] <N>: ");
    h.proc.submit_line("A");
    REQUIRE(h.last<musacad::core::PurgeCommand>()->name == "TEMP");
    REQUIRE(!h.proc.has_active_command());
    REQUIRE(h.count<musacad::core::PurgeCommand>() == 2);

    h.proc.submit_line("-PURGE");
    h.proc.submit_line("B");
    h.proc.submit_line("BO*");
    h.proc.submit_line("N"); // no verify: one command with the pattern
    REQUIRE(h.last<musacad::core::PurgeCommand>()->what == 1);
    REQUIRE(h.last<musacad::core::PurgeCommand>()->name == "BO*");

    h.proc.submit_line("-PURGE");
    h.proc.submit_line("Z");
    REQUIRE(h.last<musacad::core::PurgeCommand>()->what == 8);
    REQUIRE(!h.proc.has_active_command());
    h.proc.submit_line("-PURGE");
    h.proc.submit_line("E");
    REQUIRE(h.last<musacad::core::PurgeCommand>()->what == 9);
}

TEST_CASE("#67 -UNITS: the numbered tables, the fraction denominator, clockwise; INSUNITS; LTSCALE shows its value") {
    H h;
    h.proc.submit_line("-UNITS");
    REQUIRE(h.out.any_contains("1.  Scientific"));
    REQUIRE(h.out.prompt == "Enter choice, 1 to 5 <2>: ");
    h.proc.submit_line("4"); // Architectural
    REQUIRE(h.out.prompt.find("denominator of smallest fraction") != std::string::npos);
    h.proc.submit_line("32");
    REQUIRE(h.out.any_contains("2.  Degrees/minutes/seconds"));
    h.proc.submit_line("2");
    h.proc.submit_line("2");
    REQUIRE(h.out.any_contains("North  12 o'clock =   90"));
    h.proc.submit_line("90");
    REQUIRE(h.out.prompt == "Do you want angles measured clockwise? [Yes/No] <N>: ");
    h.proc.submit_line("Y");
    const auto* u = h.last<SetUnitsCommand>();
    REQUIRE(u != nullptr);
    REQUIRE(u->units.linear == LinearFormat::Architectural);
    REQUIRE(u->units.linear_precision == 5); // 1/32
    REQUIRE(u->units.angular == AngleFormat::DegMinSec);
    REQUIRE(u->units.angular_precision == 2);
    REQUIRE(u->units.clockwise);
    REQUIRE(u->units.base_angle == Approx(1.5707963));

    h.proc.submit_line("INSUNITS");
    REQUIRE(h.out.prompt == "Enter new value for INSUNITS <0>: ");
    h.proc.submit_line("4");
    REQUIRE(h.last<SetUnitsCommand>()->units.insunits == 4);
    REQUIRE(h.out.any_contains("Millimeters"));

    h.proc.set_ltscales(2.5, 1.0, true, true);
    h.proc.submit_line("LTS");
    REQUIRE(h.out.prompt == "Enter new linetype scale factor <2.5000>: ");
    h.proc.submit_line("0.5");
    REQUIRE(h.last<SetLtscaleCommand>()->scale == Approx(0.5));
    REQUIRE(h.out.any_contains("Regenerating model."));

    h.proc.submit_line("CELTSCALE");
    h.proc.submit_line("2");
    REQUIRE(h.last<SetCeltscaleCommand>()->scale == Approx(2.0));
    h.proc.submit_line("PSLTSCALE");
    REQUIRE(h.out.prompt == "Enter new value for PSLTSCALE <1>: ");
    h.proc.submit_line("0");
    REQUIRE(!h.last<SetLtscaleModesCommand>()->psltscale);
    REQUIRE(h.last<SetLtscaleModesCommand>()->msltscale);
    h.proc.submit_line("MSLTSCALE");
    h.proc.submit_line("2");
    REQUIRE(h.proc.has_active_command()); // 0 or 1 only
    h.proc.submit_line("0");
    REQUIRE(!h.last<SetLtscaleModesCommand>()->msltscale);
}

TEST_CASE("#46 SELECTSIMILAR, QSELECT, ISOLATEOBJECTS and the selection system variables") {
    H h;
    h.proc.set_selection_count(2);
    h.proc.submit_line("SELECTSIMILAR");
    REQUIRE(h.last<musacad::core::SelectSimilarCommand>()->mode == 130);
    h.proc.submit_line("SELECTSIMILARMODE");
    h.proc.submit_line("3");
    h.proc.submit_line("SELECTSIMILAR");
    REQUIRE(h.last<musacad::core::SelectSimilarCommand>()->mode == 3);
    musacad::command::SelectSimilarCommand::s_mode_ = 130; // leave the session default for other tests

    h.proc.set_selection_count(0);
    h.proc.submit_line("SELECTSIMILAR"); // nothing selected: asks first
    REQUIRE(h.proc.in_selection_phase());
    h.proc.submit_line("1,1");
    h.proc.set_selection_count(1);
    h.proc.submit_line("");
    REQUIRE(h.count<musacad::core::SelectSimilarCommand>() == 3);

    h.proc.submit_line("QSELECT");
    REQUIRE(h.view.dialog_opened);
    REQUIRE(!h.proc.has_active_command());

    h.proc.set_selection_count(1);
    h.proc.submit_line("HIDEOBJECTS");
    REQUIRE(h.last<IsolateObjectsCommand>()->mode == 1);
    h.proc.submit_line("UNISOLATEOBJECTS");
    REQUIRE(h.last<IsolateObjectsCommand>()->mode == 2);
    h.proc.set_selection_count(0);
    h.proc.submit_line("ISOLATEOBJECTS");
    REQUIRE(h.proc.in_selection_phase());
    h.proc.submit_line("");
    REQUIRE(!h.proc.has_active_command());
    REQUIRE(h.count<IsolateObjectsCommand>() == 2); // nothing chosen: nothing hidden

    h.proc.submit_line("PICKBOX");
    REQUIRE(h.out.prompt == "Enter new value for PICKBOX <10>: ");
    h.proc.submit_line("60");
    REQUIRE(h.out.any_contains("out of range"));
    h.proc.submit_line("6");
    REQUIRE(h.view.pickbox == 6);
    REQUIRE(!h.proc.has_active_command());
}

TEST_CASE("#38 Enter at a point prompt with no default asks again without complaint") {
    H h;
    h.proc.submit_line("C"); // CIRCLE's centre prompt has options but no default
    h.proc.submit_line("");
    REQUIRE(h.proc.has_active_command());
    REQUIRE(!h.out.any_contains("Empty coordinate"));
    h.proc.cancel();
}
