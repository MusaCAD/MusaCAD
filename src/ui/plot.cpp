// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/ui/plot.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <vector>

#include <QBrush>
#include <QColor>
#include <QFileInfo>
#include <QLineF>
#include <QPageSize>
#include <QPaintDevice>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QImage>
#include <QRectF>
#include <QTransform>
#include <QSizeF>
#include <QString>

#include <map>
#include <memory>

#include "musacad/core/geometry_store.hpp"
#include "musacad/core/image.hpp"
#include "musacad/core/image_decoder.hpp"
#include "musacad/core/render_snapshot.hpp"

namespace musacad::ui {

namespace {
constexpr double kMmPerInch = 25.4;
constexpr int kPlotDpi = 300; ///< PDF device resolution; the tolerance rule assumes it

double luminance(core::Rgb c) {
    return 0.30 * c.r + 0.59 * c.g + 0.11 * c.b;
}

// Standard sheets as (long edge, short edge) millimetres. THE paper table -- the PLOT
// dialog and the CLI's --paper both read it, so a sheet cannot mean two things.
constexpr std::array<PaperSize, 7> kPapers{{
    {"ISO A4", 297.0, 210.0},
    {"ISO A3", 420.0, 297.0},
    {"ISO A2", 594.0, 420.0},
    {"ISO A1", 841.0, 594.0},
    {"ISO A0", 1189.0, 841.0},
    {"ANSI A (Letter)", 279.4, 215.9},
    {"ANSI B (Tabloid)", 431.8, 279.4},
}};

std::string lower(std::string_view s) {
    std::string r(s);
    for (char& c : r) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return r;
}
} // namespace

std::span<const PaperSize> standard_papers() {
    return {kPapers.data(), kPapers.size()};
}

bool resolve_paper(std::string_view name, bool landscape, double& w_mm, double& h_mm) {
    const std::string want = lower(name);
    if (want.empty()) {
        return false;
    }
    for (const PaperSize& p : kPapers) {
        const std::string full = lower(p.name);
        // Exact ("iso a4"), or the short form users actually type: the trailing token of
        // an ISO name ("a4"), or the parenthesised alias of an ANSI one ("letter").
        bool hit = full == want;
        if (!hit) {
            const std::size_t sp = full.rfind(' ');
            hit = sp != std::string::npos && full.substr(sp + 1) == want;
        }
        if (!hit) {
            const std::size_t op = full.find('(');
            const std::size_t cp = full.find(')');
            hit = op != std::string::npos && cp > op && full.substr(op + 1, cp - op - 1) == want;
        }
        if (hit) {
            w_mm = landscape ? p.long_mm : p.short_mm;
            h_mm = landscape ? p.short_mm : p.long_mm;
            return true;
        }
    }
    return false;
}

double plot_tolerance(core::Vec2 amin, core::Vec2 amax, double paper_w_mm, double paper_h_mm) {
    const double area_diag = std::max(core::length(amax - amin), 1e-9);
    const double paper_diag_px = std::hypot(paper_w_mm, paper_h_mm) / kMmPerInch * kPlotDpi;
    return std::max(area_diag / paper_diag_px * 0.3, 1e-9);
}

bool write_plot_pdf(const std::string& path, const core::RenderSnapshot& snap,
                    const PlotSpec& spec, core::Vec2 amin, core::Vec2 amax, std::string& error,
                    const ImageSource* images) {
    const QString qpath = QString::fromStdString(path);
    {
        QPdfWriter w(qpath);
        w.setPageSize(QPageSize(QSizeF(spec.paper_w_mm, spec.paper_h_mm), QPageSize::Millimeter));
        w.setResolution(kPlotDpi);
        paint_plot(w, snap, spec, amin, amax, images);
    } // the writer must be destroyed before the file is complete on disk
    const QFileInfo fi(qpath);
    if (!fi.exists() || fi.size() == 0) {
        error = "could not write the PDF: " + path;
        return false;
    }
    return true;
}

namespace {
/// Decodes on first use and caches by definition index, so N placements of one logo
/// decode once. Deliberately NOT in the snapshot: pixels must never ride the triple
/// buffer (see core::ImageInstance).
class StoreImageSource final : public ImageSource {
public:
    StoreImageSource(const core::GeometryStore& store, const core::IImageDecoder* decoder,
                     std::string_view drawing_dir)
        : store_(store), decoder_(decoder), dir_(drawing_dir) {}

