// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// A DXF Musa CAD writes is an AutoCAD 2000 file in its structure too (#95): each table
// record names its table and carries its subclass markers, each entity its owner (the
// block record of its space or block) and AcDbEntity then its own subclass, every block has
// a BLOCK_RECORD, the two spaces have their blocks, and handles are unique and below
// $HANDSEED. Without these the ODA File Converter (and so Export DWG) rejected every file.

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "musacad/core/io/document.hpp"
#include "musacad/core/io/dxf.hpp"

using namespace musacad::core;
using namespace musacad::core::io;

namespace {
struct Rec {
    std::string type;
    std::string section;
    std::vector<std::pair<int, std::string>> groups;
    [[nodiscard]] std::vector<std::string> all(int code) const {
        std::vector<std::string> out;
        for (const auto& [c, v] : groups) {
            if (c == code) {
                out.push_back(v);
            }
        }
        return out;
    }
    [[nodiscard]] std::string first(int code) const {
        const std::vector<std::string> v = all(code);
        return v.empty() ? std::string() : v.front();
    }
    /// The position of the first group `code` with `value`, or npos.
    [[nodiscard]] std::size_t at(int code, const std::string& value) const {
        for (std::size_t i = 0; i < groups.size(); ++i) {
            if (groups[i].first == code && groups[i].second == value) {
                return i;
            }
        }
        return std::string::npos;
    }
};

std::vector<Rec> records_of(const std::string& text) {
    std::istringstream in(text);
    std::vector<Rec> out;
    std::string section;
    std::string c;
    std::string v;
    while (std::getline(in, c) && std::getline(in, v)) {
        const int code = std::stoi(c);
        if (code == 0) {
            if (v == "SECTION" || v == "ENDSEC" || v == "EOF") {
                section = v == "SECTION" ? "?" : "";
                out.push_back({v, section, {}});
                continue;
            }
            out.push_back({v, section, {}});
            continue;
        }
        if (!out.empty()) {
            if (out.back().type == "SECTION" && code == 2) {
                section = v;
            }
            out.back().groups.emplace_back(code, v);
        }
    }
    return out;
}

Document every_kind() {
    Document d;
    d.layers = {Layer{"0", {255, 255, 255}, Linetype::Continuous, 25, true, false, false}};
    DimStyle standard;
    standard.name = "Standard";
    d.dimstyles = {standard};
    d.lines.push_back({{0, 0}, {30, 0}});
    d.circles.push_back({{50, 0}, 5});
    d.arcs.push_back({{70, 0}, 5, 0.0, 1.5});
    DocPolyline pl;
    pl.points = {{0, 10}, {10, 10}, {10, 20}};
    d.polylines.push_back(pl);
    DocPolyline fitted = pl; // a heavy POLYLINE with VERTEX records
    fitted.fit = 1;
    fitted.frame = pl.points;
    d.polylines.push_back(fitted);
    d.points.push_back({{1, 1}});
    d.ellipses.push_back({{90, 0}, {10, 0}, 0.5});
    DocSpline sp;
    sp.control_points = {{0, 30}, {5, 35}, {10, 30}, {15, 35}};
    d.splines.push_back(sp);
    d.xlines.push_back({{0, 0}, {1, 0}, false});
    d.xlines.push_back({{0, 0}, {0, 1}, true});
    DocText t;
    t.pos = {0, 40};
    t.content = "TEXT";
    t.justify = 4; // middle: the second AcDbText carries 73
    t.align = {5, 41};
    d.texts.push_back(t);
    DocAttDef ad;
    ad.text = t;
    ad.text.content = "TAG";
    ad.def = "value";
    ad.prompt = "Value?";
    d.attdefs.push_back(ad);
    for (const DimType k : {DimType::Linear, DimType::Aligned, DimType::Radius, DimType::Diameter,
                            DimType::Angular, DimType::Ordinate, DimType::Jogged, DimType::ArcLength}) {
        DocDim dim;
        dim.type = static_cast<std::uint8_t>(k);
        dim.a = {0, 0};
        dim.b = {10, 0};
        dim.line_pt = {5, 5};
        dim.text_override = "<> TYP";
        d.dims.push_back(dim);
    }
    DocLeader ld;
    ld.tip = {0, 50};
    ld.knee = {10, 55};
    ld.content = "NOTE";
    d.leaders.push_back(ld);
    DocMText mt;
    mt.block.pos = {0, 60};
    mt.block.height = 2.5;
    mt.content = "MTEXT";
    d.mtexts.push_back(mt);
    DocHatch solid;
    solid.loops = {{{0, 70}, {10, 70}, {10, 80}}};
    d.hatches.push_back(solid);
    DocHatch wipeout = solid;
    wipeout.pattern_name = "WIPEOUT";
    d.hatches.push_back(wipeout);
    DocFcf f;
    f.cells = {"\xE2\x8C\x96", "0.1", "A"};
    d.fcfs.push_back(f);
    DocViewport vp;
    vp.props.set_space(1);
    d.viewports.push_back(vp);
    DocLine paper_line{{0, 0}, {100, 0}};
    paper_line.props.set_space(1);
    d.lines.push_back(paper_line);

    DocBlockDef b;
    b.name = "PART";
    b.lines.push_back({{0, 0}, {1, 1}});
    b.circles.push_back({{0, 0}, 1});
    b.arcs.push_back({{0, 0}, 1, 0.0, 1.0});
    b.polylines.push_back(pl);
    b.texts.push_back(t);
    b.mtexts.push_back(mt);
    b.hatches.push_back(solid);
    b.attdefs.push_back(ad);
    d.block_defs.push_back(b);
    DocBlockDef outer;
    outer.name = "ASSEMBLY";
    DocInsert nested;
    nested.block_name = "PART";
    outer.inserts.push_back(nested);
    d.block_defs.push_back(outer);
    DocInsert in;
    in.block_name = "PART";
    in.pos = {200, 0};
    in.attribs.push_back({"TAG", "42"});
    d.inserts.push_back(in);
    return d;
}

/// The subclass each entity type carries after AcDbEntity.
const std::map<std::string, std::string> kSubclass = {
    {"LINE", "AcDbLine"},          {"CIRCLE", "AcDbCircle"},          {"ARC", "AcDbCircle"},
    {"LWPOLYLINE", "AcDbPolyline"}, {"POLYLINE", "AcDb2dPolyline"},   {"VERTEX", "AcDbVertex"},
    {"POINT", "AcDbPoint"},        {"ELLIPSE", "AcDbEllipse"},        {"SPLINE", "AcDbSpline"},
    {"XLINE", "AcDbXline"},        {"RAY", "AcDbRay"},                {"TEXT", "AcDbText"},
    {"ATTDEF", "AcDbText"},        {"ATTRIB", "AcDbText"},            {"DIMENSION", "AcDbDimension"},
    {"LEADER", "AcDbLeader"},      {"MTEXT", "AcDbMText"},            {"HATCH", "AcDbHatch"},
    {"WIPEOUT", "AcDbWipeout"},    {"TOLERANCE", "AcDbFcf"},          {"INSERT", "AcDbBlockReference"},
    {"VIEWPORT", "AcDbViewport"},  {"BLOCK", "AcDbBlockBegin"},       {"ENDBLK", "AcDbBlockEnd"},
};
} // namespace

