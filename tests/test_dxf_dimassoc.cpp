// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// Dimension association in DXF (#59): AutoCAD's DIMASSOC objects. Writing names, for each
// associative dimension, the objects its points are on, as AutoCAD names them; reading takes
// a dimension a DIMASSOC names as associative and, in a file that has $DIMASSOC, the others
// as not.

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/command.hpp"
#include "musacad/core/geometry_engine.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"

using namespace musacad::core;
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
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

/// The file's (code, value) pairs.
std::vector<std::pair<int, std::string>> pairs_of(const std::string& path) {
    std::ifstream in(path);
    std::vector<std::pair<int, std::string>> out;
    std::string c;
    std::string v;
    while (std::getline(in, c) && std::getline(in, v)) {
        if (!v.empty() && v.back() == '\r') {
            v.pop_back();
        }
        out.emplace_back(std::stoi(c), v);
    }
    return out;
}

/// The groups of the record with handle `h` (from its 0 to the next 0).
std::vector<std::pair<int, std::string>> record(const std::vector<std::pair<int, std::string>>& ps,
                                                const std::string& h) {
    for (std::size_t i = 0; i + 1 < ps.size(); ++i) {
        if (ps[i].first == 0 && ps[i + 1].first == 5 && ps[i + 1].second == h) {
            std::vector<std::pair<int, std::string>> out{ps[i]};
            for (std::size_t j = i + 1; j < ps.size() && ps[j].first != 0; ++j) {
                out.push_back(ps[j]);
            }
            return out;
        }
    }
    return {};
}

std::vector<std::string> values(const std::vector<std::pair<int, std::string>>& rec, int code) {
    std::vector<std::string> out;
    for (const auto& [c, v] : rec) {
        if (c == code) {
            out.push_back(v);
        }
    }
    return out;
}

/// Saves the engine's drawing as DXF and waits until the file is there to read.
void save_dxf_from(GeometryEngine& engine, const std::filesystem::path& p) {
    std::filesystem::remove(p);
    engine.consume_snapshot();
    const std::uint64_t before = engine.snapshot().document_version;
    engine.submit(SaveDocumentCommand{p.string(), true});
    REQUIRE(wait_until(engine, [&](const auto& s) { return s.document_version > before && !s.dirty; }));
    REQUIRE(std::filesystem::exists(p));
}

/// A minimal AutoCAD-like DXF: two lines and two linear dimensions on their ends, the first
/// of them named by a DIMASSOC object. `header` adds $DIMASSOC.
std::string autocad_like(bool header) {
    std::string s;
    if (header) {
        s += "0\nSECTION\n2\nHEADER\n9\n$DIMASSOC\n280\n2\n0\nENDSEC\n";
    }
    s += "0\nSECTION\n2\nENTITIES\n"
         "0\nLINE\n5\nA1\n8\n0\n10\n0\n20\n0\n11\n30\n21\n0\n"
         "0\nLINE\n5\nA2\n8\n0\n10\n0\n20\n50\n11\n30\n21\n50\n"
         "0\nDIMENSION\n5\nD1\n8\n0\n2\n*D1\n10\n15\n20\n-10\n70\n0\n13\n0\n23\n0\n14\n30\n24\n0\n"
         "0\nDIMENSION\n5\nD2\n8\n0\n2\n*D2\n10\n15\n20\n40\n70\n0\n13\n0\n23\n50\n14\n30\n24\n50\n"
         "0\nENDSEC\n"
         "0\nSECTION\n2\nOBJECTS\n"
         "0\nDIMASSOC\n5\nE1\n330\nE0\n100\nAcDbDimAssoc\n330\nD1\n90\n3\n70\n0\n71\n0\n"
         "1\nAcDbOsnapPointRef\n72\n13\n331\nA1\n73\n2\n91\n0\n40\n0.0\n75\n0\n"
         "1\nAcDbOsnapPointRef\n72\n1\n331\nA1\n73\n2\n91\n0\n40\n1.0\n75\n0\n"
         "0\nENDSEC\n0\nEOF\n";
    return s;
}
} // namespace