    bool pixels(std::uint16_t def, std::uint32_t& width, std::uint32_t& height,
                std::vector<std::uint8_t>& rgba) const override {
        if (decoder_ == nullptr) {
            return false; // no decoder injected: geometry still plots, pixels do not
        }
        if (const auto it = cache_.find(def); it != cache_.end()) {
            width = it->second.width;
            height = it->second.height;
            rgba = it->second.rgba;
            return it->second.valid();
        }
        core::DecodedImage img;
        if (const core::ImageDef* d = store_.image_def(def)) {
            if (!d->bytes.empty()) {
                img = decoder_->decode_bytes(d->bytes); // embedded payload wins
            } else if (std::string full; core::resolve_image_path(dir_, d->source, full)) {
                img = decoder_->decode_file(full);
            }
        }
        const bool ok = img.valid();
        width = img.width;
        height = img.height;
        rgba = img.rgba;
        cache_.emplace(def, std::move(img));
        return ok;
    }

private:
    const core::GeometryStore& store_;
    const core::IImageDecoder* decoder_;
    std::string dir_;
    mutable std::map<std::uint16_t, core::DecodedImage> cache_;
};
} // namespace

std::unique_ptr<ImageSource> make_store_image_source(const core::GeometryStore& store,
                                                     const core::IImageDecoder* decoder,
                                                     std::string_view drawing_dir) {
    return std::make_unique<StoreImageSource>(store, decoder, drawing_dir);
}

core::Rgb plot_color(core::Rgb resolved, PlotSpec::Style style, bool computed_color) {
    core::Rgb c = resolved;
    // White-on-dark-screen geometry must show on white paper: near-white -> black. A
    // computed colour (a gradient band) is what it says and stays.
    if (!computed_color && c.r >= 240 && c.g >= 240 && c.b >= 240) {
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
                core::Vec2 amin, core::Vec2 amax, const ImageSource* images) {
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

    // Raster images FIRST, so vector geometry always lands on top -- an image is a
    // backdrop (a logo, a shaded pictorial), and it must never hide a dimension.
    // Rasterised at the DEVICE's resolution, so a 300 DPI PDF gets 300 DPI pixels.
    if (images != nullptr) {
        std::uint32_t iw = 0;
        std::uint32_t ih = 0;
        std::vector<std::uint8_t> rgba;
        for (const core::ImageInstance& inst : snap.images) {
            if (!images->pixels(inst.def, iw, ih, rgba) || iw == 0 || ih == 0) {
                continue; // a missing external file omits THAT image, not the plot
            }
            const QImage img(rgba.data(), static_cast<int>(iw), static_cast<int>(ih),
                             static_cast<int>(iw) * 4, QImage::Format_RGBA8888);
            // The instance's UV rect selects the (clipped) source region; the quad gives
            // the destination. Both come from the shared resolver, so the plot shows
            // exactly the region pick and bounds agree on.
            const QRectF src(inst.uv[0] * iw, inst.uv[1] * ih,
                             (inst.uv[2] - inst.uv[0]) * iw, (inst.uv[3] - inst.uv[1]) * ih);
            // Map the quad through the world->device transform. The quad is a rotated
            // rectangle, so an affine transform reproduces it exactly.
            const QPointF p0 = to_dev(inst.quad[0]); // bottom-left
            const QPointF p1 = to_dev(inst.quad[1]); // bottom-right
            const QPointF p3 = to_dev(inst.quad[3]); // top-left
            // Device space is y-down, so the image's TOP-left maps to quad corner 3.
            QTransform t(( p1.x() - p0.x()) / src.width(), (p1.y() - p0.y()) / src.width(),
                         ( p0.x() - p3.x()) / src.height(), (p0.y() - p3.y()) / src.height(),
                         p3.x(), p3.y());
            p.save();
            if (!inst.clip_world.empty()) {
                // A polygonal clip: the boundary as a device-space clip path.
                QPolygonF poly;
                for (const core::Vec2& w : inst.clip_world) {
                    poly << to_dev(w);
                }
                QPainterPath clip;
                clip.addPolygon(poly);
                clip.closeSubpath();
                p.setClipPath(clip);
            }
            p.setTransform(t, true);
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            p.drawImage(QRectF(0, 0, src.width(), src.height()), img, src);
            p.restore();
        }
    }

    // Filled triangles (outline-font glyphs, arrowheads, hatches): one path per colour
    // batch, filled in one go. Triangles drawn one at a time leave antialiasing hairlines
    // along every shared edge (a gradient's bands, a solid hatch's fan); one path with the
    // winding rule rasterises their union, seamlessly.
    const auto fill_triangles = [&](const core::Vec2* verts, std::size_t count, QColor color) {
        QPainterPath path;
        path.setFillRule(Qt::WindingFill);
        for (std::size_t i = 0; i + 2 < count; i += 3) {
            path.moveTo(to_dev(verts[i]));
            path.lineTo(to_dev(verts[i + 1]));
            path.lineTo(to_dev(verts[i + 2]));
            path.closeSubpath();
        }
        p.fillPath(path, QBrush(color));
    };
    for (const core::ColorBatch& b : snap.fill_batches) {
        const core::Rgb c = plot_color(b.color, spec.style, b.computed_color);
        fill_triangles(snap.fill_vertices.data() + b.first, b.count, QColor(c.r, c.g, c.b));
    }
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::NoBrush);

    // Line segments: one pen per (colour, lineweight) batch. Pen width is the entity's
    // real lineweight in paper millimetres -- independent of the drawing scale.
    // Geometry first, then the WIPEOUT masks in the paper colour, then text -- the
    // viewport's order, so a note on a mask plots readable and the line work under the
    // mask does not.
    const auto draw_lines = [&](bool text_pass) {
        for (const core::ColorBatch& b : snap.line_batches) {
            if (b.is_text != text_pass) {
                continue;
            }
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
    };
    draw_lines(false);
    if (!snap.wipeout_vertices.empty()) {
        fill_triangles(snap.wipeout_vertices.data(), snap.wipeout_vertices.size(), QColor(Qt::white));
    }
    draw_lines(true);

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
