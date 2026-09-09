#pragma once

// ============================================================================
// OcrStrokeRasterizer - Normalized stroke -> QImage for raster OCR engines
// ============================================================================
// Part of the OCR Phase 4A raster pipeline.
//
// Renders a group of vector strokes into a clean, normalized monochrome image
// strip suitable for image-OCR backends (Apple Vision, PaddleOCR/ONNX). Because
// we control the rasterization end to end, the page<->image transform is exact
// and invertible (RasterTransform), so recognized geometry in image space can be
// mapped straight back to canvas coordinates.
//
// Normalization (Phase 4 QA Q4.1): fixed target strip height, uniform stroke
// thickness (pen pressure/width ignored), fixed padding, dark-on-white. This
// both maximizes recognition accuracy and makes the rendered result independent
// of cosmetic pen attributes (color/width), so they can be excluded from a line
// signature cache key.
// ============================================================================

#include "../strokes/VectorStroke.h"

#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QVector>

/**
 * @brief Exact, invertible affine mapping from canvas space to strip-image space.
 *
 * Forward (canvas -> image, used internally while rendering):
 *   img = (canvas - originPage) * scale + padding
 * Inverse (image -> canvas, exposed for mapping recognized geometry back):
 *   canvas = originPage + (img - padding) / scale
 *
 * Axis-aligned only (no deskew; Phase 4 QA Q3.4).
 */
struct RasterTransform {
    QPointF originPage;   ///< Canvas-space top-left of the rendered content rect
    qreal   scale = 1.0;  ///< Image pixels per canvas unit
    int     padding = 0;  ///< Image-pixel padding around the ink

    QPointF imageToCanvas(const QPointF& imgPt) const {
        return QPointF(originPage.x() + (imgPt.x() - padding) / scale,
                       originPage.y() + (imgPt.y() - padding) / scale);
    }

    QRectF imageToCanvas(const QRectF& imgRect) const {
        const QPointF tl = imageToCanvas(imgRect.topLeft());
        return QRectF(tl, QSizeF(imgRect.width() / scale, imgRect.height() / scale));
    }
};

/// A rendered strip plus the transform needed to map results back to canvas.
struct RasterStrip {
    QImage image;
    RasterTransform transform;
};

/**
 * @brief Render the indexed strokes into a normalized monochrome strip.
 *
 * @param strokes        The full stroke vector.
 * @param indices        Indices into @p strokes to render (the line/cell group).
 * @param targetHeightPx Target ink height in pixels (excludes padding).
 * @return A RasterStrip; image is null when @p indices yields no drawable points.
 */
RasterStrip rasterizeStrokes(const QVector<VectorStroke>& strokes,
                             const QVector<int>& indices,
                             int targetHeightPx);
