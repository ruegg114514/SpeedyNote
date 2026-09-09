#include "OcrStrokeRasterizer.h"

#include <QColor>
#include <QPainter>
#include <QPen>
#include <QPolygonF>

#include <cmath>

RasterStrip rasterizeStrokes(const QVector<VectorStroke>& strokes,
                             const QVector<int>& indices,
                             int targetHeightPx)
{
    RasterStrip out;
    if (indices.isEmpty())
        return out; // null image, identity transform

    // --- 1. Tight content rect from raw point positions (not the thickness-
    //        padded VectorStroke::boundingBox; QA Q4.1). --------------------
    bool any = false;
    qreal minX = 0, minY = 0, maxX = 0, maxY = 0;
    for (int idx : indices) {
        if (idx < 0 || idx >= strokes.size())
            continue;
        for (const auto& pt : strokes[idx].points) {
            const qreal x = pt.pos.x();
            const qreal y = pt.pos.y();
            if (!any) {
                minX = maxX = x;
                minY = maxY = y;
                any = true;
            } else {
                minX = qMin(minX, x);
                maxX = qMax(maxX, x);
                minY = qMin(minY, y);
                maxY = qMax(maxY, y);
            }
        }
    }
    if (!any)
        return out;

    const qreal contentW = maxX - minX;
    const qreal contentH = maxY - minY;

    // --- 2. Derive scale + image dimensions. ---------------------------------
    const int target = qMax(8, targetHeightPx);
    // Guard against degenerate (e.g. perfectly horizontal) lines whose height
    // is ~0; clamp the divisor and cap upscaling of tiny ink.
    const qreal effH = qMax(contentH, 1.0);
    const qreal kMaxScale = 200.0;
    const qreal scale = qMin(static_cast<qreal>(target) / effH, kMaxScale);

    const int padding = qMax(4, target / 8);
    int imgW = static_cast<int>(std::lround(contentW * scale)) + 2 * padding;
    const int imgH = target + 2 * padding;
    imgW = qBound(1 + 2 * padding, imgW, 8192);

    // --- 3. Render dark-on-white with a uniform pen (cosmetic attributes
    //        ignored so the result is stable across color/width edits). -------
    QImage img(imgW, imgH, QImage::Format_Grayscale8);
    img.fill(255);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal penW = qMax(2.0, target / 16.0);
    QPen pen(QColor(0, 0, 0));
    pen.setWidthF(penW);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);

    auto mapPt = [&](const QPointF& c) {
        return QPointF((c.x() - minX) * scale + padding,
                       (c.y() - minY) * scale + padding);
    };

    for (int idx : indices) {
        if (idx < 0 || idx >= strokes.size())
            continue;
        const VectorStroke& s = strokes[idx];
        if (s.points.isEmpty())
            continue;

        if (s.points.size() == 1) {
            // Dot: draw a filled disc of pen diameter.
            const QPointF c = mapPt(s.points[0].pos);
            p.setBrush(QColor(0, 0, 0));
            p.drawEllipse(c, penW / 2.0, penW / 2.0);
            p.setBrush(Qt::NoBrush);
            continue;
        }

        QPolygonF poly;
        poly.reserve(s.points.size());
        for (const auto& pt : s.points)
            poly << mapPt(pt.pos);
        p.drawPolyline(poly);
    }
    p.end();

    out.image = img;
    out.transform.originPage = QPointF(minX, minY);
    out.transform.scale = scale;
    out.transform.padding = padding;
    return out;
}
