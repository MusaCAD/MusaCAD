// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/core/geometry_store.hpp"

#include <algorithm>

namespace musacad::core {

EntityHandle GeometryStore::add_ellipse(Vec2 center, Vec2 major, double ratio, double start,
                                        double end, EntityProps props) {
    ratio = std::clamp(ratio, 1e-6, 1.0);
    const auto slot = ellipses_.insert(EllipseData{center, major, ratio, start, end, props});
    return EntityHandle{slot.index, slot.generation, EntityKind::Ellipse};
}

bool GeometryStore::set_dim_aux(EntityHandle h, double aux) noexcept {
    if (h.kind != EntityKind::Dimension) {
        return false;
    }
    if (DimData* d = dims_.get(h.index, h.generation)) {
        d->aux = aux;
        return true;
    }
    return false;
}

EntityHandle GeometryStore::add_xline(Vec2 base, Vec2 dir, bool ray, EntityProps props) {
    const double len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    const Vec2 unit = len > 1e-12 ? Vec2{dir.x / len, dir.y / len} : Vec2{1.0, 0.0};
    const auto slot = xlines_.insert(XlineData{base, unit, ray, props});
    return EntityHandle{slot.index, slot.generation, EntityKind::Xline};
}

EntityHandle GeometryStore::add_point(Vec2 p, EntityProps props) {
    const auto slot = points_.insert(PointData{p, props});
    return EntityHandle{slot.index, slot.generation, EntityKind::Point};
}

EntityHandle GeometryStore::add_line(Vec2 a, Vec2 b, EntityProps props) {
    const auto slot = lines_.insert(LineData{a, b, props});
    return EntityHandle{slot.index, slot.generation, EntityKind::Line};
}

EntityHandle GeometryStore::add_circle(Vec2 center, double radius, EntityProps props) {
    const auto slot = circles_.insert(CircleData{center, radius, props});
    return EntityHandle{slot.index, slot.generation, EntityKind::Circle};
}

EntityHandle GeometryStore::add_arc(Vec2 center, double radius, double start_angle,
                                    double end_angle, EntityProps props) {
    const auto slot = arcs_.insert(ArcData{center, radius, start_angle, end_angle, props});
    return EntityHandle{slot.index, slot.generation, EntityKind::Arc};
}

EntityHandle GeometryStore::add_polyline(std::span<const Vec2> vertices, bool closed,
                                         EntityProps props) {
    return add_polyline(vertices, {}, closed, props);
}

EntityHandle GeometryStore::add_polyline(std::span<const Vec2> vertices,
                                         std::span<const double> bulges, bool closed,
                                         EntityProps props) {
    const auto offset = static_cast<std::uint32_t>(polyline_pool_.size());
    polyline_pool_.insert(polyline_pool_.end(), vertices.begin(), vertices.end());
    // Store bulges only if any are non-zero (keep straight polylines lean).
    std::uint32_t bulge_offset = PolylineData::kNoBulges;
    const bool any = !bulges.empty() &&
                     std::any_of(bulges.begin(), bulges.end(), [](double b) { return b != 0.0; });
    if (any && bulges.size() == vertices.size()) {
        bulge_offset = static_cast<std::uint32_t>(bulge_pool_.size());
        bulge_pool_.insert(bulge_pool_.end(), bulges.begin(), bulges.end());
    }
    const auto slot = polylines_.insert(PolylineData{
        offset, static_cast<std::uint32_t>(vertices.size()), bulge_offset, closed, props});
    return EntityHandle{slot.index, slot.generation, EntityKind::Polyline};
}

EntityHandle GeometryStore::add_spline(std::span<const Vec2> control_points, std::uint32_t degree,
                                       EntityProps props) {
    const auto offset = static_cast<std::uint32_t>(spline_pool_.size());
    spline_pool_.insert(spline_pool_.end(), control_points.begin(), control_points.end());
    const auto slot = splines_.insert(
        SplineData{offset, static_cast<std::uint32_t>(control_points.size()), degree, props});
    return EntityHandle{slot.index, slot.generation, EntityKind::Spline};
}

EntityHandle GeometryStore::add_text(Vec2 pos, double height, double rotation, std::uint8_t justify,
                                     std::string_view content, EntityProps props,
                                     std::uint16_t font, std::uint16_t style) {
    const auto offset = static_cast<std::uint32_t>(string_pool_.size());
    string_pool_.insert(string_pool_.end(), content.begin(), content.end());
    const auto slot =
        texts_.insert(TextData{pos, height, rotation, justify, font, style, offset,
                               static_cast<std::uint32_t>(content.size()), props});
    return EntityHandle{slot.index, slot.generation, EntityKind::Text};
}

EntityHandle GeometryStore::add_dimension(DimType type, Vec2 a, Vec2 b, Vec2 line_pt,
                                          std::uint16_t style, EntityProps props,
                                          DimOverrides overrides, std::string_view prefix,
                                          std::string_view suffix, DimTolerance tol,
                                          std::string_view text_override, Vec2 text_offset) {
    const auto poff = static_cast<std::uint32_t>(string_pool_.size());
    string_pool_.insert(string_pool_.end(), prefix.begin(), prefix.end());
    const auto soff = static_cast<std::uint32_t>(string_pool_.size());
    string_pool_.insert(string_pool_.end(), suffix.begin(), suffix.end());
    const auto ooff = static_cast<std::uint32_t>(string_pool_.size());
    string_pool_.insert(string_pool_.end(), text_override.begin(), text_override.end());
    const auto slot = dims_.insert(DimData{type, a, b, line_pt, style, props, overrides, tol, poff,
                                           static_cast<std::uint32_t>(prefix.size()), soff,
                                           static_cast<std::uint32_t>(suffix.size()), ooff,
                                           static_cast<std::uint32_t>(text_override.size()),
                                           text_offset});
    return EntityHandle{slot.index, slot.generation, EntityKind::Dimension};
}

EntityHandle GeometryStore::add_leader(Vec2 tip, Vec2 knee, double text_height, std::uint16_t style,
                                       std::string_view content, EntityProps props,
                                       std::uint16_t font, DimOverrides overrides) {
    const auto offset = static_cast<std::uint32_t>(string_pool_.size());
    string_pool_.insert(string_pool_.end(), content.begin(), content.end());
    const auto slot = leaders_.insert(LeaderData{tip, knee, text_height, style, font, offset,
                                                 static_cast<std::uint32_t>(content.size()), props,
                                                 overrides});
    return EntityHandle{slot.index, slot.generation, EntityKind::Leader};
}

EntityHandle GeometryStore::add_fcf(const std::vector<std::string>& cells, Vec2 pos,
                                    double rotation, std::uint16_t style, EntityProps props,
                                    DimOverrides overrides) {
    // Cells are variable-length, so they live in a shared pool as an (offset, count)
    // view -- the polyline/spline pattern -- and each cell's text goes in the shared
    // char pool like every other string in the store.
    const auto cell_offset = static_cast<std::uint32_t>(fcf_cell_pool_.size());
    for (const std::string& c : cells) {
        const auto off = static_cast<std::uint32_t>(string_pool_.size());
        string_pool_.insert(string_pool_.end(), c.begin(), c.end());
        fcf_cell_pool_.push_back(FcfCell{off, static_cast<std::uint32_t>(c.size())});
    }
    const auto slot = fcfs_.insert(FcfData{cell_offset, static_cast<std::uint32_t>(cells.size()),
                                           pos, rotation, style, props, overrides});
    return EntityHandle{slot.index, slot.generation, EntityKind::Fcf};
}

EntityHandle GeometryStore::add_datum(std::string_view letter, Vec2 tip, Vec2 pos, double rotation,
                                      std::uint16_t style, EntityProps props,
                                      DimOverrides overrides) {
    const auto offset = static_cast<std::uint32_t>(string_pool_.size());
    string_pool_.insert(string_pool_.end(), letter.begin(), letter.end());
    const auto slot = datums_.insert(DatumData{tip, pos, rotation, style, offset,
                                               static_cast<std::uint32_t>(letter.size()), props,
                                               overrides});
    return EntityHandle{slot.index, slot.generation, EntityKind::Datum};
}

EntityHandle GeometryStore::add_image(std::uint16_t def, Vec2 pos, double width, double height,
                                     double rotation, EntityProps props) {
    ImageData d;
    d.def = def;
    d.pos = pos;
    d.width = width;
    d.height = height;
    d.rotation = rotation;
    d.props = props;
    const auto slot = images_.insert(d);
    return EntityHandle{slot.index, slot.generation, EntityKind::Image};
}

std::uint16_t GeometryStore::add_image_def(const ImageDef& def) {
    // Get-or-add by payload identity (the add_layer/add_dimstyle/add_block shape), so
    // placing one logo ten times holds ONE definition and one copy of the bytes.
    for (std::size_t i = 0; i < image_defs_.size(); ++i) {
        const ImageDef& e = image_defs_[i];
        if (e.source == def.source && e.bytes == def.bytes) {
            return static_cast<std::uint16_t>(i);
        }
    }
    image_defs_.push_back(def);
    return static_cast<std::uint16_t>(image_defs_.size() - 1);
}

void GeometryStore::set_image_def_table(std::vector<ImageDef> defs) {
    image_defs_ = std::move(defs);
}

EntityHandle GeometryStore::add_table(std::uint16_t rows, std::uint16_t cols,
                                     const std::vector<TableCell>& cells,
                                     const std::vector<double>& col_widths,
                                     const std::vector<double>& row_heights, Vec2 pos,
                                     double rotation, std::uint16_t style, bool has_title,
                                     bool has_header, EntityProps props) {
    // Cells and sizes are variable-length, so they live in shared pools as (offset,count)
    // views -- the polyline/spline/FCF pattern. Cell TEXT is already in the char pool.
    const auto cell_offset = static_cast<std::uint32_t>(table_cell_pool_.size());
    table_cell_pool_.insert(table_cell_pool_.end(), cells.begin(), cells.end());
    const auto size_offset = static_cast<std::uint32_t>(table_size_pool_.size());
    table_size_pool_.insert(table_size_pool_.end(), col_widths.begin(), col_widths.end());
    table_size_pool_.insert(table_size_pool_.end(), row_heights.begin(), row_heights.end());
    TableData t;
    t.cell_offset = cell_offset;
    t.size_offset = size_offset;
    t.rows = rows;
    t.cols = cols;
    t.has_title = has_title;
    t.has_header = has_header;
    t.pos = pos;
    t.rotation = rotation;
    t.style = style;
    t.props = props;
    const auto slot = tables_.insert(t);
    return EntityHandle{slot.index, slot.generation, EntityKind::Table};
}

std::uint16_t GeometryStore::add_table_style(const TableStyle& st) {
    for (std::size_t i = 0; i < table_styles_.size(); ++i) {
        if (table_styles_[i].name == st.name) {
            return static_cast<std::uint16_t>(i);
        }
    }
    table_styles_.push_back(st);
    return static_cast<std::uint16_t>(table_styles_.size() - 1);
}

void GeometryStore::set_table_style_table(std::vector<TableStyle> styles) {
    table_styles_ = std::move(styles);
    if (table_styles_.empty()) {
        table_styles_.push_back(TableStyle{"Standard"});
    }
}

std::span<const TableCell> GeometryStore::table_cells(const TableData& t) const noexcept {
    return {table_cell_pool_.data() + t.cell_offset,
            static_cast<std::size_t>(t.rows) * t.cols};
}

std::span<const double> GeometryStore::table_col_widths(const TableData& t) const noexcept {
    return {table_size_pool_.data() + t.size_offset, t.cols};
}

std::span<const double> GeometryStore::table_row_heights(const TableData& t) const noexcept {
    return {table_size_pool_.data() + t.size_offset + t.cols, t.rows};
}

std::uint32_t GeometryStore::intern_string(std::string_view s) {
    const auto off = static_cast<std::uint32_t>(string_pool_.size());
    string_pool_.insert(string_pool_.end(), s.begin(), s.end());
    return off;
}

std::string_view GeometryStore::string_of(const TableCell& c) const noexcept {
    return {string_pool_.data() + c.str_offset, c.str_len};
}

std::vector<TableCellView> GeometryStore::table_cell_views(const TableData& t) const {
    std::vector<TableCellView> out;
    const std::span<const TableCell> cells = table_cells(t);
    out.reserve(cells.size());
    for (const TableCell& c : cells) {
        out.push_back(TableCellView{string_of(c), c.span_cols, c.span_rows, c.align});
    }
    return out;
}

EntityHandle GeometryStore::add_mtext(const MTextBlock& block, std::string_view content,
                                      EntityProps props) {
    MTextBlock b = block;
    b.str_offset = static_cast<std::uint32_t>(string_pool_.size());
    b.str_len = static_cast<std::uint32_t>(content.size());
    string_pool_.insert(string_pool_.end(), content.begin(), content.end());
    const auto slot = mtexts_.insert(MTextData{b, props});
    return EntityHandle{slot.index, slot.generation, EntityKind::MText};
}

EntityHandle GeometryStore::add_mleader(std::span<const Vec2> vertices, std::uint16_t style,
                                        const MTextBlock& text, std::string_view content,
                                        EntityProps props, DimOverrides overrides) {
    const auto voff = static_cast<std::uint32_t>(polyline_pool_.size());
    polyline_pool_.insert(polyline_pool_.end(), vertices.begin(), vertices.end());
    MTextBlock b = text;
    b.str_offset = static_cast<std::uint32_t>(string_pool_.size());
    b.str_len = static_cast<std::uint32_t>(content.size());
    string_pool_.insert(string_pool_.end(), content.begin(), content.end());
    const auto slot = mleaders_.insert(MLeaderData{
        voff, static_cast<std::uint32_t>(vertices.size()), style, b, props, overrides});
    return EntityHandle{slot.index, slot.generation, EntityKind::MLeader};
}

namespace {
std::string pack_attribs(const std::vector<std::string>& values) {
    std::string packed;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            packed += '\x1F';
        }
        packed += values[i];
    }
    return packed;
}
} // namespace

