// TABLE entity (issue #22): the grid geometry, merges, alignment, style-driven text
// heights, pick, persistence and capture/recreate.
//
// Like every other entity here, a table stores its parameters (cells, column widths, row
// heights) and ALL drawable geometry is derived by one function, so the displayed,
// picked and bounded geometry cannot diverge.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "musacad/core/entity_bounds.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/grips.hpp"
#include "musacad/core/io/document.hpp"
#include "musacad/core/io/native_format.hpp"
#include "musacad/core/native_kernel_2d.hpp"
#include "musacad/core/table.hpp"
#include "musacad/core/text/stroke_font.hpp"

using namespace musacad::core;
using Catch::Approx;

namespace {

/// A rows x cols table of plain centred cells with the given texts (row-major).
EntityHandle make_table(GeometryStore& s, std::uint16_t rows, std::uint16_t cols,
                        const std::vector<std::string>& texts, double cw = 40.0,
                        double rh = 8.0, bool title = false, bool header = false) {
    std::vector<TableCell> cells;
    for (std::size_t i = 0; i < static_cast<std::size_t>(rows) * cols; ++i) {
        TableCell c;
        const std::string& t = i < texts.size() ? texts[i] : std::string{};
        c.str_offset = s.intern_string(t);
        c.str_len = static_cast<std::uint32_t>(t.size());
        cells.push_back(c);
    }
    return s.add_table(rows, cols, cells, std::vector<double>(cols, cw),
                       std::vector<double>(rows, rh), {0, 100}, 0.0, 0, title, header);
}

TableGeometry geom(const GeometryStore& s, EntityHandle h) {
    const TableData* t = s.table(h);
    const TableStyle* st = s.table_style(t->style);
    return compute_table_geometry(*t, s.table_cell_views(*t), s.table_col_widths(*t),
                                  s.table_row_heights(*t),
                                  st != nullptr ? *st : TableStyle{}, Rgb{});
}

} // namespace

TEST_CASE("#22: the grid is derived from the stored column widths and row heights") {
    GeometryStore s;
    const EntityHandle h = make_table(s, 3, 2, {"A", "B", "C", "D", "E", "F"}, 40.0, 8.0);
    const TableGeometry g = geom(s, h);

    REQUIRE(g.cell_quads.size() == 6);
    REQUIRE(g.width == Approx(80.0));  // 2 columns x 40
    REQUIRE(g.height == Approx(24.0)); // 3 rows x 8

    // Every cell is 40 x 8 and they tile the grid exactly.
    for (const std::array<Vec2, 4>& q : g.cell_quads) {
        REQUIRE(length(q[1] - q[0]) == Approx(40.0));
        REQUIRE(length(q[3] - q[0]) == Approx(8.0));
    }
    // Rows grow DOWNWARD from the insertion point (its top-left corner).
    REQUIRE(g.cell_quads[0][3].y == Approx(100.0)); // first row's top edge
    REQUIRE(g.cell_quads[0][0].y == Approx(92.0));  // ... and its bottom
}

TEST_CASE("#22: unequal column widths are honoured and cells still abut") {
    GeometryStore s;
    std::vector<TableCell> cells(3);
    for (TableCell& c : cells) {
        c.str_offset = s.intern_string("x");
        c.str_len = 1;
    }
    const EntityHandle h =
        s.add_table(1, 3, cells, {10.0, 25.0, 5.0}, {8.0}, {0, 0}, 0.0, 0);
    const TableGeometry g = geom(s, h);
    REQUIRE(g.width == Approx(40.0));
    REQUIRE(length(g.cell_quads[0][1] - g.cell_quads[0][0]) == Approx(10.0));
    REQUIRE(length(g.cell_quads[1][1] - g.cell_quads[1][0]) == Approx(25.0));
    REQUIRE(length(g.cell_quads[2][1] - g.cell_quads[2][0]) == Approx(5.0));
    // Each cell's right edge is the next one's left edge -- no gaps, no overlap.
    for (std::size_t i = 0; i + 1 < g.cell_quads.size(); ++i) {
        REQUIRE(distance(g.cell_quads[i][1], g.cell_quads[i + 1][0]) == Approx(0.0).margin(1e-12));
    }
}