TEST_CASE("#59 an AutoCAD drawing: a dimension a DIMASSOC names is associative, the others not") {
    io::Document doc;
    REQUIRE(io::parse_dxf(autocad_like(true), doc).ok);
    REQUIRE(doc.dims.size() == 2);
    REQUIRE_FALSE(doc.dims[0].unassoc);
    REQUIRE(doc.dims[1].unassoc);

    // A file without $DIMASSOC (as Musa CAD wrote before) leaves them as they were: following
    // what their points are on.
    io::Document old;
    REQUIRE(io::parse_dxf(autocad_like(false), old).ok);
    REQUIRE(old.dims.size() == 2);
    REQUIRE_FALSE(old.dims[0].unassoc);
    REQUIRE_FALSE(old.dims[1].unassoc);
}

TEST_CASE("#59 DXF names the objects an associative dimension's points are on, as AutoCAD does") {
    const std::filesystem::path p = std::filesystem::temp_directory_path() / "musacad_dimassoc_out.dxf";
    {
        GeometryEngine engine;
        engine.start();
        engine.submit(AddLineCommand{{0, 0}, {30, 0}, 1});
        engine.submit(AddCircleCommand{{100, 0}, 10, 2});
        engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Aligned), {0, 0}, {30, 0}, {15, -5}, 0, 3});
        engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Radius), {100, 0}, {110, 0}, {112, 0}, 0, 4});
        // On nothing: no DIMASSOC.
        engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Linear), {200, 0}, {220, 0}, {210, 5}, 0, 5});
        save_dxf_from(engine, p);
        engine.stop();
    }
    const auto ps = pairs_of(p.string());
    // The header says the file names its associative dimensions; the class is declared.
    const auto var = std::find(ps.begin(), ps.end(), std::pair<int, std::string>{9, "$DIMASSOC"});
    REQUIRE(var != ps.end());
    REQUIRE(std::next(var)->second == "2");
    REQUIRE(std::find(ps.begin(), ps.end(), std::pair<int, std::string>{2, "AcDbDimAssoc"}) != ps.end());

    // Two DIMASSOC objects: the aligned dimension's and the radius's.
    std::vector<std::string> assocs;
    std::string line_handle;
    std::string circle_handle;
    for (std::size_t i = 0; i + 1 < ps.size(); ++i) {
        if (ps[i].first == 0 && ps[i + 1].first == 5) {
            if (ps[i].second == "DIMASSOC") {
                assocs.push_back(ps[i + 1].second);
            } else if (ps[i].second == "LINE") {
                line_handle = ps[i + 1].second;
            } else if (ps[i].second == "CIRCLE") {
                circle_handle = ps[i + 1].second;
            }
        }
    }
    REQUIRE(assocs.size() == 2);
    // The line carries the DIMASSOC as a reactor, as AutoCAD's objects do.
    const auto line = record(ps, line_handle);
    REQUIRE(std::find(line.begin(), line.end(), std::pair<int, std::string>{102, "{ACAD_REACTORS"}) != line.end());

    bool saw_aligned = false;
    bool saw_radius = false;
    for (const std::string& h : assocs) {
        const auto a = record(ps, h);
        const std::vector<std::string> objects = values(a, 331);
        const std::vector<std::string> snaps = values(a, 72);
        const std::string flags = values(a, 90).at(0);
        // The dimension named (330 after the subclass marker) has this DIMASSOC's dictionary
        // as its extension dictionary.
        const std::string dim = values(a, 330).back();
        const auto d = record(ps, dim);
        REQUIRE(d.at(0).second == "DIMENSION");
        const std::string dict = values(d, 360).at(0);
        REQUIRE(values(record(ps, dict), 360).at(0) == h);
        if (objects.size() == 2) {
            // The aligned one: both its extension-line points on the line, the first at its
            // start (13, parameter 0), the second at its end (1, parameter 1).
            saw_aligned = true;
            REQUIRE(flags == "3");
            REQUIRE(objects == std::vector<std::string>{line_handle, line_handle});
            REQUIRE(snaps == std::vector<std::string>{"13", "1"});
            REQUIRE(std::stod(values(a, 40).at(0)) == Approx(0.0));
            REQUIRE(std::stod(values(a, 40).at(1)) == Approx(1.0));
        } else {
            // The radius: its centre, by the circle's centre snap, as AutoCAD's point 1.
            saw_radius = true;
            REQUIRE(flags == "2");
            REQUIRE(objects == std::vector<std::string>{circle_handle});
            REQUIRE(snaps == std::vector<std::string>{"3"});
        }
    }
    REQUIRE(saw_aligned);
    REQUIRE(saw_radius);

    // Read back: the two are associative, the one on nothing is not.
    io::Document doc;
    REQUIRE(io::load_dxf(p.string(), doc).ok);
    REQUIRE(doc.dims.size() == 3);
    int assoc = 0;
    for (const io::DocDim& d : doc.dims) {
        if (static_cast<DimType>(d.type) == DimType::Linear) {
            REQUIRE(d.unassoc);
        } else {
            REQUIRE_FALSE(d.unassoc);
            ++assoc;
        }
    }
    REQUIRE(assoc == 2);

    // Opened again, the aligned dimension still follows its line.
    GeometryEngine engine;
    engine.start();
    engine.submit(OpenDocumentCommand{p.string(), true});
    REQUIRE(wait_until(engine, [](const auto& s) { return s.document_version > 0; }));
    engine.submit(SelectPickCommand{{15, 0}, 0.5, false});
    engine.submit(MoveSelectionCommand{{0, 10}, 7});
    engine.submit(SelectAllCommand{});
    REQUIRE(wait_until(engine, [&](const auto& s) {
        return std::any_of(s.selected_dims.begin(), s.selected_dims.end(), [](const SelectedDim& d) {
            return d.type == DimType::Aligned && d.points.size() == 2 && d.points[1] == Vec2{30, 10};
        });
    }));
    engine.stop();
    std::filesystem::remove(p);
}

