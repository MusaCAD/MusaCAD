// Command-flow tests: the Phase-10 Modify state machines, driven through the
// CommandProcessor exactly as the command line does, emit the right messages.

#include <algorithm>
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

TEST_CASE("ROTATE with no selection does nothing") {
    Harness h;
    h.proc.set_selection_count(0);
    h.proc.submit_line("RO");
    h.proc.submit_line("0,0");
    h.proc.submit_line("90");
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
    h.proc.submit_line("2");     // radius
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
