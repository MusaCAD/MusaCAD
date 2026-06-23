// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/ui/command_icons.hpp"

#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRectF>

namespace musacad::ui {

namespace {

constexpr int kSize = 24;

void draw_glyph(QPainter& p, const QString& kind) {
    const QRectF r(3, 3, kSize - 6, kSize - 6); // inner box
    const double x0 = r.left();
    const double y0 = r.top();
    const double x1 = r.right();
    const double y1 = r.bottom();
    const double cx = r.center().x();
    const double cy = r.center().y();

    if (kind == "line") {
        p.drawLine(QPointF(x0, y1), QPointF(x1, y0));
    } else if (kind == "polyline") {
        p.drawPolyline(QPolygonF({{x0, y1}, {cx - 2, y0}, {cx + 2, y1}, {x1, y0}}));
    } else if (kind == "circle") {
        p.drawEllipse(r);
    } else if (kind == "arc") {
        p.drawArc(r, 20 * 16, 140 * 16);
    } else if (kind == "rectangle") {
        p.drawRect(r);
    } else if (kind == "erase") {
        p.drawLine(QPointF(x0, y0), QPointF(x1, y1));
        p.drawLine(QPointF(x0, y1), QPointF(x1, y0));
    } else if (kind == "undo" || kind == "redo") {
        const bool fwd = (kind == "redo");
        p.drawArc(r, (fwd ? 30 : 30) * 16, 200 * 16);
        const double ax = fwd ? x1 : x0;
        p.drawLine(QPointF(ax, y0 + 1), QPointF(ax + (fwd ? -4 : 4), y0 - 2));
        p.drawLine(QPointF(ax, y0 + 1), QPointF(ax + (fwd ? -4 : 4), y0 + 5));
    } else if (kind == "new") {
        p.drawRect(QRectF(x0 + 2, y0, x1 - x0 - 4, y1 - y0));
    } else if (kind == "open") {
        p.drawRect(QRectF(x0, cy - 2, x1 - x0, y1 - cy + 2));
        p.drawLine(QPointF(x0, cy - 2), QPointF(cx, y0));
    } else if (kind == "save") {
        p.drawRect(r);
        p.drawRect(QRectF(cx - 3, y1 - 6, 6, 6));
    } else if (kind == "print") {
        p.drawRect(QRectF(x0, cy - 2, x1 - x0, 8));
        p.drawRect(QRectF(x0 + 3, y0, x1 - x0 - 6, cy - 2 - y0));
    } else if (kind == "zoom") {
        const QRectF lens(x0, y0, (x1 - x0) * 0.7, (y1 - y0) * 0.7);
        p.drawEllipse(lens);
        p.drawLine(lens.bottomRight(), QPointF(x1, y1));
    } else if (kind == "move") {
        p.drawLine(QPointF(cx, y0), QPointF(cx, y1));
        p.drawLine(QPointF(x0, cy), QPointF(x1, cy));
    } else if (kind == "copy") {
        p.drawRect(QRectF(x0, y0, (x1 - x0) * 0.65, (y1 - y0) * 0.65));
        p.drawRect(QRectF(cx - 2, cy - 2, (x1 - x0) * 0.65, (y1 - y0) * 0.65));
    } else if (kind == "offset") {
        p.drawLine(QPointF(x0, y0), QPointF(x1, y0));
        p.drawLine(QPointF(x0, cy), QPointF(x1, cy));
    } else if (kind == "trim") {
        p.drawLine(QPointF(x0, y1), QPointF(x1, y0));
        p.drawEllipse(QPointF(x0 + 2, y1 - 2), 2.0, 2.0);
        p.drawEllipse(QPointF(x0 + 2, y1 - 6), 2.0, 2.0);
    } else if (kind == "join") {
        // Two segments meeting at a highlighted junction (chain joined into one).
        p.drawLine(QPointF(x0, y1), QPointF(cx, cy));
        p.drawLine(QPointF(cx, cy), QPointF(x1, y0));
        p.drawEllipse(QPointF(cx, cy), 2.2, 2.2);
    } else if (kind == "layers") {
        for (int i = 0; i < 3; ++i) {
            const double yy = y0 + i * 4.0;
            p.drawPolygon(QPolygonF({{cx, yy}, {x1, yy + 2}, {cx, yy + 4}, {x0, yy + 2}}));
        }
    } else if (kind == "text") {
        QFont f = p.font();
        f.setBold(true);
        f.setPixelSize(kSize - 8);
        p.setFont(f);
        p.drawText(QRectF(0, 0, kSize, kSize), Qt::AlignCenter, "A");
    } else if (kind == "dim") {
        p.drawLine(QPointF(x0, cy), QPointF(x1, cy));
        p.drawLine(QPointF(x0, y0), QPointF(x0, y1));
        p.drawLine(QPointF(x1, y0), QPointF(x1, y1));
    } else {
        p.drawRect(r); // generic placeholder
    }
}

} // namespace

QIcon make_icon(const QString& kind) {
    QPixmap pix(kSize, kSize);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(0xd0, 0xd0, 0xd0));
    pen.setWidthF(1.6);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    draw_glyph(p, kind);
    p.end();
    return QIcon(pix);
}

QIcon ribbon_icon(const QString& asset_path) {
    if (asset_path.isEmpty()) {
        return make_icon(QString()); // generic placeholder square
    }
    // Cache by asset path -- avoids re-reading + re-rasterising the SVG per button/frame.
    static QHash<QString, QIcon> cache;
    const auto hit = cache.constFind(asset_path);
    if (hit != cache.constEnd()) {
        return hit.value();
    }
    // "assets/ribbon/line.svg" -> the compiled Qt resource ":/ribbon/line.svg".
    QString res = asset_path;
    const int slash = res.indexOf(QLatin1Char('/'));
    if (slash >= 0) {
        res = QStringLiteral(":/") + res.mid(slash + 1);
    }
    QIcon icon;
    QFile f(res);
    if (f.open(QIODevice::ReadOnly)) {
        QByteArray bytes = f.readAll();
        // The SVGs are authored with stroke="currentColor"; QIcon has no colour context,
        // so bake the theme's icon grey in here (one theme today; future themes re-bake).
        bytes.replace("currentColor", "#d0d0d0");
        QBuffer buf(&bytes);
        buf.open(QIODevice::ReadOnly);
        QImageReader reader(&buf, "svg"); // the qsvg image plugin (same as the branding logo)
        // Rasterise generously (64px) so QIcon stays crisp when scaled to 24px (1x) or
        // 48px (2x HiDPI). The source is vector, so this is a clean downscale either way.
        reader.setScaledSize(QSize(64, 64));
        const QImage img = reader.read();
        if (!img.isNull()) {
            icon = QIcon(QPixmap::fromImage(img));
        }
    }
    if (icon.isNull()) {
        icon = make_icon(QString()); // missing/invalid asset -> graceful placeholder
    }
    cache.insert(asset_path, icon);
    return icon;
}

} // namespace musacad::ui
