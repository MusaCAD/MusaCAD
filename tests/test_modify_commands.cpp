// Command-flow tests: the Phase-10 Modify state machines, driven through the
// CommandProcessor exactly as the command line does, emit the right messages.

#include <algorithm>
#include <cmath>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/core/command.hpp"
#include "musacad/core/math/math.hpp"

using namespace musacad::command;
using Catch::Approx;

namespace {
struct SilentOutput : CommandOutput {
    void append_line(const std::string&) override {}
    void set_prompt(const std::string&) override {}
};
struct Harness {
    std::vector<musacad::core::Command> cmds;
    SilentOutput out;
    CommandProcessor proc{[this](musacad::core::Command c) { cmds.push_back(std::move(c)); }, nullptr,
                          out};
};
} // namespace

TEST_CASE("ROTATE flow emits RotateSelectionCommand with the typed angle") {
    Harness h;
    h.proc.set_selection_count(1); // ROTATE requires a selection
    h.proc.submit_line("RO");
    h.proc.submit_line("0,0"); // base point
    h.proc.submit_line("90");  // degrees
    REQUIRE(h.cmds.size() == 1);
    const auto* rot = std::get_if<musacad::core::RotateSelectionCommand>(&h.cmds[0]);
    REQUIRE(rot != nullptr);
    REQUIRE(rot->angle == Approx(musacad::core::kHalfPi));
}

TEST_CASE("ROTATE with no selection asks Select objects and ends when nothing is chosen") {
    Harness h;
    h.proc.set_selection_count(0);
    h.proc.submit_line("RO");
    REQUIRE(h.proc.in_selection_phase()); // verb-noun (#46)
    h.proc.submit_line("");               // nothing chosen
    REQUIRE(!h.proc.has_active_command());
    REQUIRE(h.cmds.empty());
}

TEST_CASE("SCALE flow emits ScaleSelectionCommand with the typed factor") {
    Harness h;
    h.proc.set_selection_count(2);
    h.proc.submit_line("SC");
    h.proc.submit_line("0,0");
    h.proc.submit_line("3");
    REQUIRE(h.cmds.size() == 1);
    const auto* sc = std::get_if<musacad::core::ScaleSelectionCommand>(&h.cmds[0]);
    REQUIRE(sc != nullptr);
    REQUIRE(sc->factor == Approx(3.0));
}

TEST_CASE("ARRAY rectangular flow emits ArrayRectCommand") {
    Harness h;
    h.proc.set_selection_count(1);
    h.proc.submit_line("AR");
    h.proc.submit_line("R"); // rectangular
    h.proc.submit_line("2"); // rows
    h.proc.submit_line("3"); // cols
    h.proc.submit_line("10"); // row spacing
    h.proc.submit_line("15"); // col spacing
    REQUIRE(h.cmds.size() == 1);
    const auto* ar = std::get_if<musacad::core::ArrayRectCommand>(&h.cmds[0]);
    REQUIRE(ar != nullptr);
    REQUIRE(ar->rows == 2);
    REQUIRE(ar->cols == 3);
    REQUIRE(ar->dx == Approx(15.0));
    REQUIRE(ar->dy == Approx(10.0));
}

TEST_CASE("FILLET flow emits FilletPickCommand with radius and two picks") {
    Harness h;
    h.proc.submit_line("F");
    h.proc.submit_line("R");     // [Radius]
    h.proc.submit_line("2");
    h.proc.submit_line("10,0");  // first line pick
    h.proc.submit_line("0,10");  // second line pick
    REQUIRE(h.cmds.size() == 1);
    const auto* f = std::get_if<musacad::core::FilletPickCommand>(&h.cmds[0]);
    REQUIRE(f != nullptr);
    REQUIRE(f->radius == Approx(2.0));
    REQUIRE(f->pick1.x == Approx(10.0));
    REQUIRE(f->pick2.y == Approx(10.0));
}

TEST_CASE("CHAMFER flow emits ChamferPickCommand with two distances") {
    Harness h;
    h.proc.submit_line("CHA");
    h.proc.submit_line("D");    // [Distance]
    h.proc.submit_line("2");    // dist1
    h.proc.submit_line("3");    // dist2
    h.proc.submit_line("10,0"); // first line
    h.proc.submit_line("0,10"); // second line
    REQUIRE(h.cmds.size() == 1);
    const auto* c = std::get_if<musacad::core::ChamferPickCommand>(&h.cmds[0]);
    REQUIRE(c != nullptr);
    REQUIRE(c->dist1 == Approx(2.0));
    REQUIRE(c->dist2 == Approx(3.0));
}