TEST_CASE("#22: a merged cell spans its neighbours and covered cells are not drawn") {
    GeometryStore s;
    std::vector<TableCell> cells(6); // 2 rows x 3 cols
    for (TableCell& c : cells) {
        c.str_offset = s.intern_string("");
        c.str_len = 0;
    }
    // Merge the whole first row into one cell.
    cells[0].span_cols = 3;
    cells[1].span_cols = 0; // covered
    cells[2].span_cols = 0; // covered
    const EntityHandle h = s.add_table(2, 3, cells, {20.0, 20.0, 20.0}, {8.0, 8.0}, {0, 0},
                                       0.0, 0);
    const TableGeometry g = geom(s, h);

    // Four drawn cells: the merged one + the three below it.
    REQUIRE(g.cell_quads.size() == 4);
    // The merged cell is as wide as the whole table.
    REQUIRE(length(g.cell_quads[0][1] - g.cell_quads[0][0]) == Approx(60.0));
    // ... and the covered ones produced no rectangle, so a click there hits the merge.
    const Vec2 inside_merge{30.0, -4.0};
    REQUIRE(table_cell_at(g, inside_merge) == 0);
}

TEST_CASE("#22: cell text is centred, left- or right-aligned within its rectangle") {
    GeometryStore s;
    std::vector<TableCell> cells(3);
    const char* txt = "AB";
    for (std::size_t i = 0; i < 3; ++i) {
        cells[i].str_offset = s.intern_string(txt);
        cells[i].str_len = 2;
        cells[i].align = static_cast<std::uint8_t>(i); // Left, Center, Right
    }
    const EntityHandle h = s.add_table(1, 3, cells, {40.0, 40.0, 40.0}, {10.0}, {0, 0}, 0.0, 0);
    const TableGeometry g = geom(s, h);
    REQUIRE(g.text_pos.size() == 3);

    const double w = text::text_width("AB", g.text_height[0]);
    // Left: at the left edge + padding. Right: at the right edge - padding - width.
    REQUIRE(g.text_pos[0].x < g.text_pos[1].x);
    REQUIRE(g.text_pos[1].x < g.text_pos[2].x);
    // Centre really is centred in its 40-wide cell (which spans x 40..80).
    REQUIRE(g.text_pos[1].x == Approx(40.0 + (40.0 - w) * 0.5));
}

TEST_CASE("#22: title and header rows take their text heights from the style") {
    GeometryStore s;
    TableStyle st;
    st.name = "BOM";
    st.title_height = 6.0;
    st.header_height = 4.0;
    st.data_height = 2.0;
    const std::uint16_t si = s.add_table_style(st);

    std::vector<TableCell> cells(4); // 4 rows x 1 col: title, header, 2 data
    for (TableCell& c : cells) {
        c.str_offset = s.intern_string("X");
        c.str_len = 1;
    }
    const EntityHandle h = s.add_table(4, 1, cells, {30.0}, {10.0, 10.0, 10.0, 10.0}, {0, 0},
                                       0.0, si, /*has_title=*/true, /*has_header=*/true);
    const TableGeometry g = geom(s, h);
    REQUIRE(g.text_height.size() == 4);
    REQUIRE(g.text_height[0] == Approx(6.0)); // title
    REQUIRE(g.text_height[1] == Approx(4.0)); // header
    REQUIRE(g.text_height[2] == Approx(2.0)); // data
    REQUIRE(g.text_height[3] == Approx(2.0));
}

TEST_CASE("#22: cell text goes through the shared control-code pass") {
    GeometryStore s;
    const EntityHandle h = make_table(s, 1, 2, {"%%c50", "\\U+2300 12"});
    const TableGeometry g = geom(s, h);
    REQUIRE(g.cell_text[0] == "⌀50");
    REQUIRE(g.cell_text[1] == "⌀ 12");
}