EntityHandle GeometryStore::add_insert(std::uint16_t block, Vec2 pos, double scale_x,
                                       double scale_y, double rotation, EntityProps props,
                                       const std::vector<std::string>& attribs) {
    InsertData d{block, pos, scale_x, scale_y, rotation, props};
    if (!attribs.empty()) {
        const std::string packed = pack_attribs(attribs);
        d.attr_offset = static_cast<std::uint32_t>(string_pool_.size());
        d.attr_len = static_cast<std::uint32_t>(packed.size());
        string_pool_.insert(string_pool_.end(), packed.begin(), packed.end());
    }
    const auto slot = inserts_.insert(d);
    return EntityHandle{slot.index, slot.generation, EntityKind::Insert};
}

std::vector<std::string> GeometryStore::insert_attribs(const InsertData& in) const {
    std::vector<std::string> out;
    if (in.attr_len == 0) {
        return out;
    }
    const std::string_view packed(string_pool_.data() + in.attr_offset, in.attr_len);
    std::size_t start = 0;
    while (true) {
        const std::size_t sep = packed.find('\x1F', start);
        out.emplace_back(packed.substr(start, sep == std::string_view::npos ? std::string_view::npos
                                                                            : sep - start));
        if (sep == std::string_view::npos) {
            break;
        }
        start = sep + 1;
    }
    return out;
}

