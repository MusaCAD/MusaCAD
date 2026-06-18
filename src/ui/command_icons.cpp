// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#include "musacad/ui/command_icons.hpp"

#include <QColor>
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

} // namespace musacad::ui