TEST_CASE("#22: rotation turns the whole table rigidly") {
    GeometryStore s;
    std::vector<TableCell> cells(2);
    for (TableCell& c : cells) {
        c.str_offset = s.intern_string("");
        c.str_len = 0;
    }
    const EntityHandle flat = s.add_table(1, 2, cells, {10.0, 10.0}, {5.0}, {0, 0}, 0.0, 0);
    const EntityHandle turned =
        s.add_table(1, 2, cells, {10.0, 10.0}, {5.0}, {0, 0}, 1.5707963267948966, 0);
    const TableGeometry a = geom(s, flat);
    const TableGeometry b = geom(s, turned);
    REQUIRE(b.width == Approx(a.width));
    REQUIRE(b.height == Approx(a.height));
    // Cell sizes are preserved; only the orientation changed.
    REQUIRE(length(b.cell_quads[0][1] - b.cell_quads[0][0]) ==
            Approx(length(a.cell_quads[0][1] - a.cell_quads[0][0])));
    // The table now grows in +y instead of +x.
    REQUIRE(b.cell_quads[1][1].y > b.cell_quads[0][0].y + 1.0);
}

TEST_CASE("#22: pick reports the cell that was clicked; bounds enclose the drawing") {
    GeometryStore s;
    NativeKernel2D kernel;
    const EntityHandle h = make_table(s, 2, 2, {"A", "B", "C", "D"}, 20.0, 10.0);
    const TableGeometry g = geom(s, h);

    // Cell 3 is the bottom-right of a 2x2 grid whose top-left is (0,100).
    const Vec2 in_d{30.0, 85.0};
    REQUIRE(table_cell_at(g, in_d) == 3);
    REQUIRE(table_cell_at(g, {-5.0, 85.0}) == -1); // outside the table

    Vec2 cp{};
    REQUIRE(kernel.closest_point(s, h, in_d, cp)); // a click in a cell picks the table
    REQUIRE(cp.x == Approx(in_d.x));

    Vec2 lo{};
    Vec2 hi{};
    REQUIRE(entity_aabb(s, h, lo, hi));
    REQUIRE(lo.x == Approx(0.0));
    REQUIRE(hi.x == Approx(40.0));
    REQUIRE(hi.y == Approx(100.0));
    REQUIRE(lo.y == Approx(80.0));
}

TEST_CASE("#22: a malformed table draws nothing rather than a stray box") {
    GeometryStore s;
    TableData t; // zero rows/cols
    const TableGeometry g = compute_table_geometry(t, {}, {}, {}, TableStyle{}, Rgb{});
    REQUIRE(g.lines.empty());
    REQUIRE(g.cell_quads.empty());
}

TEST_CASE("#22: grips move the table and resize a column") {
    GeometryStore s;
    const EntityHandle h = make_table(s, 2, 3, {"a", "b", "c", "d", "e", "f"}, 20.0, 8.0);
    std::vector<Grip> grips;
    grips_of(s, h, grips);
    // Insertion point + one grip per INTERIOR column boundary (2 for 3 columns) + one
    // per interior ROW boundary (1 for 2 rows).
    REQUIRE(grips.size() == 4);

    GeometryStore moved;
    const EntityHandle m =
        add_command_to_store(moved, edit_for_grip_drag(s, h, 0, {5.0, 50.0}), EntityProps{});
    REQUIRE(moved.table(m)->pos == Vec2{5.0, 50.0});

    // Dragging the first column boundary to x=35 makes column 0 that wide.
    GeometryStore sized;
    const EntityHandle z =
        add_command_to_store(sized, edit_for_grip_drag(s, h, 1, {35.0, 100.0}), EntityProps{});
    REQUIRE(sized.table_col_widths(*sized.table(z))[0] == Approx(35.0));
    REQUIRE(sized.table_col_widths(*sized.table(z))[1] == Approx(20.0)); // others untouched
}