bool GeometryStore::set_insert_attribs(EntityHandle h, const std::vector<std::string>& values) {
    InsertData* d = h.kind == EntityKind::Insert ? inserts_.get(h.index, h.generation) : nullptr;
    if (d == nullptr) {
        return false;
    }
    const std::string packed = pack_attribs(values);
    d->attr_offset = static_cast<std::uint32_t>(string_pool_.size());
    d->attr_len = static_cast<std::uint32_t>(packed.size());
    string_pool_.insert(string_pool_.end(), packed.begin(), packed.end());
    return true;
}

EntityHandle GeometryStore::add_attdef(Vec2 pos, double height, double rotation,
                                       std::uint8_t justify, std::string_view tag,
                                       std::string_view prompt, std::string_view def,
                                       std::uint8_t flags, EntityProps props, std::uint16_t font,
                                       std::uint16_t style) {
    AttDefData a;
    const auto toff = static_cast<std::uint32_t>(string_pool_.size());
    string_pool_.insert(string_pool_.end(), tag.begin(), tag.end());
    a.text = TextData{pos, height, rotation, justify, font, style, toff,
                      static_cast<std::uint32_t>(tag.size()), props};
    a.prompt_offset = static_cast<std::uint32_t>(string_pool_.size());
    string_pool_.insert(string_pool_.end(), prompt.begin(), prompt.end());
    a.prompt_len = static_cast<std::uint32_t>(prompt.size());
    a.def_offset = static_cast<std::uint32_t>(string_pool_.size());
    string_pool_.insert(string_pool_.end(), def.begin(), def.end());
    a.def_len = static_cast<std::uint32_t>(def.size());
    a.flags = flags;
    const auto slot = attdefs_.insert(a);
    return EntityHandle{slot.index, slot.generation, EntityKind::AttDef};
}

EntityHandle GeometryStore::add_hatch(const std::vector<std::vector<Vec2>>& loops,
                                      std::string_view pattern, double scale, double angle,
                                      Vec2 origin, EntityProps props, Rgb color2) {
    HatchData d;
    d.vtx_offset = static_cast<std::uint32_t>(hatch_vtx_pool_.size());
    d.loop_offset = static_cast<std::uint32_t>(hatch_loop_lens_.size());
    for (const std::vector<Vec2>& loop : loops) {
        hatch_loop_lens_.push_back(static_cast<std::uint32_t>(loop.size()));
        hatch_vtx_pool_.insert(hatch_vtx_pool_.end(), loop.begin(), loop.end());
        d.vtx_count += static_cast<std::uint32_t>(loop.size());
    }
    d.loop_count = static_cast<std::uint32_t>(loops.size());
    d.str_offset = static_cast<std::uint32_t>(string_pool_.size());
    d.str_len = static_cast<std::uint32_t>(pattern.size());
    string_pool_.insert(string_pool_.end(), pattern.begin(), pattern.end());
    d.pattern_scale = scale;
    d.pattern_angle = angle;
    d.pattern_origin = origin;
    d.props = props;
    d.color2 = color2;
    const auto slot = hatches_.insert(d);
    return EntityHandle{slot.index, slot.generation, EntityKind::Hatch};
}

