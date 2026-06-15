#include <cmath>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/command/command_processor.hpp"
#include "musacad/command/commands.hpp"
#include "musacad/core/command.hpp"

using namespace musacad::command;
using musacad::core::Vec2;
using Catch::Approx;

namespace {

struct CaptureOutput : CommandOutput {
    std::vector<std::string> lines;
    std::string prompt;
    void append_line(const std::string& l) override { lines.push_back(l); }
    void set_prompt(const std::string& p) override { prompt = p; }
    bool any_contains(const std::string& sub) const {
        for (const auto& l : lines) {
            if (l.find(sub) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
};

struct FakeView : ViewControl {
    int extents = 0;
    double scale = 0.0;
    void zoom_extents() override { ++extents; }
    void zoom_scale(double f) override { scale = f; }
};

struct Harness {
    std::vector<musacad::core::Command> cmds;
    CaptureOutput out;
    FakeView view;
    CommandProcessor proc{[this](musacad::core::Command c) { cmds.push_back(std::move(c)); }, &view,
                          out};
};

} // namespace

TEST_CASE("Processor: canonical LINE 0,0 @100,0 ESC draws a horizontal line") {
    Harness h;
    h.proc.submit_line("L");
    h.proc.submit_line("0,0");
    h.proc.submit_line("@100,0");
    h.proc.cancel(); // ESC

    REQUIRE(h.cmds.size() == 1);
    const auto* line = std::get_if<musacad::core::AddLineCommand>(&h.cmds[0]);
    REQUIRE(line != nullptr);
    REQUIRE(line->a == Vec2{0.0, 0.0});
    REQUIRE(line->b == Vec2{100.0, 0.0});
    REQUIRE_FALSE(h.proc.has_active_command());
}

TEST_CASE("Processor: malformed coordinate re-prompts without aborting") {
    Harness h;
    h.proc.submit_line("L");
    h.proc.submit_line("0,0");
    h.proc.submit_line("not-a-point"); // rejected, command stays active
    REQUIRE(h.proc.has_active_command());
    REQUIRE(h.out.any_contains("Malformed"));
    REQUIRE(h.cmds.empty());
    h.proc.submit_line("@100,0"); // now valid
    REQUIRE(h.cmds.size() == 1);
}

TEST_CASE("Processor: CIRCLE center + radius") {
    Harness h;
    h.proc.submit_line("C");
    h.proc.submit_line("5,5");
    h.proc.submit_line("3");
    REQUIRE(h.cmds.size() == 1);
    const auto* c = std::get_if<musacad::core::AddCircleCommand>(&h.cmds[0]);
    REQUIRE(c != nullptr);
    REQUIRE(c->center == Vec2{5.0, 5.0});
    REQUIRE(c->radius == Approx(3.0));
}

TEST_CASE("Processor: RECTANGLE makes a closed 4-point polyline") {
    Harness h;
    h.proc.submit_line("REC");
    h.proc.submit_line("0,0");
    h.proc.submit_line("10,5");
    REQUIRE(h.cmds.size() == 1);
    const auto* pl = std::get_if<musacad::core::AddPolylineCommand>(&h.cmds[0]);
    REQUIRE(pl != nullptr);
    REQUIRE(pl->closed);
    REQUIRE(pl->points.size() == 4);
    REQUIRE(pl->points[2] == Vec2{10.0, 5.0});
}

namespace {
// Pull the single committed closed polyline out of the harness.
const musacad::core::AddPolylineCommand* only_polyline(const Harness& h) {
    if (h.cmds.size() != 1) {
        return nullptr;
    }
    return std::get_if<musacad::core::AddPolylineCommand>(&h.cmds[0]);
}
} // namespace

TEST_CASE("RECTANGLE Dimensions: 50x30, quadrant follows the placement click") {
    SECTION("click NE of the first corner -> extends NE") {
        Harness h;
        h.proc.submit_line("REC");
        h.proc.submit_line("0,0"); // first corner
        h.proc.submit_line("D");   // Dimensions option keyword
        h.proc.submit_line("50");  // length
        h.proc.submit_line("30");  // width
        h.proc.submit_line("10,10"); // placement click, NE quadrant
        const auto* pl = only_polyline(h);
        REQUIRE(pl != nullptr);
        REQUIRE(pl->closed);
        REQUIRE(pl->points.size() == 4);
        REQUIRE(pl->points[0] == Vec2{0.0, 0.0});
        REQUIRE(pl->points[1] == Vec2{50.0, 0.0});
        REQUIRE(pl->points[2] == Vec2{50.0, 30.0});
        REQUIRE(pl->points[3] == Vec2{0.0, 30.0});
    }
    SECTION("click SW of the first corner -> same 50x30 extends SW") {
        Harness h;
        h.proc.submit_line("REC");
        h.proc.submit_line("0,0");
        h.proc.submit_line("D");
        h.proc.submit_line("50");
        h.proc.submit_line("30");
        h.proc.submit_line("-10,-10"); // placement click, SW quadrant
        const auto* pl = only_polyline(h);
        REQUIRE(pl != nullptr);
        REQUIRE(pl->points[1] == Vec2{-50.0, 0.0});
        REQUIRE(pl->points[2] == Vec2{-50.0, -30.0});
        REQUIRE(pl->points[3] == Vec2{0.0, -30.0});
    }
}

TEST_CASE("RECTANGLE Area: requested area within float tolerance, L and W paths") {
    SECTION("Length given -> width computed") {
        Harness h;
        h.proc.submit_line("REC");
        h.proc.submit_line("0,0");
        h.proc.submit_line("A");    // Area option
        h.proc.submit_line("1500"); // area
        h.proc.submit_line("L");    // calculate from Length
        h.proc.submit_line("50");   // length -> width = 1500/50 = 30
        h.proc.submit_line("10,10");
        const auto* pl = only_polyline(h);
        REQUIRE(pl != nullptr);
        REQUIRE(pl->points[2].x == Approx(50.0));
        REQUIRE(pl->points[2].y == Approx(30.0));
        const double area = std::abs(pl->points[2].x) * std::abs(pl->points[2].y);
        REQUIRE(area == Approx(1500.0));
    }
    SECTION("Width given -> length computed") {
        Harness h;
        h.proc.submit_line("REC");
        h.proc.submit_line("0,0");
        h.proc.submit_line("A");
        h.proc.submit_line("1500");
        h.proc.submit_line("W");  // calculate from Width
        h.proc.submit_line("30"); // width -> length = 1500/30 = 50
        h.proc.submit_line("10,10");
        const auto* pl = only_polyline(h);
        REQUIRE(pl != nullptr);
        REQUIRE(pl->points[2].x == Approx(50.0));
        REQUIRE(pl->points[2].y == Approx(30.0));
    }
}

TEST_CASE("RECTANGLE Rotation: fixed-size rectangle is rotated about the first corner") {
    Harness h;
    h.proc.submit_line("REC");
    h.proc.submit_line("0,0");
    h.proc.submit_line("R");   // Rotation option
    h.proc.submit_line("90");  // 90 degrees
    h.proc.submit_line("D");   // then Dimensions 50x30
    h.proc.submit_line("50");
    h.proc.submit_line("30");
    h.proc.submit_line("10,10"); // NE quadrant, then rotated 90 deg about (0,0)
    const auto* pl = only_polyline(h);
    REQUIRE(pl != nullptr);
    // (50,0) rotated +90 about origin -> (0,50); (50,30) -> (-30,50).
    REQUIRE(pl->points[1].x == Approx(0.0).margin(1e-9));
    REQUIRE(pl->points[1].y == Approx(50.0));
    REQUIRE(pl->points[2].x == Approx(-30.0));
    REQUIRE(pl->points[2].y == Approx(50.0));
}

TEST_CASE("RECTANGLE: a non-number at a value prompt reverts to the corner pick") {
    Harness h;
    h.proc.submit_line("REC");
    h.proc.submit_line("0,0");
    h.proc.submit_line("D");     // Dimensions
    h.proc.submit_line("oops");  // not a number -> revert to other-corner pick
    REQUIRE(h.proc.has_active_command());
    REQUIRE(h.cmds.empty());
    h.proc.submit_line("10,5");  // now a normal two-point corner
    const auto* pl = only_polyline(h);
    REQUIRE(pl != nullptr);
    REQUIRE(pl->points[2] == Vec2{10.0, 5.0}); // corner-to-corner, unchanged behaviour
}

TEST_CASE("RECTANGLE: ESC after the first corner cancels cleanly") {
    Harness h;
    h.proc.submit_line("REC");
    h.proc.submit_line("0,0");
    h.proc.submit_line("D");
    h.proc.submit_line("50");
    h.proc.cancel(); // ESC mid-flow
    REQUIRE_FALSE(h.proc.has_active_command());
    REQUIRE(h.cmds.empty());
}

TEST_CASE("Processor: 3-point ARC") {
    Harness h;
    h.proc.submit_line("A");
    h.proc.submit_line("0,0");
    h.proc.submit_line("1,1");
    h.proc.submit_line("2,0");
    REQUIRE(h.cmds.size() == 1);
    const auto* a = std::get_if<musacad::core::AddArcCommand>(&h.cmds[0]);
    REQUIRE(a != nullptr);
    REQUIRE(a->center.x == Approx(1.0));
    REQUIRE(a->center.y == Approx(0.0).margin(1e-9));
    REQUIRE(a->radius == Approx(1.0));
}

TEST_CASE("Processor: PLINE with Close") {
    Harness h;
    h.proc.submit_line("PL");
    h.proc.submit_line("0,0");
    h.proc.submit_line("10,0");
    h.proc.submit_line("10,10");
    h.proc.submit_line("C");
    REQUIRE(h.cmds.size() == 1);
    const auto* pl = std::get_if<musacad::core::AddPolylineCommand>(&h.cmds[0]);
    REQUIRE(pl != nullptr);
    REQUIRE(pl->closed);
    REQUIRE(pl->points.size() == 3);
}

TEST_CASE("Processor: each LINE invocation gets its own undo group") {
    Harness h;
    h.proc.submit_line("L");
    h.proc.submit_line("0,0");
    h.proc.submit_line("1,0");
    h.proc.submit_line("2,0"); // two segments, same group
    h.proc.submit_line("");    // end LINE
    REQUIRE(h.cmds.size() == 2);
    // Copy group ids (pushing more commands may reallocate h.cmds).
    const std::uint64_t g1 = std::get<musacad::core::AddLineCommand>(h.cmds[0]).group;
    const std::uint64_t g2 = std::get<musacad::core::AddLineCommand>(h.cmds[1]).group;
    REQUIRE(g1 == g2); // grouped together

    h.proc.submit_line("L"); // a new invocation -> new group
    h.proc.submit_line("0,0");
    h.proc.submit_line("0,1");
    h.proc.submit_line("");
    const std::uint64_t g3 = std::get<musacad::core::AddLineCommand>(h.cmds.back()).group;
    REQUIRE(g3 != g1);
}

TEST_CASE("Processor: ENTER repeats the last command") {
    Harness h;
    h.proc.submit_line("C");
    h.proc.submit_line("0,0");
    h.proc.submit_line("1");
    REQUIRE_FALSE(h.proc.has_active_command());
    h.proc.submit_line(""); // ENTER on empty -> repeat CIRCLE
    REQUIRE(h.proc.has_active_command());
    REQUIRE(h.out.prompt.find("center") != std::string::npos);
}

TEST_CASE("Processor: ERASE and UNDO emit messages") {
    Harness h;
    h.proc.submit_line("ERASE");
    h.proc.submit_line("L");
    REQUIRE(std::holds_alternative<musacad::core::EraseCommand>(h.cmds.back()));
    REQUIRE(std::get<musacad::core::EraseCommand>(h.cmds.back()).scope ==
            musacad::core::EraseScope::Last);

    h.proc.submit_line("U");
    REQUIRE(std::holds_alternative<musacad::core::UndoLastGroupCommand>(h.cmds.back()));
}

TEST_CASE("Processor: ZOOM drives the view, not geometry") {
    Harness h;
    h.proc.submit_line("ZOOM");
    h.proc.submit_line("E");
    REQUIRE(h.view.extents == 1);
    REQUIRE(h.cmds.empty()); // no geometry message

    h.proc.submit_line("Z");
    h.proc.submit_line("2");
    REQUIRE(h.view.scale == Approx(2.0));
    REQUIRE(h.cmds.empty());
}

TEST_CASE("Processor: unknown command reports an error") {
    Harness h;
    h.proc.submit_line("FLOOP");
    REQUIRE(h.out.any_contains("Unknown command"));
    REQUIRE_FALSE(h.proc.has_active_command());
}

TEST_CASE("Registry is data-driven and trivially extensible") {
    Harness h;
    REQUIRE(h.proc.registry().contains("LINE"));
    REQUIRE(h.proc.registry().contains("l"));
    REQUIRE_FALSE(h.proc.registry().contains("WOBBLE"));
    // Adding a command is a single row, no parser changes:
    h.proc.registry().register_command({"WOBBLE"},
                                       [] { return std::make_unique<LineCommand>(); });
    REQUIRE(h.proc.registry().contains("WOBBLE"));
}

// --- Phase 15: object-aware dimensioning + the smart DIM command -------------

using musacad::core::AddObjectDimensionCommand;
using musacad::core::DimType;
using musacad::core::EntityKind;

namespace {
const AddObjectDimensionCommand* last_object_dim(const std::vector<musacad::core::Command>& cmds) {
    const AddObjectDimensionCommand* found = nullptr;
    for (const auto& c : cmds) {
        if (const auto* od = std::get_if<AddObjectDimensionCommand>(&c)) {
            found = od;
        }
    }
    return found;
}
} // namespace

TEST_CASE("DIMRADIUS selects the object then places (object-aware message)") {
    Harness h;
    h.proc.submit_line("DRA");
    h.proc.submit_line("10,0");  // select circle/arc
    h.proc.submit_line("20,0");  // dimension line location
    const auto* od = last_object_dim(h.cmds);
    REQUIRE(od != nullptr);
    REQUIRE(od->type == static_cast<std::uint8_t>(DimType::Radius));
    REQUIRE(od->pick1.x == Approx(10.0));
    REQUIRE(od->pick2.x == Approx(20.0));
}

TEST_CASE("DIMANGULAR selects two lines then places the arc (object picks)") {
    Harness h;
    h.proc.submit_line("DAN");
    h.proc.submit_line("5,0"); // first line
    h.proc.submit_line("0,5"); // second line -> resolve + preview, not committed yet
    REQUIRE(last_object_dim(h.cmds) == nullptr);
    REQUIRE(h.proc.preview().kind == PreviewKind::Dimension);
    h.proc.submit_line("3,3"); // place -> commit
    const auto* od = last_object_dim(h.cmds);
    REQUIRE(od != nullptr);
    REQUIRE(od->type == static_cast<std::uint8_t>(DimType::Angular));
}

TEST_CASE("DIMLINEAR [Object] mode dimensions a selected segment") {
    Harness h;
    h.proc.submit_line("DLI");
    h.proc.submit_line("O");     // switch to object mode
    h.proc.submit_line("5,0");   // select the line/segment
    h.proc.submit_line("5,5");   // dimension line location
    const auto* od = last_object_dim(h.cmds);
    REQUIRE(od != nullptr);
    REQUIRE(od->type == static_cast<std::uint8_t>(DimType::Linear));
    // Two-point mode still works (no object message, a plain AddDimensionCommand).
    Harness h2;
    h2.proc.submit_line("DLI");
    h2.proc.submit_line("0,0");
    h2.proc.submit_line("10,0");
    h2.proc.submit_line("5,3");
    REQUIRE(last_object_dim(h2.cmds) == nullptr);
    bool plain = false;
    for (const auto& c : h2.cmds) {
        plain = plain || std::holds_alternative<musacad::core::AddDimensionCommand>(c);
    }
    REQUIRE(plain);
}

TEST_CASE("Smart DIM dispatches by hovered entity kind") {
    // Copy the submitted type out before the Harness (and its vector) is destroyed.
    auto run = [](EntityKind k) -> std::optional<std::uint8_t> {
        Harness h;
        h.proc.submit_line("DIM");
        h.proc.set_hovered_kind(k);     // cursor over this kind
        h.proc.submit_line("10,10");    // pick the object
        h.proc.submit_line("20,20");    // placement
        const auto* od = last_object_dim(h.cmds);
        return od != nullptr ? std::optional<std::uint8_t>{od->type} : std::nullopt;
    };
    REQUIRE(run(EntityKind::Circle) == static_cast<std::uint8_t>(DimType::Diameter));
    REQUIRE(run(EntityKind::Arc) == static_cast<std::uint8_t>(DimType::Radius));
    REQUIRE(run(EntityKind::Line) == static_cast<std::uint8_t>(DimType::Linear));
}

TEST_CASE("Smart DIM previews the type in the prompt as the cursor hovers") {
    Harness h;
    h.proc.submit_line("DIM");
    h.proc.set_hovered_kind(EntityKind::Circle);
    REQUIRE(h.out.prompt.find("Diameter") != std::string::npos);
    h.proc.set_hovered_kind(EntityKind::Arc);
    REQUIRE(h.out.prompt.find("Radius") != std::string::npos);
    h.proc.set_hovered_kind(std::nullopt); // over empty space
    REQUIRE(h.out.prompt.find("Diameter") == std::string::npos);
    REQUIRE(h.out.prompt.find("Radius") == std::string::npos);
}

TEST_CASE("Smart DIM refuses to dimension empty space") {
    Harness h;
    h.proc.submit_line("DIM");
    h.proc.set_hovered_kind(std::nullopt); // nothing under the cursor
    h.proc.submit_line("10,10");
    REQUIRE(last_object_dim(h.cmds) == nullptr); // nothing submitted
    REQUIRE(h.proc.has_active_command());        // still waiting
}

// --- Phase 16 Part C: dimension placement preview (cursor-follow rubber-band) ---

using musacad::core::ResolveDimObjectCommand;

namespace {
bool has_resolve(const std::vector<musacad::core::Command>& cmds) {
    for (const auto& c : cmds) {
        if (std::holds_alternative<ResolveDimObjectCommand>(c)) {
            return true;
        }
    }
    return false;
}
} // namespace

TEST_CASE("DIMLINEAR placement step requests a preview and emits nothing until the click") {
    Harness h;
    h.proc.submit_line("DLI");
    h.proc.submit_line("0,0");
    h.proc.submit_line("10,0"); // now in placement: a full-dimension preview is active
    REQUIRE(h.proc.preview().kind == PreviewKind::Dimension);
    REQUIRE(h.proc.preview().dim_type == static_cast<int>(DimType::Linear));
    // The drag has produced NO geometry command yet (zero store mutation invariant).
    for (const auto& c : h.cmds) {
        REQUIRE_FALSE(std::holds_alternative<musacad::core::AddDimensionCommand>(c));
    }
    h.proc.submit_line("5,4"); // commit placement
    bool committed = false;
    for (const auto& c : h.cmds) {
        committed = committed || std::holds_alternative<musacad::core::AddDimensionCommand>(c);
    }
    REQUIRE(committed);
}

TEST_CASE("DIMRADIUS resolves def points for the preview, commits only on placement") {
    Harness h;
    h.proc.submit_line("DRA");
    h.proc.submit_line("10,0"); // select the circle -> resolve-for-preview, no create
    REQUIRE(h.proc.preview().kind == PreviewKind::Dimension);
    REQUIRE(has_resolve(h.cmds));               // preview resolve was issued
    REQUIRE(last_object_dim(h.cmds) == nullptr); // but nothing created yet
    h.proc.submit_line("20,0");                  // placement click commits
    REQUIRE(last_object_dim(h.cmds) != nullptr);
}

TEST_CASE("Processor: MTEXT two corners + text emits AddMTextCommand") {
    Harness h;
    h.proc.submit_line("MT");
    h.proc.submit_line("0,0");
    h.proc.submit_line("40,-20");
    h.proc.submit_line("ALPHA BETA GAMMA");
    REQUIRE(h.cmds.size() == 1);
    const auto* m = std::get_if<musacad::core::AddMTextCommand>(&h.cmds[0]);
    REQUIRE(m != nullptr);
    REQUIRE(m->content == "ALPHA BETA GAMMA");
    REQUIRE(m->block.width == Approx(40.0));
}

TEST_CASE("Processor: QLEADER arrow + vertex + text emits AddMLeaderCommand") {
    Harness h;
    h.proc.submit_line("LE");
    h.proc.submit_line("0,0");
    h.proc.submit_line("10,8");
    h.proc.submit_line(""); // finish chain
    h.proc.submit_line("SEE NOTE");
    REQUIRE(h.cmds.size() == 1);
    const auto* m = std::get_if<musacad::core::AddMLeaderCommand>(&h.cmds[0]);
    REQUIRE(m != nullptr);
    REQUIRE(m->vertices.size() == 2);
    REQUIRE(m->content == "SEE NOTE");
}

TEST_CASE("Processor: TEXTEDIT picks then sets content via EditTextContentCommand") {
    Harness h;
    h.proc.submit_line("ED");
    h.proc.submit_line("5,5");          // pick a point on the text
    h.proc.submit_line("NEW CONTENT");  // the new string
    REQUIRE(h.cmds.size() == 1);
    const auto* e = std::get_if<musacad::core::EditTextContentCommand>(&h.cmds[0]);
    REQUIRE(e != nullptr);
    REQUIRE(e->content == "NEW CONTENT");
    REQUIRE(e->at == Vec2{5.0, 5.0});
}

TEST_CASE("Processor: LINE then polar @50<0 lands exactly at (50,0) [DYN composition]") {
    Harness h;
    h.proc.submit_line("L");
    h.proc.submit_line("0,0");
    h.proc.submit_line("@50<0"); // exactly what DYN composes from length=50, angle=0
    h.proc.cancel();
    REQUIRE(h.cmds.size() == 1);
    const auto* line = std::get_if<musacad::core::AddLineCommand>(&h.cmds[0]);
    REQUIRE(line != nullptr);
    REQUIRE(line->a == Vec2{0.0, 0.0});
    REQUIRE(line->b.x == Approx(50.0));
    REQUIRE(line->b.y == Approx(0.0).margin(1e-9));
}

TEST_CASE("Processor: CIRCLE [Diameter] option -> radius = diameter/2") {
    Harness h;
    h.proc.submit_line("C");
    h.proc.submit_line("0,0");
    h.proc.submit_line("D");   // switch to diameter
    h.proc.submit_line("50");  // diameter 50
    REQUIRE(h.cmds.size() == 1);
    const auto* c = std::get_if<musacad::core::AddCircleCommand>(&h.cmds[0]);
    REQUIRE(c != nullptr);
    REQUIRE(c->center == Vec2{0.0, 0.0});
    REQUIRE(c->radius == Approx(25.0));
}
