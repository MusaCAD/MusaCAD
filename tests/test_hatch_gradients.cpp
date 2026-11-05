// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// HATCH (#41): the nine gradient shapes -- their names, how they draw and their DXF form --
// and HATCHEDIT's Style, Draw order, Associative and Transparency.

#include <chrono>
#include <cmath>
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
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/hatch.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/native_kernel_2d.hpp"
#include "musacad/core/render_snapshot.hpp"
#include "musacad/core/scene_snapshot.hpp"

using namespace musacad::core;
using musacad::command::CommandOutput;
using musacad::command::CommandProcessor;
using Catch::Approx;

namespace {
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

/// The colour the snapshot fills `p` with (the first fill triangle holding it).
std::optional<Rgb> fill_at(const RenderSnapshot& snap, Vec2 p) {
    const auto inside = [](Vec2 a, Vec2 b, Vec2 c, Vec2 q) {
        const auto side = [](Vec2 u, Vec2 v, Vec2 w) { return (v.x - u.x) * (w.y - u.y) - (v.y - u.y) * (w.x - u.x); };
        const double d1 = side(a, b, q);
        const double d2 = side(b, c, q);
        const double d3 = side(c, a, q);
        return !((d1 < 0 || d2 < 0 || d3 < 0) && (d1 > 0 || d2 > 0 || d3 > 0));
    };
    for (const ColorBatch& b : snap.fill_batches) {
        for (std::uint32_t i = b.first; i + 2 < b.first + b.count; i += 3) {
            const Vec2 a = snap.fill_vertices[i];
            const Vec2 bb = snap.fill_vertices[i + 1];
            const Vec2 c = snap.fill_vertices[i + 2];
            if (std::abs((bb.x - a.x) * (c.y - a.y) - (bb.y - a.y) * (c.x - a.x)) < 1e-9) {
                continue; // a sliver the clipping left: it holds no point
            }
            if (inside(a, bb, c, p)) {
                return b.color;
            }
        }
    }
    return std::nullopt;
}
} // namespace

TEST_CASE("#41 the nine gradients: their names, and where each is lightest") {
    using hatch::GradientShape;
    REQUIRE(hatch::gradient_shape("GRADIENT") == GradientShape::Linear); // drawings from before
    REQUIRE(hatch::gradient_shape("GR_LINEAR") == GradientShape::Linear);
    REQUIRE(hatch::gradient_shape("gr_spher") == GradientShape::Spherical);
    REQUIRE(hatch::gradient_shape("GR_INVCUR") == GradientShape::InvCurved);
    REQUIRE_FALSE(hatch::gradient_shape("ANSI31"));
    REQUIRE_FALSE(hatch::is_gradient("SOLID"));
    for (const GradientShape g : hatch::kGradientShapes) {
        REQUIRE(hatch::gradient_shape(hatch::gradient_pattern_name(g)) == g);
        REQUIRE(hatch::gradient_from_dxf(hatch::gradient_dxf_name(g)) == g);
    }
    // 0 is the first colour, 1 the second.
    REQUIRE(hatch::gradient_value(GradientShape::Linear, 0.0, 0.5) == Approx(0.0));
    REQUIRE(hatch::gradient_value(GradientShape::Linear, 1.0, 0.5) == Approx(1.0));
    REQUIRE(hatch::gradient_value(GradientShape::Cylinder, 0.5, 0.2) == Approx(1.0)); // down the middle
    REQUIRE(hatch::gradient_value(GradientShape::Cylinder, 0.0, 0.2) == Approx(0.0));
    REQUIRE(hatch::gradient_value(GradientShape::InvCylinder, 0.5, 0.2) == Approx(0.0));
    REQUIRE(hatch::gradient_value(GradientShape::Spherical, 0.5, 0.5) == Approx(1.0)); // the centre
    REQUIRE(hatch::gradient_value(GradientShape::Spherical, 0.0, 0.0) == Approx(0.0));
    REQUIRE(hatch::gradient_value(GradientShape::InvSpherical, 0.5, 0.5) == Approx(0.0));
    REQUIRE(hatch::gradient_value(GradientShape::Hemispherical, 0.5, 0.0) == Approx(1.0)); // lower edge
    REQUIRE(hatch::gradient_value(GradientShape::Hemispherical, 0.5, 1.0) < 0.2);
    REQUIRE(hatch::gradient_value(GradientShape::Curved, 0.5, 0.5) > 0.5); // rises quickly
    REQUIRE(hatch::gradient_value(GradientShape::InvCurved, 0.5, 0.5) < 0.5);
}