bool GeometryStore::remove(EntityHandle h) noexcept {
    celtscale_.erase(celtscale_key(h)); // drop any per-entity scale so a reused slot defaults
    switch (h.kind) {
    case EntityKind::Point:
        return points_.erase(h.index, h.generation);
    case EntityKind::Xline:
        return xlines_.erase(h.index, h.generation);
    case EntityKind::Ellipse:
        return ellipses_.erase(h.index, h.generation);
    case EntityKind::Line:
        return lines_.erase(h.index, h.generation);
    case EntityKind::Polyline:
        return polylines_.erase(h.index, h.generation);
    case EntityKind::Circle:
        return circles_.erase(h.index, h.generation);
    case EntityKind::Arc:
        return arcs_.erase(h.index, h.generation);
    case EntityKind::Spline:
        return splines_.erase(h.index, h.generation);
    case EntityKind::Text:
        return texts_.erase(h.index, h.generation);
    case EntityKind::AttDef:
        return attdefs_.erase(h.index, h.generation);
    case EntityKind::Dimension:
        return dims_.erase(h.index, h.generation);
    case EntityKind::Leader:
        return leaders_.erase(h.index, h.generation);
    case EntityKind::MText:
        return mtexts_.erase(h.index, h.generation);
    case EntityKind::MLeader:
        return mleaders_.erase(h.index, h.generation);
    case EntityKind::Insert:
        return inserts_.erase(h.index, h.generation);
    case EntityKind::Hatch:
        return hatches_.erase(h.index, h.generation);
    case EntityKind::Fcf:
        return fcfs_.erase(h.index, h.generation);
    case EntityKind::Datum:
        return datums_.erase(h.index, h.generation);
    case EntityKind::Image:
        return images_.erase(h.index, h.generation);
    case EntityKind::Table:
        return tables_.erase(h.index, h.generation);
    }
    return false;
}

bool GeometryStore::is_valid(EntityHandle h) const noexcept {
    switch (h.kind) {
    case EntityKind::Point:
        return points_.is_valid(h.index, h.generation);
    case EntityKind::Xline:
        return xlines_.is_valid(h.index, h.generation);
    case EntityKind::Ellipse:
        return ellipses_.is_valid(h.index, h.generation);
    case EntityKind::Line:
        return lines_.is_valid(h.index, h.generation);
    case EntityKind::Polyline:
        return polylines_.is_valid(h.index, h.generation);
    case EntityKind::Circle:
        return circles_.is_valid(h.index, h.generation);
    case EntityKind::Arc:
        return arcs_.is_valid(h.index, h.generation);
    case EntityKind::Spline:
        return splines_.is_valid(h.index, h.generation);
    case EntityKind::Text:
        return texts_.is_valid(h.index, h.generation);
    case EntityKind::AttDef:
        return attdefs_.is_valid(h.index, h.generation);
    case EntityKind::Dimension:
        return dims_.is_valid(h.index, h.generation);
    case EntityKind::Leader:
        return leaders_.is_valid(h.index, h.generation);
    case EntityKind::MText:
        return mtexts_.is_valid(h.index, h.generation);
    case EntityKind::MLeader:
        return mleaders_.is_valid(h.index, h.generation);
    case EntityKind::Insert:
        return inserts_.is_valid(h.index, h.generation);
    case EntityKind::Hatch:
        return hatches_.is_valid(h.index, h.generation);
    case EntityKind::Fcf:
        return fcfs_.is_valid(h.index, h.generation);
    case EntityKind::Datum:
        return datums_.is_valid(h.index, h.generation);
    case EntityKind::Image:
        return images_.is_valid(h.index, h.generation);
    case EntityKind::Table:
        return tables_.is_valid(h.index, h.generation);
    }
    return false;
}

std::size_t GeometryStore::live_count() const noexcept {
    return points_.live_count() + lines_.live_count() + polylines_.live_count() +
           circles_.live_count() + arcs_.live_count() + splines_.live_count() +
           texts_.live_count() + dims_.live_count() + leaders_.live_count() +
           mtexts_.live_count() + mleaders_.live_count() + inserts_.live_count() +
           hatches_.live_count() + fcfs_.live_count() + datums_.live_count() +
           images_.live_count() + tables_.live_count();
}

void GeometryStore::clear() noexcept {
    points_.clear();
    lines_.clear();
    circles_.clear();
    arcs_.clear();
    polylines_.clear();
    splines_.clear();
    texts_.clear();
    attdefs_.clear();
    dims_.clear();
    leaders_.clear();
    mtexts_.clear();
    mleaders_.clear();
    inserts_.clear();
    hatches_.clear();
    fcfs_.clear();
    datums_.clear();
    images_.clear();
    image_defs_.clear();
    tables_.clear();
    table_cell_pool_.clear();
    table_size_pool_.clear();
    fcf_cell_pool_.clear();
    polyline_pool_.clear();
    bulge_pool_.clear();
    spline_pool_.clear();
    string_pool_.clear();
    hatch_vtx_pool_.clear();
    hatch_loop_lens_.clear();
    celtscale_.clear();
    layers_.assign(1, Layer{"0"}); // reset to just layer 0
    current_layer_ = 0;
    dimstyles_.assign(1, DimStyle{"Standard"});
    // Every table a fresh store starts with, restored here too: a store that was moved
    // out of (a document parked behind another tab) and then cleared must be a fresh
    // store again, Standard text style included.
    text_styles_.assign(1, TextStyle{}); // [0] = Standard
    current_text_style_ = 0;
    named_views_.clear();
    groups_.clear();
    units_ = DrawingUnits{};
    wipeout_frames_ = true;
    attdisp_ = 0;
    image_frame_ = 1;
    ltscale_ = 1.0;
    blocks_.clear();
    fonts_.assign(1, std::string{}); // reset to just the stroke font
}