TEST_CASE("#22: a row-boundary grip resizes that row only") {
    // Rows are stored per-table and are what the grid is laid out from, so they are
    // draggable on the same footing as columns. The table sits at y=100 and local y
    // runs DOWN, so dragging the boundary to world y=85 makes row 0 fifteen tall.
    GeometryStore s;
    const EntityHandle h = make_table(s, 2, 3, {"a", "b", "c", "d", "e", "f"}, 20.0, 8.0);

    std::vector<Grip> grips;
    grips_of(s, h, grips);
    const auto row_grip = std::find_if(grips.begin(), grips.end(), [](const Grip& g) {
        return g.index >= kTableRowGripBase;
    });
    REQUIRE(row_grip != grips.end());
    REQUIRE(row_grip->index == kTableRowGripBase); // the only interior boundary
    REQUIRE(row_grip->pos.x == Approx(0.0));       // on the table's left edge
    REQUIRE(row_grip->pos.y == Approx(92.0));      // 100 - row 0's height of 8

    GeometryStore sized;
    const EntityHandle z = add_command_to_store(
        sized, edit_for_grip_drag(s, h, kTableRowGripBase, {0.0, 85.0}), EntityProps{});
    const std::span<const double> rh = sized.table_row_heights(*sized.table(z));
    REQUIRE(rh[0] == Approx(15.0));
    REQUIRE(rh[1] == Approx(8.0)); // the row below is untouched
    // Columns are not disturbed by a row drag.
    REQUIRE(sized.table_col_widths(*sized.table(z))[0] == Approx(20.0));
}

TEST_CASE("#22: row and column grips follow a ROTATED table's own axes") {
    // Both axes measure the drag in the table's frame, so a rotated table resizes
    // along itself rather than along the world axes.
    GeometryStore s;
    std::vector<TableCell> cells(4);
    const double a = kPi / 2.0; // +90 degrees: local +x points along world +y
    const EntityHandle h =
        s.add_table(2, 2, cells, {20.0, 20.0}, {8.0, 8.0}, {0.0, 0.0}, a, 0, false, false);

    std::vector<Grip> grips;
    grips_of(s, h, grips);
    // Column boundary at local x=20 -> world (0,20); row boundary at local y=8 ->
    // world (8,0), because local +y (down the page) rotates onto world +x.
    const auto col = std::find_if(grips.begin(), grips.end(),
                                  [](const Grip& g) { return g.index == 1; });
    const auto row = std::find_if(grips.begin(), grips.end(), [](const Grip& g) {
        return g.index == kTableRowGripBase;
    });
    REQUIRE(col != grips.end());
    REQUIRE(row != grips.end());
    REQUIRE(col->pos.x == Approx(0.0).margin(1e-9));
    REQUIRE(col->pos.y == Approx(20.0));
    REQUIRE(row->pos.x == Approx(8.0));
    REQUIRE(row->pos.y == Approx(0.0).margin(1e-9));

    // Drag the row boundary out to local y=20, i.e. world (20,0).
    GeometryStore sized;
    const EntityHandle z = add_command_to_store(
        sized, edit_for_grip_drag(s, h, kTableRowGripBase, {20.0, 0.0}), EntityProps{});
    REQUIRE(sized.table_row_heights(*sized.table(z))[0] == Approx(20.0));
    REQUIRE(sized.table_col_widths(*sized.table(z))[0] == Approx(20.0)); // unchanged
}

TEST_CASE("#22: a grip drag can never collapse a row or column to zero") {
    // Guarded the same way as the column path: a drag that would produce a
    // non-positive size is ignored, so a table cannot be grip-dragged into a
    // degenerate grid that draws nothing.
    GeometryStore s;
    const EntityHandle h = make_table(s, 2, 3, {"a", "b", "c", "d", "e", "f"}, 20.0, 8.0);

    GeometryStore r;
    const EntityHandle zr = add_command_to_store(
        r, edit_for_grip_drag(s, h, kTableRowGripBase, {0.0, 100.0}), EntityProps{});
    REQUIRE(r.table_row_heights(*r.table(zr))[0] == Approx(8.0)); // refused, kept

    GeometryStore c;
    const EntityHandle zc =
        add_command_to_store(c, edit_for_grip_drag(s, h, 1, {0.0, 100.0}), EntityProps{});
    REQUIRE(c.table_col_widths(*c.table(zc))[0] == Approx(20.0)); // refused, kept
}