TEST_CASE("#41 a spherical gradient draws lightest at the centre; DXF carries the shape") {
    GeometryStore store;
    EntityProps props;
    props.set_color_by_layer(false);
    props.color = Rgb{0, 0, 0};
    store.add_hatch({{{0, 0}, {100, 0}, {100, 100}, {0, 100}}}, "GR_SPHER", 1.0, 0.0, {}, props, Rgb{255, 255, 255});
    NativeKernel2D kernel;
    RenderSnapshot snap;
    build_render_snapshot(store, kernel, snap);
    const std::optional<Rgb> centre = fill_at(snap, {50.5, 50.5});
    const std::optional<Rgb> corner = fill_at(snap, {1, 1});
    const std::optional<Rgb> edge = fill_at(snap, {50.5, 1});
    REQUIRE(centre);
    REQUIRE(corner);
    REQUIRE(edge);
    REQUIRE(centre->r > 230);              // the second colour in the middle
    REQUIRE(corner->r < 25);               // the first at the corners
    REQUIRE(edge->r > corner->r);          // and between at the edges
    REQUIRE(edge->r < centre->r);
    std::size_t computed = 0;
    for (const ColorBatch& b : snap.fill_batches) {
        computed += b.computed_color ? 1 : 0;
    }
    REQUIRE(computed > 10); // many steps of colour, not one

    // DXF: the shape is AutoCAD's 470 name, and comes back.
    io::Document doc;
    for (const hatch::GradientShape g : hatch::kGradientShapes) {
        io::DocHatch h;
        h.loops = {{{0, 0}, {10, 0}, {10, 10}}};
        h.pattern_name = std::string(hatch::gradient_pattern_name(g));
        h.color2 = Rgb{1, 2, 3};
        doc.hatches.push_back(h);
    }
    const std::string dxf = io::serialize_dxf(doc);
    REQUIRE(dxf.find("\nSPHERICAL\n") != std::string::npos);
    REQUIRE(dxf.find("\nINVHEMISPHERICAL\n") != std::string::npos);
    io::Document back;
    REQUIRE(io::parse_dxf(dxf, back).ok);
    REQUIRE(back.hatches.size() == doc.hatches.size());
    for (std::size_t i = 0; i < back.hatches.size(); ++i) {
        // A linear one keeps the name drawings have always had.
        const std::string want = i == 0 ? std::string("GRADIENT") : doc.hatches[i].pattern_name;
        REQUIRE(back.hatches[i].pattern_name == want);
    }
}

TEST_CASE("#41 HATCH's Gradient asks for the shape; a colour typed there keeps the last one") {
    H h;
    h.run({"HATCH", "G"});
    REQUIRE(h.out.prompt == "Enter gradient pattern name or [?] <GR_LINEAR>: ");
    h.run({"?"});
    REQUIRE(h.out.lines.back().find("GR_INVHEM") != std::string::npos);
    h.run({"SPHER", "0,0,255", "0", "5,5"});
    const auto* g = h.last<HatchPickPointCommand>();
    REQUIRE(g != nullptr);
    REQUIRE(g->pattern_name == "GR_SPHER");
    h.run({""});
    // The pattern prompt takes a gradient by its name too.
    h.run({"-HATCH", "P", "GR_CYLIN", "", "", "5,5"});
    REQUIRE(h.last<HatchPickPointCommand>()->pattern_name == "GR_CYLIN");
    h.run({""});
    // A colour at the shape prompt: the shape kept.
    h.run({"HATCH", "G", "255,0,0", "0", "5,5"});
    REQUIRE(h.last<HatchPickPointCommand>()->pattern_name == "GR_CYLIN");
    REQUIRE(h.last<HatchPickPointCommand>()->color2 == Rgb{255, 0, 0});
    h.run({""});
    // Back to linear: the shape is kept from one HATCH to the next, as the colours are.
    h.run({"HATCH", "G", "LINEAR", "255,255,255", "0", ""});
}