const EllipseData* GeometryStore::ellipse(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Ellipse ? ellipses_.get(h.index, h.generation) : nullptr;
}

const XlineData* GeometryStore::xline(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Xline ? xlines_.get(h.index, h.generation) : nullptr;
}

const PointData* GeometryStore::point(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Point ? points_.get(h.index, h.generation) : nullptr;
}
const LineData* GeometryStore::line(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Line ? lines_.get(h.index, h.generation) : nullptr;
}
const CircleData* GeometryStore::circle(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Circle ? circles_.get(h.index, h.generation) : nullptr;
}
const ArcData* GeometryStore::arc(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Arc ? arcs_.get(h.index, h.generation) : nullptr;
}
const PolylineData* GeometryStore::polyline(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Polyline ? polylines_.get(h.index, h.generation) : nullptr;
}
const SplineData* GeometryStore::spline(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Spline ? splines_.get(h.index, h.generation) : nullptr;
}
const TextData* GeometryStore::text(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Text ? texts_.get(h.index, h.generation) : nullptr;
}
const AttDefData* GeometryStore::attdef(EntityHandle h) const noexcept {
    return h.kind == EntityKind::AttDef ? attdefs_.get(h.index, h.generation) : nullptr;
}
const DimData* GeometryStore::dimension(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Dimension ? dims_.get(h.index, h.generation) : nullptr;
}
const LeaderData* GeometryStore::leader(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Leader ? leaders_.get(h.index, h.generation) : nullptr;
}
const MTextData* GeometryStore::mtext(EntityHandle h) const noexcept {
    return h.kind == EntityKind::MText ? mtexts_.get(h.index, h.generation) : nullptr;
}
const MLeaderData* GeometryStore::mleader(EntityHandle h) const noexcept {
    return h.kind == EntityKind::MLeader ? mleaders_.get(h.index, h.generation) : nullptr;
}
const InsertData* GeometryStore::insert(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Insert ? inserts_.get(h.index, h.generation) : nullptr;
}
const HatchData* GeometryStore::hatch(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Hatch ? hatches_.get(h.index, h.generation) : nullptr;
}

const FcfData* GeometryStore::fcf(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Fcf ? fcfs_.get(h.index, h.generation) : nullptr;
}

const DatumData* GeometryStore::datum(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Datum ? datums_.get(h.index, h.generation) : nullptr;
}

ImageData* GeometryStore::mutable_image(EntityHandle h) noexcept {
    return h.kind == EntityKind::Image ? images_.get(h.index, h.generation) : nullptr;
}

const ImageData* GeometryStore::image(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Image ? images_.get(h.index, h.generation) : nullptr;
}

const TableData* GeometryStore::table(EntityHandle h) const noexcept {
    return h.kind == EntityKind::Table ? tables_.get(h.index, h.generation) : nullptr;
}
std::string_view GeometryStore::string_of(const TextData& t) const noexcept {
    return std::string_view(string_pool_.data() + t.str_offset, t.str_len);
}
std::string_view GeometryStore::attdef_prompt(const AttDefData& a) const noexcept {
    return std::string_view(string_pool_.data() + a.prompt_offset, a.prompt_len);
}
std::string_view GeometryStore::attdef_default(const AttDefData& a) const noexcept {
    return std::string_view(string_pool_.data() + a.def_offset, a.def_len);
}
std::string_view GeometryStore::string_of(const LeaderData& l) const noexcept {
    return std::string_view(string_pool_.data() + l.str_offset, l.str_len);
}
std::string_view GeometryStore::dim_prefix(const DimData& d) const noexcept {
    return std::string_view(string_pool_.data() + d.prefix_offset, d.prefix_len);
}
std::string_view GeometryStore::dim_suffix(const DimData& d) const noexcept {
    return std::string_view(string_pool_.data() + d.suffix_offset, d.suffix_len);
}
std::string_view GeometryStore::string_of(const MTextBlock& b) const noexcept {
    return std::string_view(string_pool_.data() + b.str_offset, b.str_len);
}
std::span<const Vec2> GeometryStore::vertices_of(const MLeaderData& m) const noexcept {
    return std::span<const Vec2>(polyline_pool_).subspan(m.vtx_offset, m.vtx_count);
}
std::string_view GeometryStore::string_of(const HatchData& h) const noexcept {
    return std::string_view(string_pool_.data() + h.str_offset, h.str_len);
}

std::string_view GeometryStore::string_of(const FcfCell& c) const noexcept {
    return {string_pool_.data() + c.str_offset, c.str_len};
}

std::string_view GeometryStore::string_of(const DatumData& d) const noexcept {
    return {string_pool_.data() + d.str_offset, d.str_len};
}

std::span<const FcfCell> GeometryStore::fcf_cells(const FcfData& f) const noexcept {
    return {fcf_cell_pool_.data() + f.cell_offset, f.cell_count};
}

std::vector<std::string_view> GeometryStore::fcf_cell_text(const FcfData& f) const {
    std::vector<std::string_view> out;
    out.reserve(f.cell_count);
    for (const FcfCell& c : fcf_cells(f)) {
        out.push_back(string_of(c));
    }
    return out;
}
std::vector<std::vector<Vec2>> GeometryStore::hatch_loops(const HatchData& h) const {
    std::vector<std::vector<Vec2>> loops;
    loops.reserve(h.loop_count);
    std::uint32_t v = h.vtx_offset;
    for (std::uint32_t i = 0; i < h.loop_count; ++i) {
        const std::uint32_t n = hatch_loop_lens_[h.loop_offset + i];
        loops.emplace_back(hatch_vtx_pool_.begin() + v, hatch_vtx_pool_.begin() + v + n);
        v += n;
    }
    return loops;
}
std::span<const Vec2> GeometryStore::hatch_verts(const HatchData& h) const noexcept {
    return std::span<const Vec2>(hatch_vtx_pool_).subspan(h.vtx_offset, h.vtx_count);
}