TEST_CASE("CHAMFER Angle method (length + angle, default 45) emits equal distances") {
    Harness h;
    h.proc.submit_line("CHA");
    h.proc.submit_line("A");    // Angle method
    h.proc.submit_line("5");    // chamfer length on first line
    h.proc.submit_line("45");   // angle -> dist2 = 5 * tan(45) = 5
    h.proc.submit_line("10,0");
    h.proc.submit_line("0,10");
    REQUIRE(h.cmds.size() == 1);
    const auto* c = std::get_if<musacad::core::ChamferPickCommand>(&h.cmds[0]);
    REQUIRE(c != nullptr);
    REQUIRE(c->dist1 == Approx(5.0));
    REQUIRE(c->dist2 == Approx(5.0)); // 45 degrees => equal legs
}

TEST_CASE("EXTEND flow emits ExtendPickCommand per pick") {
    Harness h;
    h.proc.submit_line("EX");
    h.proc.submit_line("5,0");
    REQUIRE(h.cmds.size() == 1);
    REQUIRE(std::holds_alternative<musacad::core::ExtendPickCommand>(h.cmds[0]));
}

// ---------------------------------------------------------------------------
// ROTATE / SCALE: AutoCAD's Reference flows -- the reference by a value or two points,
// the new value by a value, a point from the base, or [Points]; the prompts as AutoCAD
// words them; the live band's reference carried in the preview spec.
// ---------------------------------------------------------------------------
namespace {
struct PromptOutput : CommandOutput {
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompts.push_back(p); }
    std::vector<std::string> lines;
    std::vector<std::string> prompts;
};
struct PromptHarness {
    std::vector<musacad::core::Command> cmds;
    PromptOutput out;
    CommandProcessor proc{[this](musacad::core::Command c) { cmds.push_back(std::move(c)); }, nullptr,
                          out};
    template <class T>
    const T* last() const {
        const T* found = nullptr;
        for (const auto& c : cmds) {
            if (const auto* p = std::get_if<T>(&c)) {
                found = p;
            }
        }
        return found;
    }
};
} // namespace

TEST_CASE("SCALE: the factor prompt offers Copy/Reference at once; a point is the distance from the base") {
    PromptHarness h;
    h.proc.set_selection_count(1);
    h.proc.submit_line("SC");
    h.proc.submit_line("10,10");
    REQUIRE(h.out.prompts.back() == "Specify scale factor or [Copy/Reference]: ");
    REQUIRE(h.proc.preview().kind == PreviewKind::Scale);
    REQUIRE(h.proc.preview().ref_length == Approx(1.0));
    h.proc.submit_line("13,14"); // 5 units away -> factor 5
    const auto* sc = h.last<musacad::core::ScaleSelectionCommand>();
    REQUIRE(sc != nullptr);
    REQUIRE(sc->factor == Approx(5.0));
    REQUIRE(sc->base.x == Approx(10.0));
}

TEST_CASE("SCALE Reference: two points for the reference length, then a typed new length") {
    PromptHarness h;
    h.proc.set_selection_count(1);
    h.proc.submit_line("SC");
    h.proc.submit_line("0,0");
    h.proc.submit_line("R");
    REQUIRE(h.out.prompts.back() == "Specify reference length <1.0000>: ");
    h.proc.submit_line("0,0"); // a point starts a two-point length
    REQUIRE(h.out.prompts.back() == "Specify second point: ");
    h.proc.submit_line("4,0"); // reference length 4
    REQUIRE(h.out.prompts.back() == "Specify new length or [Points] <1.0000>: ");
    REQUIRE(h.proc.preview().kind == PreviewKind::Scale);
    REQUIRE(h.proc.preview().ref_length == Approx(4.0)); // the band divides by it
    h.proc.submit_line("10"); // new length 10 -> factor 2.5
    const auto* sc = h.last<musacad::core::ScaleSelectionCommand>();
    REQUIRE(sc != nullptr);
    REQUIRE(sc->factor == Approx(2.5));
}