TEST_CASE("#95 a DXF from Musa CAD has AutoCAD 2000's structure: owners, subclass markers, records") {
    const std::vector<Rec> recs = records_of(serialize_dxf(every_kind()));

    // Handles: unique, and below $HANDSEED.
    std::set<std::string> handles;
    std::string seed;
    for (const Rec& r : recs) {
        if (r.type == "SECTION" && r.first(2) == "HEADER") {
            const std::size_t i = r.at(9, "$HANDSEED");
            REQUIRE(i != std::string::npos);
            seed = r.groups[i + 1].second;
        }
        if (r.type == "SECTION") {
            continue; // the header's $HANDSEED is a 5 too
        }
        for (const int code : {5, 105}) {
            for (const std::string& h : r.all(code)) {
                INFO(r.type << " " << h);
                REQUIRE(handles.insert(h).second);
            }
        }
    }
    const auto num = [](const std::string& h) { return std::stoull(h, nullptr, 16); };
    for (const std::string& h : handles) {
        REQUIRE(num(h) < num(seed));
    }

    // Tables: each owned by nothing, each record by its table, with both subclass markers.
    std::map<std::string, std::string> record_name; // BLOCK_RECORD handle -> name
    std::string table;
    std::set<std::string> tables;
    for (const Rec& r : recs) {
        if (r.type == "TABLE") {
            table = r.first(5);
            tables.insert(r.first(2));
            REQUIRE(r.first(330) == "0");
            REQUIRE(r.at(100, "AcDbSymbolTable") != std::string::npos);
        } else if (r.section == "TABLES" && r.type != "ENDTAB") {
            INFO(r.type << " " << r.first(2));
            REQUIRE(r.first(330) == table);
            REQUIRE(r.at(100, "AcDbSymbolTableRecord") != std::string::npos);
            REQUIRE(r.all(100).size() == 2);
            REQUIRE_FALSE(r.first(2).empty()); // the name, which an R2000 reader finds after the markers
            if (r.type == "BLOCK_RECORD") {
                record_name[r.first(5)] = r.first(2);
            }
        }
    }
    for (const char* t : {"VPORT", "LTYPE", "LAYER", "STYLE", "VIEW", "UCS", "APPID", "DIMSTYLE", "BLOCK_RECORD"}) {
        INFO(t);
        REQUIRE(tables.contains(t));
    }
    // A record for each space and each block.
    std::set<std::string> names;
    for (const auto& [h, n] : record_name) {
        names.insert(n);
    }
    REQUIRE(names == std::set<std::string>{"*Model_Space", "*Paper_Space", "PART", "ASSEMBLY"});

    // Entities (and BLOCK / ENDBLK): owned by a block record, then AcDbEntity, then their own
    // subclass. Paper space's are owned by *Paper_Space and say so (67); a block's, by its record.
    std::set<std::string> blocks;
    std::string block_record;
    std::string parent; // the POLYLINE or INSERT whose VERTEX / ATTRIB records follow
    std::set<std::string> seen;
    for (const Rec& r : recs) {
        if (r.section != "ENTITIES" && r.section != "BLOCKS") {
            continue;
        }
        if (r.type == "SECTION" || r.type == "ENDSEC" || r.type == "SEQEND") {
            continue;
        }
        INFO(r.section << " " << r.type << " " << r.first(5));
        const std::string owner = r.first(330);
        const bool sub = r.type == "VERTEX" || r.type == "ATTRIB";
        if (sub) {
            REQUIRE(owner == parent); // a vertex is its polyline's, an attribute its insert's
        } else {
            REQUIRE(record_name.contains(owner));
        }
        if (r.type == "POLYLINE" || r.type == "INSERT") {
            parent = r.first(5);
        }
        const std::size_t entity = r.at(100, "AcDbEntity");
        REQUIRE(entity != std::string::npos);
        REQUIRE(r.groups[entity - 1] == std::pair<int, std::string>{330, owner}); // owner, then AcDbEntity
        REQUIRE(r.at(8, r.first(8)) > entity);                                    // the layer is AcDbEntity's
        REQUIRE(kSubclass.contains(r.type));
        REQUIRE(r.at(100, kSubclass.at(r.type)) > entity);
        seen.insert(r.type);
        if (r.type == "BLOCK") {
            blocks.insert(r.first(2));
            block_record = owner;
            REQUIRE(record_name.at(owner) == r.first(2));
        } else if (r.type == "ENDBLK") {
            REQUIRE(owner == block_record);
            block_record.clear();
        } else if (sub) {
            continue;
        } else if (r.section == "BLOCKS") {
            REQUIRE(owner == block_record); // a block's entity is the block's
        } else {
            const bool paper = r.first(67) == "1";
            REQUIRE(record_name.at(owner) == (paper ? "*Paper_Space" : "*Model_Space"));
        }
    }
    REQUIRE(blocks == std::set<std::string>{"*Model_Space", "*Paper_Space", "PART", "ASSEMBLY"});
    for (const auto& [type, sub] : kSubclass) {
        INFO(type);
        REQUIRE(seen.contains(type));
    }

    // Where the second part goes: an arc's angles after AcDbArc, a text's vertical alignment
    // in its second AcDbText, an attribute definition's tag after AcDbAttributeDefinition, a
    // rotated dimension's points in AcDbAlignedDimension then AcDbRotatedDimension last.
    for (const Rec& r : recs) {
        if (r.type == "ARC") {
            REQUIRE(r.at(50, r.first(50)) > r.at(100, "AcDbArc"));
        } else if (r.type == "TEXT" && r.section == "ENTITIES" && r.first(1) == "TEXT") {
            REQUIRE(r.all(100) == std::vector<std::string>{"AcDbEntity", "AcDbText", "AcDbText"});
            REQUIRE(r.at(73, r.first(73)) > r.at(1, "TEXT"));
        } else if (r.type == "ATTDEF") {
            REQUIRE(r.at(2, "TAG") > r.at(100, "AcDbAttributeDefinition"));
        } else if (r.type == "ATTRIB") {
            REQUIRE(r.at(2, "TAG") > r.at(100, "AcDbAttribute"));
        } else if (r.type == "DIMENSION" && r.first(70) == "0") {
            const std::vector<std::string> subs = r.all(100);
            REQUIRE(subs == std::vector<std::string>{"AcDbEntity", "AcDbDimension", "AcDbAlignedDimension",
                                                     "AcDbRotatedDimension"});
            REQUIRE(r.at(1, "<> TYP") < r.at(100, "AcDbAlignedDimension")); // the override is AcDbDimension's
            REQUIRE(r.at(13, r.first(13)) > r.at(100, "AcDbAlignedDimension"));
        }
    }

    // The named object dictionary has ACAD_GROUP.
    const auto root = std::find_if(recs.begin(), recs.end(), [](const Rec& r) { return r.type == "DICTIONARY"; });
    REQUIRE(root != recs.end());
    REQUIRE(root->at(3, "ACAD_GROUP") != std::string::npos);
}