const DimStyle* GeometryStore::dimstyle(std::uint16_t index) const noexcept {
    return index < dimstyles_.size() ? &dimstyles_[index] : nullptr;
}
std::uint16_t GeometryStore::add_dimstyle(const DimStyle& style) {
    for (std::size_t i = 0; i < dimstyles_.size(); ++i) {
        if (dimstyles_[i].name == style.name) {
            return static_cast<std::uint16_t>(i);
        }
    }
    dimstyles_.push_back(style);
    return static_cast<std::uint16_t>(dimstyles_.size() - 1);
}
void GeometryStore::set_dimstyle_table(std::vector<DimStyle> styles) {
    dimstyles_ = std::move(styles);
    if (dimstyles_.empty()) {
        dimstyles_.push_back(DimStyle{"Standard"});
    }
    dimstyles_[0].name = "Standard";
}

bool GeometryStore::set_dimstyle(std::uint16_t index, const DimStyle& style) {
    if (index >= dimstyles_.size()) {
        return false;
    }
    DimStyle updated = style;
    if (index == 0) {
        updated.name = "Standard"; // index 0 is always "Standard"
    }
    dimstyles_[index] = updated;
    return true;
}

std::span<const Vec2> GeometryStore::vertices_of(const PolylineData& pl) const noexcept {
    return std::span<const Vec2>(polyline_pool_).subspan(pl.offset, pl.count);
}
std::span<const double> GeometryStore::bulges_of(const PolylineData& pl) const noexcept {
    if (pl.bulge_offset == PolylineData::kNoBulges) {
        return {};
    }
    return std::span<const double>(bulge_pool_).subspan(pl.bulge_offset, pl.count);
}
std::span<const Vec2> GeometryStore::control_points_of(const SplineData& sp) const noexcept {
    return std::span<const Vec2>(spline_pool_).subspan(sp.offset, sp.count);
}

// --- per-entity properties -------------------------------------------------

const EntityProps* GeometryStore::props(EntityHandle h) const noexcept {
    switch (h.kind) {
    case EntityKind::Point:
        if (const PointData* d = point(h)) {
            return &d->props;
        }
        break;
    case EntityKind::Xline:
        if (const XlineData* d = xline(h)) {
            return &d->props;
        }
        break;
    case EntityKind::Ellipse:
        if (const EllipseData* d = ellipse(h)) {
            return &d->props;
        }
        break;
    case EntityKind::Line:
        if (const LineData* d = line(h)) {
            return &d->props;
        }
        break;
    case EntityKind::Circle:
        if (const CircleData* d = circle(h)) {
            return &d->props;
        }
        break;
    case EntityKind::Arc:
        if (const ArcData* d = arc(h)) {
            return &d->props;
        }
        break;
    case EntityKind::Polyline:
        if (const PolylineData* d = polyline(h)) {
            return &d->props;
        }
        break;
    case EntityKind::Spline:
        if (const SplineData* d = spline(h)) {
            return &d->props;
        }
        break;
    case EntityKind::AttDef:
        if (const AttDefData* d = attdef(h)) {
            return &d->text.props;
        }
        break;
    case EntityKind::Text:
        if (const TextData* d = text(h)) {
            return &d->props;
        }
        break;
    case EntityKind::Dimension:
        if (const DimData* d = dimension(h)) {
            return &d->props;
        }
        break;
    case EntityKind::Leader:
        if (const LeaderData* d = leader(h)) {
            return &d->props;
        }
        break;
    case EntityKind::MText:
        if (const MTextData* d = mtext(h)) {
            return &d->props;
        }
        break;
    case EntityKind::MLeader:
        if (const MLeaderData* d = mleader(h)) {
            return &d->props;
        }
        break;
    case EntityKind::Insert:
        if (const InsertData* d = insert(h)) {
            return &d->props;
        }
        break;
    case EntityKind::Hatch:
        if (const HatchData* d = hatch(h)) {
            return &d->props;
        }
        break;
    case EntityKind::Fcf:
        if (const FcfData* d = fcf(h)) {
            return &d->props;
        }
        break;
    case EntityKind::Datum:
        if (const DatumData* d = datum(h)) {
            return &d->props;
        }
        break;
    case EntityKind::Image:
        if (const ImageData* d = image(h)) {
            return &d->props;
        }
        break;
    case EntityKind::Table:
        if (const TableData* d = table(h)) {
            return &d->props;
        }
        break;
    }
    return nullptr;
}