TEST_CASE("SCALE Reference: [Points] measures the new length between two picks") {
    PromptHarness h;
    h.proc.set_selection_count(1);
    h.proc.submit_line("SC");
    h.proc.submit_line("0,0");
    h.proc.submit_line("REFERENCE");
    h.proc.submit_line("2"); // typed reference length
    h.proc.submit_line("P");
    REQUIRE(h.out.prompts.back() == "Specify first point: ");
    h.proc.submit_line("100,100");
    REQUIRE(h.out.prompts.back() == "Specify second point: ");
    h.proc.submit_line("100,106"); // new length 6 -> factor 3
    const auto* sc = h.last<musacad::core::ScaleSelectionCommand>();
    REQUIRE(sc != nullptr);
    REQUIRE(sc->factor == Approx(3.0));
    // Enter at the new-length prompt takes the default 1; a zero reference is refused.
    PromptHarness z;
    z.proc.set_selection_count(1);
    z.proc.submit_line("SC");
    z.proc.submit_line("0,0");
    z.proc.submit_line("R");
    z.proc.submit_line("0");
    REQUIRE(z.out.lines.back() == "Value must be positive and nonzero.");
    z.proc.submit_line("4");
    z.proc.submit_line("");
    const auto* zc = z.last<musacad::core::ScaleSelectionCommand>();
    REQUIRE(zc != nullptr);
    REQUIRE(zc->factor == Approx(0.25));
}

TEST_CASE("ROTATE Reference: two points for the reference angle, [Points] for the new one") {
    PromptHarness h;
    h.proc.set_selection_count(1);
    h.proc.submit_line("RO");
    REQUIRE(std::any_of(h.out.lines.begin(), h.out.lines.end(), [](const std::string& l) {
        return l.rfind("Current positive angle in UCS:", 0) == 0;
    }));
    h.proc.submit_line("0,0");
    REQUIRE(h.out.prompts.back() == "Specify rotation angle or [Copy/Reference] <0>: ");
    h.proc.submit_line("R");
    REQUIRE(h.out.prompts.back() == "Specify the reference angle <0>: ");
    h.proc.submit_line("0,0");
    REQUIRE(h.out.prompts.back() == "Specify second point: ");
    h.proc.submit_line("1,1"); // reference 45 degrees
    REQUIRE(h.out.prompts.back() == "Specify the new angle or [Points] <0>: ");
    REQUIRE(h.proc.preview().kind == PreviewKind::Rotate);
    REQUIRE(h.proc.preview().ref_angle == Approx(musacad::core::kHalfPi / 2.0));
    h.proc.submit_line("P");
    h.proc.submit_line("5,5");
    h.proc.submit_line("5,9"); // new angle 90 -> rotate by 45
    const auto* rot = h.last<musacad::core::RotateSelectionCommand>();
    REQUIRE(rot != nullptr);
    REQUIRE(rot->angle == Approx(musacad::core::kHalfPi / 2.0));
}

TEST_CASE("ROTATE: a typed new angle less the typed reference; Esc ends the band") {
    PromptHarness h;
    h.proc.set_selection_count(1);
    h.proc.submit_line("RO");
    h.proc.submit_line("0,0");
    h.proc.submit_line("R");
    h.proc.submit_line("30");
    h.proc.submit_line("90");
    const auto* rot = h.last<musacad::core::RotateSelectionCommand>();
    REQUIRE(rot != nullptr);
    REQUIRE(rot->angle == Approx(musacad::core::to_radians(60.0)));

    PromptHarness c;
    c.proc.set_selection_count(1);
    c.proc.submit_line("RO");
    c.proc.submit_line("0,0");
    c.proc.cancel();
    const auto* band = c.last<musacad::core::TransformPreviewCommand>();
    REQUIRE(band != nullptr);
    REQUIRE(!band->active);
    REQUIRE(c.last<musacad::core::RotateSelectionCommand>() == nullptr);
}