TEST_CASE("#59 a polyline's vertex is named by its segment, and an arc's centre by the centre snap") {
    const std::filesystem::path p = std::filesystem::temp_directory_path() / "musacad_dimassoc_pl.dxf";
    {
        GeometryEngine engine;
        engine.start();
        AddPolylineCommand rect;
        rect.points = {{0, 0}, {40, 0}, {40, 20}, {0, 20}};
        rect.closed = true;
        rect.group = 1;
        engine.submit(rect);
        AddPolylineCommand open;
        open.points = {{0, 50}, {40, 50}, {40, 70}};
        open.group = 2;
        engine.submit(open);
        // Up the closed one's left side (vertex 3 to vertex 0) and along the open one's first side.
        engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Linear), {0, 20}, {0, 0}, {-10, 10}, 0, 3});
        engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Linear), {0, 50}, {40, 50}, {20, 45}, 0, 4});
        engine.submit(AddArcCommand{{100, 0}, 10, 0.0, 1.5, 5});
        engine.submit(AddDimensionCommand{static_cast<std::uint8_t>(DimType::Radius), {100, 0}, {110, 0}, {112, 0}, 0, 6});
        save_dxf_from(engine, p);
        engine.stop();
    }
    const auto ps = pairs_of(p.string());
    std::vector<std::vector<std::pair<int, std::string>>> assocs;
    for (std::size_t i = 0; i + 1 < ps.size(); ++i) {
        if (ps[i].first == 0 && ps[i].second == "DIMASSOC" && ps[i + 1].first == 5) {
            assocs.push_back(record(ps, ps[i + 1].second));
        }
    }
    REQUIRE(assocs.size() == 3);
    bool closed_seen = false;
    bool open_seen = false;
    bool arc_seen = false;
    for (const auto& a : assocs) {
        const std::vector<std::string> snaps = values(a, 72);
        const std::vector<std::string> segs = values(a, 91);
        if (snaps.size() == 1) {
            arc_seen = true;
            REQUIRE(snaps[0] == "3");
            continue;
        }
        REQUIRE(snaps.size() == 2);
        if (segs == std::vector<std::string>{"3", "4"}) {
            // (0,20) ends segment 3; (0,0), the closed polyline's first vertex, ends the
            // closing segment 4.
            closed_seen = true;
            REQUIRE(snaps == std::vector<std::string>{"1", "1"});
        } else {
            // (0,50) starts an open polyline's segment 1; (40,50) ends it.
            open_seen = true;
            REQUIRE(segs == std::vector<std::string>{"1", "1"});
            REQUIRE(snaps == std::vector<std::string>{"13", "1"});
        }
    }
    REQUIRE(closed_seen);
    REQUIRE(open_seen);
    REQUIRE(arc_seen);
    std::filesystem::remove(p);
}