bool GeometryStore::set_props(EntityHandle h, const EntityProps& p) noexcept {
    switch (h.kind) {
    case EntityKind::Point:
        if (PointData* d = points_.get(h.index, h.generation)) {
            d->props = p;
            return true;
        }
        break;
    case EntityKind::Xline:
        if (XlineData* d = xlines_.get(h.index, h.generation)) {
            d->props = p;
            return true;
        }
        break;
    case EntityKind::Ellipse:
        if (EllipseData* d = ellipses_.get(h.index, h.generation)) {
            d->props = p;
            return true;
        }
        break;
    case EntityKind::Line:
        if (LineData* d = lines_.get(h.index, h.generation)) {
            d->props = p;
            return true;
        }
        break;
    case EntityKind::Circle:
        if (CircleData* d = circles_.get(h.index, h.generation)) {
            d->props = p;
            return true;
        }
        break;
    case EntityKind::Arc:
        if (ArcData* d = arcs_.get(h.index, h.generation)) {
            d->props = p;
            return true;
        }
        break;
    case EntityKind::Polyline:
        if (PolylineData* d = polylines_.get(h.index, h.generation)) {
            d->props = p;
            return true;
        }
        break;
    case EntityKind::Spline:
        if (SplineData* d = splines_.get(h.index, h.generation)) {
            d->props = p;
            return true;
        }
        break;
    case EntityKind::AttDef:
        if (AttDefData* d = attdefs_.get(h.index, h.generation)) {
            d->text.props = p;
            return true;
        }
        break;
    case EntityKind::Text:
        if (TextData* d = texts_.get(h.index, h.generation)) {
            d->props = p;
            return true;
        }
        break;
    case EntityKind::Dimension:
        if (DimData* d = dims_.get(h.index, h.generation)) {
            d->props = p;
            return true;
        }
        break;
    case EntityKind::Leader:
        if (LeaderData* d = leaders_.get(h.index, h.generation)) {
            d->props = p;
            return true;
        }
        break;
    case EntityKind::MText:
        if (MTextData* d = mtexts_.get(h.index, h.generation)) {
            d->props = p;
            return true;
        }
        break;
    case EntityKind::MLeader:
        if (MLeaderData* d = mleaders_.get(h.index, h.generation)) {
            d->props = p;
            return true;
        }
        break;
    case EntityKind::Insert:
        if (InsertData* d = inserts_.get(h.index, h.generation)) {
            d->props = p;
            return true;
        }
        break;
    case EntityKind::Hatch:
        if (HatchData* d = hatches_.get(h.index, h.generation)) {
            d->props = p;
            return true;
        }
        break;
    case EntityKind::Fcf:
        if (FcfData* d = fcfs_.get(h.index, h.generation)) {
            d->props = p;
            return true;
        }
        break;
    case EntityKind::Datum:
        if (DatumData* d = datums_.get(h.index, h.generation)) {
            d->props = p;
            return true;
        }
        break;
    case EntityKind::Image:
        if (ImageData* d = images_.get(h.index, h.generation)) {
            d->props = p;
            return true;
        }
        break;
    case EntityKind::Table:
        if (TableData* d = tables_.get(h.index, h.generation)) {
            d->props = p;
            return true;
        }
        break;
    }
    return false;
}

// --- block-definition table ------------------------------------------------

const BlockDef* GeometryStore::block(std::uint16_t index) const noexcept {
    return index < blocks_.size() ? &blocks_[index] : nullptr;
}
std::uint16_t GeometryStore::add_block(const BlockDef& def) {
    for (std::size_t i = 0; i < blocks_.size(); ++i) {
        if (blocks_[i].name == def.name) {
            return static_cast<std::uint16_t>(i);
        }
    }
    blocks_.push_back(def);
    return static_cast<std::uint16_t>(blocks_.size() - 1);
}
void GeometryStore::set_block_table(std::vector<BlockDef> blocks) { blocks_ = std::move(blocks); }
bool GeometryStore::redefine_block(std::uint16_t index, const BlockDef& def) {
    if (index >= blocks_.size()) {
        return false;
    }
    blocks_[index] = def;
    return true;
}

// --- font table ------------------------------------------------------------

std::uint16_t GeometryStore::add_font(std::string_view name) {
    if (name.empty()) {
        return 0; // the built-in stroke font
    }
    for (std::size_t i = 0; i < fonts_.size(); ++i) {
        if (fonts_[i] == name) {
            return static_cast<std::uint16_t>(i);
        }
    }
    fonts_.emplace_back(name);
    return static_cast<std::uint16_t>(fonts_.size() - 1);
}
void GeometryStore::set_font_table(std::vector<std::string> fonts) {
    fonts_ = std::move(fonts);
    if (fonts_.empty()) {
        fonts_.emplace_back(); // index 0 is always the stroke font
    }
    fonts_[0].clear();
}

// --- layer table -----------------------------------------------------------

const Layer* GeometryStore::layer(std::uint16_t index) const noexcept {
    return index < layers_.size() ? &layers_[index] : nullptr;
}

void GeometryStore::set_current_layer(std::uint16_t index) noexcept {
    if (index < layers_.size()) {
        current_layer_ = index;
    }
}

void GeometryStore::set_layer_table(std::vector<Layer> layers, std::uint16_t current) {
    layers_ = std::move(layers);
    if (layers_.empty()) {
        layers_.push_back(Layer{"0"});
    }
    layers_[0].name = "0"; // index 0 is always layer "0"
    current_layer_ = current < layers_.size() ? current : 0;
}

std::uint16_t GeometryStore::add_layer(const Layer& layer) {
    for (std::size_t i = 0; i < layers_.size(); ++i) {
        if (layers_[i].name == layer.name) {
            return static_cast<std::uint16_t>(i); // names are unique
        }
    }
    layers_.push_back(layer);
    return static_cast<std::uint16_t>(layers_.size() - 1);
}

bool GeometryStore::set_layer(std::uint16_t index, const Layer& layer) {
    if (index >= layers_.size()) {
        return false;
    }
    Layer updated = layer;
    if (index == 0) {
        updated.name = "0"; // layer 0 cannot be renamed
    }
    layers_[index] = updated;
    return true;
}

namespace {
template <class Arena, class Fn>
void for_each_live_mut(Arena& arena, Fn&& fn) {
    for (std::uint32_t i = 0; i < arena.slot_count(); ++i) {
        if (arena.alive(i)) {
            fn(*arena.get(i, arena.generations()[i]));
        }
    }
}
template <class Arena, class Fn>
void for_each_live_const(const Arena& arena, Fn&& fn) {
    for (std::uint32_t i = 0; i < arena.slot_count(); ++i) {
        if (arena.alive(i)) {
            fn(arena.data()[i]);
        }
    }
}
} // namespace

bool GeometryStore::layer_in_use(std::uint16_t index) const noexcept {
    bool used = false;
    const auto check = [&](const auto& data) {
        if (data.props.layer == index) {
            used = true;
        }
    };
    for_each_live_const(points_, check);
    for_each_live_const(lines_, check);
    for_each_live_const(circles_, check);
    for_each_live_const(arcs_, check);
    for_each_live_const(polylines_, check);
    for_each_live_const(splines_, check);
    for_each_live_const(texts_, check);
    for_each_live_const(dims_, check);
    for_each_live_const(leaders_, check);
    for_each_live_const(mtexts_, check);
    for_each_live_const(mleaders_, check);
    for_each_live_const(hatches_, check);
    for_each_live_const(fcfs_, check);
    for_each_live_const(datums_, check);
    for_each_live_const(images_, check);
    for_each_live_const(tables_, check);
    for_each_live_const(inserts_, check);
    for_each_live_const(xlines_, check);
    for_each_live_const(ellipses_, check);
    return used;
}