TEST_CASE("LINE: direct distance entry draws along the cursor; Close; Continue from an arc asks a length") {
    PromptHarness h;
    h.proc.submit_line("L");
    h.proc.submit_line("0,0");
    h.proc.set_cursor_world({3.0, 4.0}); // the cursor sits on the 3-4-5 direction
    h.proc.submit_line("10");
    const auto* l1 = h.last<musacad::core::AddLineCommand>();
    REQUIRE(l1 != nullptr);
    REQUIRE(l1->b.x == Approx(6.0));
    REQUIRE(l1->b.y == Approx(8.0));
    h.proc.submit_line("6,0");
    REQUIRE(h.out.prompts.back() == "Specify next point or [Close/Undo]: ");
    h.proc.submit_line("CLOSE");
    const auto* l3 = h.last<musacad::core::AddLineCommand>();
    REQUIRE(l3->b.x == Approx(0.0).margin(1e-9));
    REQUIRE(l3->b.y == Approx(0.0).margin(1e-9));
    REQUIRE(!h.proc.has_active_command());

    // An arc ending at (0, 10) heading left; LINE + Enter continues tangent: a length.
    h.proc.submit_line("A");
    h.proc.submit_line("10,0");
    h.proc.submit_line("C");
    h.proc.submit_line("0,0");
    h.proc.submit_line("0,20");
    h.proc.submit_line("L");
    h.proc.submit_line("");
    REQUIRE(h.out.prompts.back() == "Specify length of line: ");
    h.proc.submit_line("5");
    const auto* tangent = h.last<musacad::core::AddLineCommand>();
    REQUIRE(tangent != nullptr);
    REQUIRE(tangent->a.x == Approx(0.0).margin(1e-9));
    REQUIRE(tangent->a.y == Approx(10.0));
    REQUIRE(tangent->b.x == Approx(-5.0));
    REQUIRE(tangent->b.y == Approx(10.0));
    REQUIRE(h.out.prompts.back() == "Specify next point or [Undo]: ");
}

// ---------------------------------------------------------------------------
// FILLET / CHAMFER (#49): AutoCAD's prompt order (objects first, the settings as
// options), the remembered settings, Multiple with its own undo step per corner,
// Undo, Trim / No trim, Polyline, and Shift at the second pick for a sharp corner.
// ---------------------------------------------------------------------------
TEST_CASE("FILLET: options first, remembered radius, Multiple + Undo, Trim mode, Polyline, Shift corner") {
    PromptHarness h;
    h.proc.set_pick_radius(1.0);
    h.proc.submit_line("F");
    REQUIRE(std::any_of(h.out.lines.begin(), h.out.lines.end(), [](const std::string& l) {
        return l.rfind("Current settings: Mode = TRIM, Radius = ", 0) == 0;
    }));
    REQUIRE(h.out.prompts.back() == "Select first object or [Undo/Polyline/Radius/Trim/Multiple]: ");
    h.proc.submit_line("RADIUS");
    REQUIRE(h.out.prompts.back().rfind("Specify fillet radius <", 0) == 0);
    h.proc.submit_line("3");
    h.proc.submit_line("M"); // Multiple
    h.proc.submit_line("10,0");
    REQUIRE(h.out.prompts.back() == "Select second object or shift-select to apply corner or [Radius]: ");
    h.proc.submit_line("0,10");
    const auto* f1 = h.last<musacad::core::FilletPickCommand>();
    REQUIRE(f1 != nullptr);
    REQUIRE(f1->radius == Approx(3.0));
    REQUIRE(f1->trim);
    REQUIRE(h.proc.has_active_command()); // Multiple: back to the first prompt
    REQUIRE(h.out.prompts.back() == "Select first object or [Undo/Polyline/Radius/Trim/Multiple]: ");
    const std::uint64_t g1 = f1->group;
    h.proc.submit_line("20,0");
    h.proc.set_shift_held(true); // shift-select: a sharp corner this once
    h.proc.submit_line("0,20");
    h.proc.set_shift_held(false);
    const auto* f2 = h.last<musacad::core::FilletPickCommand>();
    REQUIRE(f2->radius == Approx(0.0));
    REQUIRE(f2->group != g1); // its own undo step
    h.proc.submit_line("U");
    REQUIRE(h.last<musacad::core::UndoLastGroupCommand>() != nullptr);
    h.proc.submit_line("T");
    REQUIRE(h.out.prompts.back() == "Enter Trim mode option [Trim/No trim] <Trim>: ");
    h.proc.submit_line("N");
    h.proc.submit_line("P");
    REQUIRE(h.out.prompts.back() == "Select 2D polyline or [Radius]: ");
    h.proc.submit_line("5,5");
    const auto* fp = h.last<musacad::core::FilletPolylineCommand>();
    REQUIRE(fp != nullptr);
    REQUIRE(fp->radius == Approx(3.0));
    h.proc.submit_line(""); // Enter ends Multiple
    REQUIRE(!h.proc.has_active_command());
    // The radius and the mode are remembered: the next run says so, and No trim travels.
    PromptHarness n;
    n.proc.submit_line("FILLET");
    REQUIRE(std::any_of(n.out.lines.begin(), n.out.lines.end(), [](const std::string& l) {
        return l == "Current settings: Mode = NOTRIM, Radius = 3.0000";
    }));
    n.proc.submit_line("1,0");
    n.proc.submit_line("0,1");
    REQUIRE(!n.last<musacad::core::FilletPickCommand>()->trim);
    // Put the session back for the other cases.
    PromptHarness r;
    r.proc.submit_line("F");
    r.proc.submit_line("T");
    r.proc.submit_line("T");
    r.proc.submit_line("R");
    r.proc.submit_line("0");
    r.proc.cancel();
}