TEST_CASE("#22: capture -> recreate preserves cells, sizes and spans") {
    GeometryStore s;
    std::vector<TableCell> cells(4);
    const char* txt[] = {"ITEM", "QTY", "M6 BOLT", "12"};
    for (std::size_t i = 0; i < 4; ++i) {
        cells[i].str_offset = s.intern_string(txt[i]);
        cells[i].str_len = static_cast<std::uint32_t>(std::string(txt[i]).size());
        cells[i].align = static_cast<std::uint8_t>(CellAlign::Left);
    }
    cells[0].span_cols = 2;
    cells[1].span_cols = 0;
    const EntityHandle h = s.add_table(2, 2, cells, {30.0, 15.0}, {6.0, 6.0}, {1, 2}, 0.25, 0,
                                       true, false);

    GeometryStore rebuilt;
    const EntityHandle h2 = add_command_to_store(rebuilt, capture_entity(s, h), EntityProps{});
    const TableData* b = rebuilt.table(h2);
    REQUIRE(b != nullptr);
    REQUIRE(b->rows == 2);
    REQUIRE(b->cols == 2);
    REQUIRE(b->has_title);
    REQUIRE(b->rotation == Approx(0.25));
    REQUIRE(rebuilt.table_col_widths(*b)[0] == Approx(30.0));
    const std::span<const TableCell> rc = rebuilt.table_cells(*b);
    REQUIRE(rc[0].span_cols == 2);
    REQUIRE(rc[1].span_cols == 0);
    REQUIRE(rebuilt.string_of(rc[2]) == "M6 BOLT");
    REQUIRE(rc[0].align == static_cast<std::uint8_t>(CellAlign::Left));
}

TEST_CASE("#22: v20 round-trips tables and the style table; v19 loads with none") {
    GeometryStore s;
    TableStyle st;
    st.name = "BOM Narrow";  // a multi-word name, to prove the name boundary
    st.title_height = 6.0;
    const std::uint16_t si = s.add_table_style(st);

    std::vector<TableCell> cells(4);
    const char* txt[] = {"PARTS LIST", "", "M6 x 20 BOLT", "12"};
    for (std::size_t i = 0; i < 4; ++i) {
        cells[i].str_offset = s.intern_string(txt[i]);
        cells[i].str_len = static_cast<std::uint32_t>(std::string(txt[i]).size());
    }
    cells[0].span_cols = 2;
    cells[1].span_cols = 0;
    s.add_table(2, 2, cells, {45.0, 15.0}, {8.0, 6.0}, {10, 90}, 0.0, si, true, false);

    const musacad::core::io::Document a = musacad::core::io::document_from_store(s);
    REQUIRE(a.format_version == musacad::core::io::kFormatVersion);
    REQUIRE(a.tables.size() == 1);

    const auto path = (std::filesystem::temp_directory_path() / "musacad_table.musa").string();
    REQUIRE(musacad::core::io::save_native(a, path).ok);
    musacad::core::io::Document b;
    REQUIRE(musacad::core::io::load_native(path, b).ok);
    GeometryStore restored;
    musacad::core::io::populate_store(restored, b);
    REQUIRE(musacad::core::io::document_from_store(restored) == a);

    REQUIRE(b.tables[0].cells[2].text == "M6 x 20 BOLT"); // spaces survive the line
    REQUIRE(b.tables[0].cells[0].span_cols == 2);
    REQUIRE(b.tables[0].col_widths[0] == Approx(45.0));
    bool found = false;
    for (const TableStyle& x : b.table_styles) {
        if (x.name == "BOM Narrow") {
            REQUIRE(x.title_height == Approx(6.0));
            found = true;
        }
    }
    REQUIRE(found);
    std::filesystem::remove(path);

    const std::string v19 =
        "MUSACAD 19\nLAYER 255 255 255 0 25 1 0 0 0\n"
        "LINE 0 0 1 1 0 7 255 255 255 0 25\nEND\n";
    musacad::core::io::Document doc;
    REQUIRE(musacad::core::io::parse_native(v19, doc).ok);
    REQUIRE(doc.tables.empty());
    REQUIRE(doc.lines.size() == 1);
}

TEST_CASE("#22: struct sizes -- tables live in a cold arena, hot structs untouched") {
    static_assert(sizeof(LineData) == 48, "hot struct: LineData must stay 48 B");
    static_assert(sizeof(CircleData) == 40, "hot struct: CircleData must stay 40 B");
    static_assert(sizeof(EntityProps) == 16, "hot struct: EntityProps must stay 16 B");
    static_assert(sizeof(TableCell) == 16, "TableCell is a pooled record -- keep it small");
    static_assert(sizeof(TableData) == 64, "TableData size changed -- update the docs too");
    SUCCEED();
}