void GeometryStore::shift_layer_refs_after_removal(std::uint16_t removed) noexcept {
    const auto fix = [&](auto& data) {
        if (data.props.layer > removed) {
            --data.props.layer;
        }
    };
    for_each_live_mut(points_, fix);
    for_each_live_mut(lines_, fix);
    for_each_live_mut(circles_, fix);
    for_each_live_mut(arcs_, fix);
    for_each_live_mut(polylines_, fix);
    for_each_live_mut(splines_, fix);
    for_each_live_mut(texts_, fix);
    for_each_live_mut(dims_, fix);
    for_each_live_mut(leaders_, fix);
    for_each_live_mut(mtexts_, fix);
    for_each_live_mut(mleaders_, fix);
    for_each_live_mut(hatches_, fix);
    for_each_live_mut(fcfs_, fix);
    for_each_live_mut(datums_, fix);
    for_each_live_mut(images_, fix);
    for_each_live_mut(tables_, fix);
    for_each_live_mut(inserts_, fix);
    for_each_live_mut(xlines_, fix);
    for_each_live_mut(ellipses_, fix);
}

bool GeometryStore::remove_layer(std::uint16_t index) {
    if (index == 0 || index >= layers_.size() || index == current_layer_ || layer_in_use(index)) {
        return false; // AutoCAD: can't delete layer 0, current, or a non-empty layer
    }
    layers_.erase(layers_.begin() + index);
    shift_layer_refs_after_removal(index);
    if (current_layer_ > index) {
        --current_layer_;
    }
    return true;
}


// --- PURGE support: remove an unused table entry and reindex every reference ----------

bool GeometryStore::dimstyle_in_use(std::uint16_t index) const noexcept {
    bool used = false;
    const auto check = [&](const auto& d) {
        if (d.style == index) {
            used = true;
        }
    };
    for_each_live_const(dims_, check);
    for_each_live_const(leaders_, check);
    for_each_live_const(mleaders_, check);
    for_each_live_const(fcfs_, check);
    for_each_live_const(datums_, check);
    return used;
}

bool GeometryStore::remove_dimstyle(std::uint16_t index) {
    if (index == 0 || index >= dimstyles_.size() || dimstyle_in_use(index)) {
        return false; // style 0 is the default and stays; a used style stays
    }
    dimstyles_.erase(dimstyles_.begin() + index);
    const auto fix = [&](auto& d) {
        if (d.style > index) {
            --d.style;
        }
    };
    for_each_live_mut(dims_, fix);
    for_each_live_mut(leaders_, fix);
    for_each_live_mut(mleaders_, fix);
    for_each_live_mut(fcfs_, fix);
    for_each_live_mut(datums_, fix);
    return true;
}

bool GeometryStore::table_style_in_use(std::uint16_t index) const noexcept {
    bool used = false;
    for_each_live_const(tables_, [&](const TableData& t) {
        if (t.style == index) {
            used = true;
        }
    });
    return used;
}

bool GeometryStore::remove_table_style(std::uint16_t index) {
    if (index == 0 || index >= table_styles_.size() || table_style_in_use(index)) {
        return false;
    }
    table_styles_.erase(table_styles_.begin() + index);
    for_each_live_mut(tables_, [&](TableData& t) {
        if (t.style > index) {
            --t.style;
        }
    });
    return true;
}

bool GeometryStore::block_in_use(std::uint16_t index) const noexcept {
    bool used = false;
    for_each_live_const(inserts_, [&](const InsertData& i) {
        if (i.block == index) {
            used = true;
        }
    });
    for (const BlockDef& b : blocks_) {
        for (const InsertData& i : b.content.inserts) {
            if (i.block == index) {
                used = true; // referenced from inside another block
            }
        }
    }
    return used;
}

bool GeometryStore::remove_block(std::uint16_t index) {
    if (index >= blocks_.size() || block_in_use(index)) {
        return false;
    }
    blocks_.erase(blocks_.begin() + index);
    for_each_live_mut(inserts_, [&](InsertData& i) {
        if (i.block > index) {
            --i.block;
        }
    });
    for (BlockDef& b : blocks_) {
        for (InsertData& i : b.content.inserts) {
            if (i.block > index) {
                --i.block;
            }
        }
    }
    return true;
}

bool GeometryStore::image_def_in_use(std::uint16_t index) const noexcept {
    bool used = false;
    for_each_live_const(images_, [&](const ImageData& im) {
        if (im.def == index) {
            used = true;
        }
    });
    return used;
}

bool GeometryStore::remove_image_def(std::uint16_t index) {
    if (index >= image_defs_.size() || image_def_in_use(index)) {
        return false;
    }
    image_defs_.erase(image_defs_.begin() + index);
    for_each_live_mut(images_, [&](ImageData& im) {
        if (im.def > index) {
            --im.def;
        }
    });
    return true;
}

bool GeometryStore::text_style_in_use(std::uint16_t index) const noexcept {
    bool used = false;
    for_each_live_const(texts_, [&](const TextData& t) {
        if (t.style == index) {
            used = true;
        }
    });
    return used;
}

bool GeometryStore::remove_text_style(std::uint16_t index) {
    if (index == 0 || index >= text_styles_.size() || index == current_text_style_ ||
        text_style_in_use(index)) {
        return false;
    }
    text_styles_.erase(text_styles_.begin() + index);
    for_each_live_mut(texts_, [&](TextData& t) {
        if (t.style > index) {
            --t.style;
        }
    });
    if (current_text_style_ > index) {
        --current_text_style_;
    }
    return true;
}

} // namespace musacad::core