TEST_CASE("CHAMFER: the method is remembered; Angle, mEthod, Trim, Polyline, Undo") {
    PromptHarness h;
    h.proc.submit_line("CHA");
    REQUIRE(h.out.prompts.back() == "Select first line or [Undo/Polyline/Distance/Angle/Trim/mEthod/Multiple]: ");
    h.proc.submit_line("A");
    h.proc.submit_line("4");
    REQUIRE(h.out.prompts.back().rfind("Specify chamfer angle from the first line <", 0) == 0);
    h.proc.submit_line("30");
    h.proc.submit_line("10,0");
    REQUIRE(h.out.prompts.back() == "Select second line or shift-select to apply corner or [Distance/Angle/Method]: ");
    h.proc.submit_line("0,10");
    const auto* c = h.last<musacad::core::ChamferPickCommand>();
    REQUIRE(c != nullptr);
    REQUIRE(c->dist1 == Approx(4.0));
    REQUIRE(c->dist2 == Approx(4.0 * std::tan(musacad::core::to_radians(30.0))));
    REQUIRE(!h.proc.has_active_command());
    // The next run reports the Angle method and keeps using it.
    PromptHarness n;
    n.proc.submit_line("CHAMFER");
    REQUIRE(std::any_of(n.out.lines.begin(), n.out.lines.end(), [](const std::string& l) {
        return l.rfind("(TRIM mode) Current chamfer Length = 4.0000, Angle = 30", 0) == 0;
    }));
    n.proc.submit_line("E"); // mEthod
    REQUIRE(n.out.prompts.back() == "Enter trim method [Distance/Angle] <Angle>: ");
    n.proc.submit_line("D");
    n.proc.submit_line("D"); // Distance: the values
    n.proc.submit_line("2");
    REQUIRE(n.out.prompts.back() == "Specify second chamfer distance <2.0000>: ");
    n.proc.submit_line("");
    n.proc.submit_line("M");
    n.proc.submit_line("P");
    REQUIRE(n.out.prompts.back() == "Select 2D polyline or [Distance/Angle/mEthod]: ");
    n.proc.submit_line("5,5");
    const auto* cp = n.last<musacad::core::ChamferPolylineCommand>();
    REQUIRE(cp != nullptr);
    REQUIRE(cp->dist1 == Approx(2.0));
    REQUIRE(cp->dist2 == Approx(2.0));
    REQUIRE(n.proc.has_active_command());
    n.proc.submit_line("U");
    REQUIRE(n.last<musacad::core::UndoLastGroupCommand>() != nullptr);
    n.proc.submit_line("T");
    n.proc.submit_line("NO");
    n.proc.submit_line("1,0");
    n.proc.submit_line("0,1");
    REQUIRE(!n.last<musacad::core::ChamferPickCommand>()->trim);
    n.proc.cancel();
    PromptHarness r; // back to the defaults for the other cases
    r.proc.submit_line("CHA");
    r.proc.submit_line("T");
    r.proc.submit_line("T");
    r.proc.submit_line("D");
    r.proc.submit_line("0");
    r.proc.submit_line("0");
    r.proc.cancel();
}