// ---------------------------------------------------------------------------
// Cell text (issue #22 follow-up). A table was placeable but not fillable: the
// cells existed and rendered, but nothing in the app could put text in one.
//
// Editing rides the SAME path as TEXT/MTEXT/MLEADER rather than growing a
// parallel one -- EditTextContentCommand resolves the picked point to a cell and
// rewrites only that cell, so the existing double-click gesture and DDEDIT both
// work on tables for free, and the capture/recommit shape keeps it one undo group
// with layer, position, sizes and style preserved.
// ---------------------------------------------------------------------------

#include <chrono>
#include <thread>

#include "musacad/core/geometry_engine.hpp"

namespace {
template <class Pred>
bool wait_for(GeometryEngine& e, Pred pred) {
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

/// The 2x2 table used by the cell-editing cases: 40 wide, 8 tall, at (0,100).
/// Cell centres are therefore (20,96) (40,96) / (20,88) (60,88)... see each test.
AddTableCommand two_by_two() {
    AddTableCommand c;
    c.rows = 2;
    c.cols = 2;
    c.cells.assign(4, TableCell{});
    c.texts = {"", "", "", ""};
    c.col_widths = {40.0, 40.0};
    c.row_heights = {8.0, 8.0};
    c.pos = {0.0, 100.0};
    return c;
}

/// The stored text of every cell, in the order the snapshot publishes them.
std::vector<std::string> cell_texts(const RenderSnapshot& s) {
    std::vector<std::string> out;
    for (const TextEditTarget& t : s.text_edit_targets) {
        out.push_back(t.content);
    }
    return out;
}
} // namespace

TEST_CASE("#22: every cell is a text-edit target, empty ones included") {
    // An empty cell is exactly the one a user wants to type into, so it must still be
    // offered as a double-click target -- otherwise a fresh table is uneditable.
    GeometryEngine engine;
    engine.start();
    engine.submit(two_by_two());
    REQUIRE(wait_for(engine, [](const auto& s) { return s.text_edit_targets.size() == 4; }));
    for (const std::string& t : cell_texts(engine.snapshot())) {
        REQUIRE(t.empty());
    }
    engine.stop();
}

TEST_CASE("#22: editing a cell rewrites only that cell") {
    GeometryEngine engine;
    engine.start();
    engine.submit(two_by_two());
    REQUIRE(wait_for(engine, [](const auto& s) { return s.text_edit_targets.size() == 4; }));

    // Row 0 spans y=100..92, row 1 y=92..84; column 0 x=0..40, column 1 x=40..80.
    // Pick the middle of the top-left cell.
    engine.submit(EditTextContentCommand{{20.0, 96.0}, 1.0, "ITEM", 7});
    REQUIRE(wait_for(engine, [](const auto& s) {
        return s.status.find("Table cell edited.") != std::string::npos;
    }));
    engine.consume_snapshot();
    std::vector<std::string> texts = cell_texts(engine.snapshot());
    REQUIRE(texts.size() == 4);
    REQUIRE(texts[0] == "ITEM");
    REQUIRE(texts[1].empty());
    REQUIRE(texts[2].empty());
    REQUIRE(texts[3].empty());

    // A second cell, to prove the first survives and the pick maps to the right cell.
    engine.submit(EditTextContentCommand{{60.0, 88.0}, 1.0, "12", 8});
    REQUIRE(wait_for(engine, [](const auto& s) {
        std::vector<std::string> t = cell_texts(s);
        return t.size() == 4 && t[3] == "12";
    }));
    engine.consume_snapshot();
    texts = cell_texts(engine.snapshot());
    REQUIRE(texts[0] == "ITEM"); // untouched by the second edit
    REQUIRE(texts[3] == "12");
    engine.stop();
}

TEST_CASE("#22: a cell edit preserves the table's sizes, style and position") {
    // The capture -> change -> recommit shape must not quietly reset anything: this is
    // why editing goes through capture_entity rather than delete + recreate.
    GeometryEngine engine;
    engine.start();
    AddTableCommand c = two_by_two();
    c.col_widths = {30.0, 55.0};
    c.row_heights = {6.0, 11.0};
    c.rotation = 0.0;
    c.has_title = true;
    engine.submit(c);
    REQUIRE(wait_for(engine, [](const auto& s) { return s.text_edit_targets.size() == 4; }));
    const Vec2 bmin_before = engine.snapshot().bounds_min;
    const Vec2 bmax_before = engine.snapshot().bounds_max;

    engine.submit(EditTextContentCommand{{15.0, 97.0}, 1.0, "TITLE", 7});
    REQUIRE(wait_for(engine, [](const auto& s) {
        return s.status.find("Table cell edited.") != std::string::npos;
    }));
    engine.consume_snapshot();
    // Identical extent => widths, heights, position and rotation all came through.
    REQUIRE(engine.snapshot().bounds_min.x == Approx(bmin_before.x));
    REQUIRE(engine.snapshot().bounds_min.y == Approx(bmin_before.y));
    REQUIRE(engine.snapshot().bounds_max.x == Approx(bmax_before.x));
    REQUIRE(engine.snapshot().bounds_max.y == Approx(bmax_before.y));
    engine.stop();
}

TEST_CASE("#22: a cell edit is one undo group") {
    GeometryEngine engine;
    engine.start();
    engine.submit(two_by_two());
    REQUIRE(wait_for(engine, [](const auto& s) { return s.text_edit_targets.size() == 4; }));
    engine.submit(EditTextContentCommand{{20.0, 96.0}, 1.0, "QTY", 9});
    REQUIRE(wait_for(engine, [](const auto& s) {
        std::vector<std::string> t = cell_texts(s);
        return !t.empty() && t[0] == "QTY";
    }));
    engine.submit(UndoLastGroupCommand{});
    REQUIRE(wait_for(engine, [](const auto& s) {
        std::vector<std::string> t = cell_texts(s);
        return t.size() == 4 && t[0].empty(); // the whole table came back, empty again
    }));
    engine.stop();
}

TEST_CASE("#22: a cell keeps the RAW string so editing reopens what was typed") {
    // Cell text goes through the same control-code expansion as any other text. The
    // edit target must carry the raw `%%c`, not the diameter glyph it renders as, or a
    // round trip through the editor would silently rewrite the user's content.
    GeometryEngine engine;
    engine.start();
    engine.submit(two_by_two());
    REQUIRE(wait_for(engine, [](const auto& s) { return s.text_edit_targets.size() == 4; }));
    engine.submit(EditTextContentCommand{{20.0, 96.0}, 1.0, "%%c25", 7});
    REQUIRE(wait_for(engine, [](const auto& s) {
        std::vector<std::string> t = cell_texts(s);
        return !t.empty() && !t[0].empty();
    }));
    engine.consume_snapshot();
    REQUIRE(cell_texts(engine.snapshot())[0] == "%%c25");
    engine.stop();
}

TEST_CASE("#22: a cell edit survives a save/load round trip") {
    GeometryEngine engine;
    engine.start();
    engine.submit(two_by_two());
    REQUIRE(wait_for(engine, [](const auto& s) { return s.text_edit_targets.size() == 4; }));
    engine.submit(EditTextContentCommand{{20.0, 96.0}, 1.0, "M6 BOLT", 7});
    REQUIRE(wait_for(engine, [](const auto& s) {
        std::vector<std::string> t = cell_texts(s);
        return !t.empty() && t[0] == "M6 BOLT";
    }));

    const std::filesystem::path p =
        std::filesystem::temp_directory_path() / "musacad_table_cell_edit.musa";
    engine.submit(SaveDocumentCommand{p.string(), false});
    REQUIRE(wait_for(engine, [](const auto& s) {
        return s.status.find("Saved") != std::string::npos;
    }));
    engine.submit(NewDocumentCommand{});
    REQUIRE(wait_for(engine, [](const auto& s) { return s.text_edit_targets.empty(); }));
    engine.submit(OpenDocumentCommand{p.string()});
    REQUIRE(wait_for(engine, [](const auto& s) {
        std::vector<std::string> t = cell_texts(s);
        return t.size() == 4 && t[0] == "M6 BOLT";
    }));
    engine.stop();
    std::filesystem::remove(p);
}
