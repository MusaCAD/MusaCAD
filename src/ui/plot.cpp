// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#include "musacad/ui/plot.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <QBrush>
#include <QColor>
#include <QLineF>
#include <QPaintDevice>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>

#include "musacad/core/render_snapshot.hpp"

namespace musacad::ui {

namespace {
constexpr double kMmPerInch = 25.4;

double luminance(core::Rgb c) {
    return 0.30 * c.r + 0.59 * c.g + 0.11 * c.b;
}
} // namespace

core::Rgb plot_color(core::Rgb resolved, PlotSpec::Style style) {
    core::Rgb c = resolved;
    // White-on-dark-screen geometry must show on white paper: near-white -> black.
    if (c.r >= 240 && c.g >= 240 && c.b >= 240) {
        c = {0, 0, 0};
    }
    switch (style) {
    case PlotSpec::Style::None:
        return c;
    case PlotSpec::Style::Monochrome:
        return {0, 0, 0};
    case PlotSpec::Style::Grayscale: {
        const auto g = static_cast<std::uint8_t>(std::clamp(luminance(c), 0.0, 255.0));
        return {g, g, g};
    }
    }
    return c;
}

void paint_plot(QPaintDevice& device, const core::RenderSnapshot& snap, const PlotSpec& spec,
                core::Vec2 amin, core::Vec2 amax) {
    const double dev_w = device.width();
    const double dev_h = device.height();
    const double dpx = device.logicalDpiX() > 0 ? device.logicalDpiX() : 96.0;
    const double dpy = device.logicalDpiY() > 0 ? device.logicalDpiY() : 96.0;
    if (dev_w <= 0.0 || dev_h <= 0.0) {
        return;
    }

    // World area to plot (guard against a zero-size area).
    double aw = amax.x - amin.x;
    double ah = amax.y - amin.y;
    if (!(aw > 0.0)) {
        aw = 1.0;
    }
    if (!(ah > 0.0)) {
        ah = 1.0;
    }

    // Scale: millimetres of paper per drawing unit.
    double mm_per_unit = 0.0;
    if (spec.fit) {
        const double paper_w_mm = dev_w / dpx * kMmPerInch;
        const double paper_h_mm = dev_h / dpy * kMmPerInch;
        mm_per_unit = std::min(paper_w_mm / aw, paper_h_mm / ah);
    } else {
        mm_per_unit = spec.scale_den != 0.0 ? spec.scale_num / spec.scale_den : 1.0;
    }
    if (!(mm_per_unit > 0.0)) {
        mm_per_unit = 1.0;
    }
    const double px_per_unit_x = mm_per_unit * dpx / kMmPerInch;
    const double px_per_unit_y = mm_per_unit * dpy / kMmPerInch;

    // Placement: centre the scaled drawing on the sheet, then apply the offset.
    const double scaled_w = aw * px_per_unit_x;
    const double scaled_h = ah * px_per_unit_y;
    const double off_x_px = spec.off_x_mm * dpx / kMmPerInch;
    const double off_y_px = spec.off_y_mm * dpy / kMmPerInch;
    const double ox = (spec.center ? (dev_w - scaled_w) * 0.5 : 0.0) + off_x_px;
    const double oy = (spec.center ? (dev_h - scaled_h) * 0.5 : 0.0) + off_y_px;

    // World -> device pixels (y-flip: world is y-up, the device is y-down).
    const auto to_dev = [&](const core::Vec2& w) -> QPointF {
        const double x = ox + (w.x - amin.x) * px_per_unit_x;
        const double y = oy + (ah - (w.y - amin.y)) * px_per_unit_y;
        return {x, y};
    };

    QPainter p(&device);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(0, 0, static_cast<int>(dev_w), static_cast<int>(dev_h), Qt::white);

    // Clip to the plotted area (AutoCAD Window/Display behaviour): geometry OUTSIDE the
    // area must not slash across the sheet. The area [amin,amax] maps to this device rect;
    // without the clip a small/stale window draws every off-window entity to the page edge.
    {
        const QPointF tl = to_dev({amin.x, amax.y}); // world top-left -> device
        const QPointF br = to_dev({amax.x, amin.y});
        p.setClipRect(QRectF(tl, br).normalized());
    }

    // Filled triangles (outline-font glyphs, arrowheads): one brush per colour batch.
    QPolygonF tri(3);
    for (const core::ColorBatch& b : snap.fill_batches) {
        const core::Rgb c = plot_color(b.color, spec.style);
        p.setPen(Qt::NoPen);
        p.setBrush(QBrush(QColor(c.r, c.g, c.b)));
        for (std::uint32_t i = 0; i + 2 < b.count; i += 3) {
            const std::uint32_t base = b.first + i;
            tri[0] = to_dev(snap.fill_vertices[base]);
            tri[1] = to_dev(snap.fill_vertices[base + 1]);
            tri[2] = to_dev(snap.fill_vertices[base + 2]);
            p.drawPolygon(tri);
        }
    }
    p.setBrush(Qt::NoBrush);

    // Line segments: one pen per (colour, lineweight) batch. Pen width is the entity's
    // real lineweight in paper millimetres -- independent of the drawing scale.
    for (const core::ColorBatch& b : snap.line_batches) {
        const core::Rgb c = plot_color(b.color, spec.style);
        double width_px = 0.0; // 0 = a cosmetic 1px hairline
        if (spec.plot_lineweights && snap.lineweight_display && b.lineweight > 0) {
            width_px = (static_cast<double>(b.lineweight) / 100.0) * dpx / kMmPerInch;
        }
        QPen pen(QColor(c.r, c.g, c.b));
        pen.setWidthF(width_px);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        // Segment-unit batches via the shared iterator (no mis-indexing -> no phantom lines).
        core::for_each_line_segment(snap, b, [&](const core::Vec2& a, const core::Vec2& c) {
            p.drawLine(QLineF(to_dev(a), to_dev(c)));
        });
    }

    // Standalone points: small filled dots.
    for (const core::ColorBatch& b : snap.point_batches) {
        const core::Rgb c = plot_color(b.color, spec.style);
        p.setPen(Qt::NoPen);
        p.setBrush(QBrush(QColor(c.r, c.g, c.b)));
        const double r = std::max(0.5, 0.3 * dpx / kMmPerInch); // ~0.3mm dot
        for (std::uint32_t i = 0; i < b.count; ++i) {
            p.drawEllipse(to_dev(snap.points[b.first + i]), r, r);
        }
    }
    p.end();
}

core::PageSetup to_page_setup(const PlotSpec& s, const std::string& name) {
    core::PageSetup ps;
    ps.name = name;
    ps.paper = s.paper;
    ps.target = s.target;
    ps.paper_w_mm = s.paper_w_mm;
    ps.paper_h_mm = s.paper_h_mm;
    ps.landscape = s.landscape;
    ps.area = static_cast<std::uint8_t>(s.area);
    ps.win_min = s.win_min;
    ps.win_max = s.win_max;
    ps.fit = s.fit;
    ps.scale_num = s.scale_num;
    ps.scale_den = s.scale_den;
    ps.center = s.center;
    ps.off_x_mm = s.off_x_mm;
    ps.off_y_mm = s.off_y_mm;
    ps.plot_lineweights = s.plot_lineweights;
    ps.style = static_cast<std::uint8_t>(s.style);
    return ps;
}

PlotSpec from_page_setup(const core::PageSetup& ps) {
    PlotSpec s;
    s.paper = ps.paper;
    s.target = ps.target;
    s.paper_w_mm = ps.paper_w_mm;
    s.paper_h_mm = ps.paper_h_mm;
    s.landscape = ps.landscape;
    s.area = static_cast<PlotSpec::Area>(std::min<std::uint8_t>(ps.area, 2));
    s.win_min = ps.win_min;
    s.win_max = ps.win_max;
    s.fit = ps.fit;
    s.scale_num = ps.scale_num;
    s.scale_den = ps.scale_den;
    s.center = ps.center;
    s.off_x_mm = ps.off_x_mm;
    s.off_y_mm = ps.off_y_mm;
    s.plot_lineweights = ps.plot_lineweights;
    s.style = static_cast<PlotSpec::Style>(std::min<std::uint8_t>(ps.style, 2));
    return s;
}

} // namespace musacad::ui