// ---------------------------------------------------------------------------
// MOVE / COPY (#47): Displacement, the first point as the displacement, COPY's mode,
// Array (and Fit), Undo and Exit, every copy its own undo step.
// ---------------------------------------------------------------------------
TEST_CASE("MOVE: Displacement, and Enter at the second point uses the first as the vector") {
    PromptHarness h;
    h.proc.set_selection_count(1);
    h.proc.submit_line("M");
    REQUIRE(h.out.prompts.back() == "Specify base point or [Displacement] <Displacement>: ");
    h.proc.submit_line("D");
    REQUIRE(h.out.prompts.back().rfind("Specify displacement <", 0) == 0);
    h.proc.submit_line("3,4");
    const auto* mv = h.last<musacad::core::MoveSelectionCommand>();
    REQUIRE(mv != nullptr);
    REQUIRE(mv->delta.x == Approx(3.0));
    REQUIRE(mv->delta.y == Approx(4.0));
    h.proc.submit_line("M");
    h.proc.submit_line("5,6");
    REQUIRE(h.out.prompts.back() == "Specify second point or <use first point as displacement>: ");
    h.proc.submit_line("");
    const auto* mv2 = h.last<musacad::core::MoveSelectionCommand>();
    REQUIRE(mv2->delta.x == Approx(5.0));
    REQUIRE(mv2->delta.y == Approx(6.0));
    // Enter at the base prompt takes Displacement with the last vector as the default.
    h.proc.submit_line("MOVE");
    h.proc.submit_line("");
    REQUIRE(h.out.prompts.back() == "Specify displacement <5.0000, 6.0000, 0.0000>: ");
    h.proc.submit_line("");
    REQUIRE(h.last<musacad::core::MoveSelectionCommand>()->delta.x == Approx(5.0));
}

TEST_CASE("COPY: Multiple with Undo and Exit, Array and Fit, Single mode") {
    PromptHarness h;
    h.proc.set_selection_count(1);
    h.proc.submit_line("CO");
    REQUIRE(std::any_of(h.out.lines.begin(), h.out.lines.end(), [](const std::string& l) {
        return l == "Current settings: Copy mode = Multiple";
    }));
    REQUIRE(h.out.prompts.back() == "Specify base point or [Displacement/mOde] <Displacement>: ");
    h.proc.submit_line("0,0");
    REQUIRE(h.out.prompts.back() == "Specify second point or [Array] <use first point as displacement>: ");
    h.proc.submit_line("10,0");
    REQUIRE(h.out.prompts.back() == "Specify second point or [Array/Exit/Undo] <Exit>: ");
    h.proc.submit_line("20,0");
    std::vector<const musacad::core::CopySelectionCommand*> copies;
    for (const auto& c : h.cmds) {
        if (const auto* p = std::get_if<musacad::core::CopySelectionCommand>(&c)) {
            copies.push_back(p);
        }
    }
    REQUIRE(copies.size() == 2);
    REQUIRE(copies[0]->group != copies[1]->group); // each its own undo step
    h.proc.submit_line("U");
    REQUIRE(h.last<musacad::core::UndoLastGroupCommand>() != nullptr);
    h.proc.submit_line("A");
    REQUIRE(h.out.prompts.back() == "Enter number of items to array: ");
    h.proc.submit_line("4");
    REQUIRE(h.out.prompts.back() == "Specify second point or [Fit]: ");
    h.proc.submit_line("F");
    REQUIRE(h.out.prompts.back() == "Specify second point or [Array]: ");
    h.proc.submit_line("30,0"); // 4 items fitted between the base and (30, 0): 10 apart
    copies.clear();
    for (const auto& c : h.cmds) {
        if (const auto* p = std::get_if<musacad::core::CopySelectionCommand>(&c)) {
            copies.push_back(p);
        }
    }
    REQUIRE(copies.size() == 5);
    REQUIRE(copies[2]->delta.x == Approx(10.0));
    REQUIRE(copies[3]->delta.x == Approx(20.0));
    REQUIRE(copies[4]->delta.x == Approx(30.0));
    h.proc.submit_line("E");
    REQUIRE(!h.proc.has_active_command());

    // Single mode: one copy ends the command; Enter at the first placement uses the base
    // point as the displacement.
    h.proc.submit_line("CO");
    h.proc.submit_line("O");
    REQUIRE(h.out.prompts.back() == "Enter a copy mode option [Single/Multiple] <Multiple>: ");
    h.proc.submit_line("S");
    h.proc.submit_line("1,1");
    h.proc.submit_line("");
    REQUIRE(h.last<musacad::core::CopySelectionCommand>()->delta.x == Approx(1.0));
    REQUIRE(!h.proc.has_active_command());
    h.proc.submit_line("CO");
    h.proc.submit_line("O");
    h.proc.submit_line("M"); // back to Multiple for the other cases
    h.proc.cancel();
}