TEST_CASE("#95 reading Musa CAD's DXF back: the spaces' blocks are not blocks, the rest is as written") {
    const Document d = every_kind();
    Document back;
    REQUIRE(parse_dxf(serialize_dxf(d), back).ok);
    std::vector<std::string> names;
    for (const DocBlockDef& b : back.block_defs) {
        names.push_back(b.name);
    }
    REQUIRE(names == std::vector<std::string>{"PART", "ASSEMBLY"});
    REQUIRE(back.lines.size() == d.lines.size());
    REQUIRE(back.circles.size() == d.circles.size());
    REQUIRE(back.arcs.size() == d.arcs.size());
    REQUIRE(back.texts.size() >= d.texts.size());
    REQUIRE(back.dims.size() == d.dims.size());
    REQUIRE(back.inserts.size() == d.inserts.size());
    REQUIRE(back.inserts[0].attribs.size() == 1);
    REQUIRE(back.inserts[0].attribs[0].value == "42");
    REQUIRE(back.block_defs[0].attdefs.size() == 1);
    REQUIRE(back.texts[0].justify == d.texts[0].justify);
    REQUIRE(back.dims[0].text_override == "<> TYP");

    // A drawing that came in with *Model_Space as a block (read before they were left out)
    // writes it once, as the space.
    Document old = d;
    DocBlockDef space;
    space.name = "*Model_Space";
    old.block_defs.insert(old.block_defs.begin(), space);
    const std::string text = serialize_dxf(old);
    std::size_t n = 0;
    for (std::size_t at = text.find("\n*Model_Space\n"); at != std::string::npos;
         at = text.find("\n*Model_Space\n", at + 1)) {
        ++n;
    }
    REQUIRE(n == 3); // its BLOCK_RECORD, and its BLOCK's 2 and 3
}