// ---------------------------------------------------------------------------
// OFFSET (#50): the settings echoed, Through / Erase / Layer, a two-point distance,
// Exit / Multiple / Undo, every offset its own undo step.
// ---------------------------------------------------------------------------
TEST_CASE("OFFSET: options, two-point distance, Multiple steps from the last offset, Undo") {
    PromptHarness h;
    h.proc.set_pick_radius(1.0);
    h.proc.submit_line("O");
    REQUIRE(std::any_of(h.out.lines.begin(), h.out.lines.end(), [](const std::string& l) {
        return l == "Current settings: Erase source=No  Layer=Source  OFFSETGAPTYPE=0";
    }));
    REQUIRE(h.out.prompts.back().rfind("Specify offset distance or [Through/Erase/Layer] <", 0) == 0);
    h.proc.submit_line("E");
    REQUIRE(h.out.prompts.back() == "Erase source object after offsetting? [Yes/No] <No>: ");
    h.proc.submit_line("Y");
    h.proc.submit_line("L");
    REQUIRE(h.out.prompts.back() == "Enter layer option for offset objects [Current/Source] <Source>: ");
    h.proc.submit_line("C");
    h.proc.submit_line("0,0"); // two points: the distance is 5
    REQUIRE(h.out.prompts.back() == "Specify second point: ");
    h.proc.submit_line("3,4");
    REQUIRE(h.out.prompts.back() == "Select object to offset or [Exit/Undo] <Exit>: ");
    h.proc.submit_line("10,0");
    REQUIRE(h.out.prompts.back() == "Specify point on side to offset or [Exit/Multiple/Undo] <Exit>: ");
    h.proc.submit_line("M");
    h.proc.submit_line("10,5");
    h.proc.submit_line("10,9");
    std::vector<const musacad::core::OffsetPickCommand*> offs;
    for (const auto& c : h.cmds) {
        if (const auto* p = std::get_if<musacad::core::OffsetPickCommand>(&c)) {
            offs.push_back(p);
        }
    }
    REQUIRE(offs.size() == 2);
    REQUIRE(offs[0]->distance == Approx(5.0));
    REQUIRE(offs[0]->erase_source);
    REQUIRE(offs[0]->to_current_layer);
    REQUIRE(!offs[0]->from_last);
    REQUIRE(offs[1]->from_last); // Multiple: from the offset just made
    REQUIRE(offs[0]->group != offs[1]->group);
    h.proc.submit_line("U");
    REQUIRE(h.last<musacad::core::UndoLastGroupCommand>() != nullptr);
    h.proc.submit_line(""); // out of Multiple, back to the object prompt
    REQUIRE(h.out.prompts.back() == "Select object to offset or [Exit/Undo] <Exit>: ");
    h.proc.submit_line("EXIT");
    REQUIRE(!h.proc.has_active_command());

    // Through: the distance prompt says so and the side prompt asks the through point.
    h.proc.submit_line("OFFSET");
    REQUIRE(h.out.prompts.back() == "Specify offset distance or [Through/Erase/Layer] <5.0000>: ");
    h.proc.submit_line("T");
    h.proc.submit_line("10,0");
    REQUIRE(h.out.prompts.back() == "Specify through point or [Exit/Multiple/Undo] <Exit>: ");
    h.proc.submit_line("10,7");
    REQUIRE(h.last<musacad::core::OffsetPickCommand>()->through);
    REQUIRE(h.last<musacad::core::OffsetPickCommand>()->side.y == Approx(7.0));
    h.proc.cancel();
    PromptHarness r; // the defaults back for the other cases
    r.proc.submit_line("O");
    r.proc.submit_line("E");
    r.proc.submit_line("N");
    r.proc.submit_line("L");
    r.proc.submit_line("S");
    r.proc.submit_line("1");
    r.proc.cancel();
}
