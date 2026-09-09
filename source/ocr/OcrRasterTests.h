#pragma once

// ============================================================================
// OcrRasterTests - Unit tests for the Phase 4A raster OCR pipeline
// ============================================================================
// Header-only, runnable on any desktop OS before a real backend exists. Uses a
// StubRasterOcrEngine whose recognizeImage() returns a fixed string with
// evenly-spaced synthetic per-character boxes spanning the strip, so the whole
// pipeline (grouping, rasterization, line-signature cache, transform inverse,
// segment assembly, serialization) is exercised deterministically.
//
// Run with:  speedynote --test-ocr-raster   (debug, non-mobile builds only)
// ============================================================================

#include "engines/RasterOcrEngine.h"
#include "OcrStrokeRasterizer.h"
#include "OcrTextBlock.h"
#include "../strokes/VectorStroke.h"

#include <QDebug>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

#include <cmath>

namespace OcrRasterTests {

// ----------------------------------------------------------------------------
// Stub backend: deterministic recognizer, counts calls for cache testing.
// ----------------------------------------------------------------------------
class StubRasterOcrEngine : public RasterOcrEngine {
public:
    QString text = QStringLiteral("ab"); ///< text returned per strip
    bool emitCharBoxes = true;           ///< when false, return text only
    int recognizeCalls = 0;              ///< number of recognizeImage() calls

    QString engineId() const override { return QStringLiteral("stub_raster"); }
    bool isAvailable() const override { return true; }
    QStringList availableLanguages() const override { return {QStringLiteral("en-US")}; }

protected:
    ImageRecognition recognizeImage(const QImage& strip, const QString& /*lang*/) override
    {
        ++recognizeCalls;
        ImageRecognition rec;
        rec.text = text;
        if (emitCharBoxes && !text.isEmpty()) {
            const int n = text.length();
            const qreal w = static_cast<qreal>(strip.width()) / n;
            for (int i = 0; i < n; ++i)
                rec.charBoxesImage.append(QRectF(i * w, 0, w, strip.height()));
        }
        return rec;
    }
};

inline bool nearlyEqual(qreal a, qreal b, qreal eps = 0.5)
{
    return std::abs(a - b) <= eps;
}

inline VectorStroke makeLineStroke(const QString& id, qreal x0, qreal y0,
                                   qreal x1, qreal y1)
{
    VectorStroke s;
    s.id = id;
    s.baseThickness = 3.0;
    s.points.append({QPointF(x0, y0), 0.5});
    s.points.append({QPointF((x0 + x1) / 2.0, (y0 + y1) / 2.0), 0.5});
    s.points.append({QPointF(x1, y1), 0.5});
    s.updateBoundingBox();
    return s;
}

// ----------------------------------------------------------------------------
// Test: line grouping yields one result per visually separated line.
// ----------------------------------------------------------------------------
inline bool testGrouping()
{
    qDebug() << "=== Test: Grouping ===";
    StubRasterOcrEngine engine;
    engine.text = QStringLiteral("x");

    engine.addStrokes({
        makeLineStroke(QStringLiteral("l1"), 10, 10, 110, 12),
        makeLineStroke(QStringLiteral("l2"), 10, 200, 110, 202),
    });

    const auto results = engine.analyze();
    const bool ok = results.size() == 2;
    qDebug() << (ok ? "PASS" : "FAIL") << "- expected 2 line results, got" << results.size();
    return ok;
}

// ----------------------------------------------------------------------------
// Test: rasterizer normalization (golden-ish strip properties).
// ----------------------------------------------------------------------------
inline bool testRenderNormalization()
{
    qDebug() << "=== Test: Render Normalization ===";
    QVector<VectorStroke> strokes{makeLineStroke(QStringLiteral("s"), 0, 0, 100, 20)};
    const int target = 48;
    const RasterStrip strip = rasterizeStrokes(strokes, {0}, target);

    bool ok = !strip.image.isNull();
    ok = ok && strip.image.format() == QImage::Format_Grayscale8;

    const int padding = strip.transform.padding;
    ok = ok && strip.image.height() == target + 2 * padding;

    // Some ink must have been drawn (a pixel darker than mid-gray).
    bool foundInk = false;
    for (int y = 0; y < strip.image.height() && !foundInk; ++y)
        for (int x = 0; x < strip.image.width(); ++x)
            if (qGray(strip.image.pixel(x, y)) < 128) { foundInk = true; break; }
    ok = ok && foundInk;

    qDebug() << (ok ? "PASS" : "FAIL") << "- size" << strip.image.size()
             << "format ok / ink found:" << foundInk;
    return ok;
}

// ----------------------------------------------------------------------------
// Test: image->canvas transform round-trips a known point and rect.
// ----------------------------------------------------------------------------
inline bool testTransformRoundTrip()
{
    qDebug() << "=== Test: Transform Round-Trip ===";
    QVector<VectorStroke> strokes{makeLineStroke(QStringLiteral("s"), 30, 40, 230, 80)};
    const RasterStrip strip = rasterizeStrokes(strokes, {0}, 48);
    const RasterTransform& xf = strip.transform;

    // Forward-map a canvas point, then invert it.
    const QPointF canvasPt(130, 60);
    const QPointF imgPt((canvasPt.x() - xf.originPage.x()) * xf.scale + xf.padding,
                        (canvasPt.y() - xf.originPage.y()) * xf.scale + xf.padding);
    const QPointF back = xf.imageToCanvas(imgPt);

    bool ok = nearlyEqual(back.x(), canvasPt.x()) && nearlyEqual(back.y(), canvasPt.y());

    const QRectF imgRect(xf.padding, xf.padding, 50.0 * xf.scale, 20.0 * xf.scale);
    const QRectF canvasRect = xf.imageToCanvas(imgRect);
    ok = ok && nearlyEqual(canvasRect.x(), xf.originPage.x())
            && nearlyEqual(canvasRect.width(), 50.0)
            && nearlyEqual(canvasRect.height(), 20.0);

    qDebug() << (ok ? "PASS" : "FAIL") << "- point back" << back << "rect" << canvasRect;
    return ok;
}

// ----------------------------------------------------------------------------
// Test: line-signature cache hit on no-op, miss on edit, evict on empty.
// ----------------------------------------------------------------------------
inline bool testCacheHitEvict()
{
    qDebug() << "=== Test: Cache Hit / Evict ===";
    StubRasterOcrEngine engine;
    engine.text = QStringLiteral("hi");

    engine.addStrokes({makeLineStroke(QStringLiteral("a"), 10, 10, 110, 12)});
    engine.analyze();
    bool ok = engine.recognizeCalls == 1;

    engine.analyze(); // no change -> cache hit
    ok = ok && engine.recognizeCalls == 1;

    // Add a second stroke to the same line -> group signature changes.
    engine.addStrokes({makeLineStroke(QStringLiteral("b"), 120, 10, 220, 12)});
    engine.analyze();
    ok = ok && engine.recognizeCalls == 2;

    engine.analyze(); // no change -> cache hit again
    ok = ok && engine.recognizeCalls == 2;

    // Remove everything -> empty result, cache cleared.
    engine.removeStrokes({QStringLiteral("a"), QStringLiteral("b")});
    const auto empty = engine.analyze();
    ok = ok && empty.isEmpty();

    // Re-add the original single stroke -> must recompute (cache was cleared).
    engine.addStrokes({makeLineStroke(QStringLiteral("a"), 10, 10, 110, 12)});
    engine.analyze();
    ok = ok && engine.recognizeCalls == 3;

    qDebug() << (ok ? "PASS" : "FAIL") << "- recognizeCalls" << engine.recognizeCalls;
    return ok;
}

// ----------------------------------------------------------------------------
// Test: Latin words split on space; CJK emits one segment per glyph.
// ----------------------------------------------------------------------------
inline bool testSegmentAssembly()
{
    qDebug() << "=== Test: Segment Assembly ===";
    bool ok = true;

    // Latin: "hi there" -> 2 words with char boxes.
    {
        StubRasterOcrEngine engine;
        engine.text = QStringLiteral("hi there");
        engine.addStrokes({makeLineStroke(QStringLiteral("a"), 10, 10, 200, 12)});
        const auto results = engine.analyze();
        ok = ok && results.size() == 1;
        if (ok) {
            const auto& segs = results[0].wordSegments;
            ok = ok && segs.size() == 2;
            if (ok) {
                ok = ok && segs[0].text == QStringLiteral("hi")
                        && segs[0].charBoundingBoxes.size() == 2;
                ok = ok && segs[1].text == QStringLiteral("there")
                        && segs[1].charBoundingBoxes.size() == 5;
            }
        }
        qDebug() << "  Latin words:" << (ok ? "ok" : "FAIL");
    }

    // CJK: "你好" -> 2 single-glyph segments.
    {
        StubRasterOcrEngine engine;
        engine.text = QString::fromUtf8("\xE4\xBD\xA0\xE5\xA5\xBD"); // 你好
        engine.addStrokes({makeLineStroke(QStringLiteral("a"), 10, 10, 120, 12)});
        const auto results = engine.analyze();
        bool cjkOk = results.size() == 1;
        if (cjkOk) {
            const auto& segs = results[0].wordSegments;
            cjkOk = segs.size() == 2
                 && segs[0].text.length() == 1 && segs[0].charBoundingBoxes.size() == 1
                 && segs[1].text.length() == 1 && segs[1].charBoundingBoxes.size() == 1;
        }
        qDebug() << "  CJK glyphs:" << (cjkOk ? "ok" : "FAIL");
        ok = ok && cjkOk;
    }

    // Fallback: no char boxes -> single line-level segment.
    {
        StubRasterOcrEngine engine;
        engine.text = QStringLiteral("fallback");
        engine.emitCharBoxes = false;
        engine.addStrokes({makeLineStroke(QStringLiteral("a"), 10, 10, 120, 12)});
        const auto results = engine.analyze();
        bool fbOk = results.size() == 1
                 && results[0].wordSegments.size() == 1
                 && results[0].wordSegments[0].charBoundingBoxes.isEmpty()
                 && results[0].wordSegments[0].text == QStringLiteral("fallback");
        qDebug() << "  Fallback segment:" << (fbOk ? "ok" : "FAIL");
        ok = ok && fbOk;
    }

    qDebug() << (ok ? "PASS" : "FAIL") << "- segment assembly";
    return ok;
}

// ----------------------------------------------------------------------------
// Test: charBoundingBoxes survive OcrTextBlock JSON round-trip.
// ----------------------------------------------------------------------------
inline bool testCharBoxJsonRoundTrip()
{
    qDebug() << "=== Test: charBoundingBoxes JSON Round-Trip ===";
    OcrTextBlock block = OcrTextBlock::create();
    block.text = QStringLiteral("hi");
    block.boundingRect = QRectF(0, 0, 50, 20);
    block.engineId = QStringLiteral("stub_raster");

    OcrTextBlock::WordSegment seg;
    seg.text = QStringLiteral("hi");
    seg.boundingRect = QRectF(0, 0, 50, 20);
    seg.charBoundingBoxes = {QRectF(0, 0, 25, 20), QRectF(25, 0, 25, 20)};
    block.wordSegments.append(seg);

    // A second segment with no char boxes (must stay empty after round-trip).
    OcrTextBlock::WordSegment seg2;
    seg2.text = QStringLiteral("yo");
    seg2.boundingRect = QRectF(60, 0, 30, 20);
    block.wordSegments.append(seg2);

    const OcrTextBlock restored = OcrTextBlock::fromJson(block.toJson());

    bool ok = restored.wordSegments.size() == 2;
    if (ok) {
        const auto& r0 = restored.wordSegments[0];
        ok = ok && r0.charBoundingBoxes.size() == 2
                && nearlyEqual(r0.charBoundingBoxes[0].width(), 25.0, 0.01)
                && nearlyEqual(r0.charBoundingBoxes[1].x(), 25.0, 0.01);
        ok = ok && restored.wordSegments[1].charBoundingBoxes.isEmpty();
    }

    qDebug() << (ok ? "PASS" : "FAIL") << "- char-box serialization";
    return ok;
}

// ----------------------------------------------------------------------------
// Test: flattenOcrBlockCharRects maps block.text to per-char rects, synthesizes
// gap rects for spaces, and returns empty when geometry is missing/mismatched.
// (Phase 4E consumer-wiring helper used by search + selection.)
// ----------------------------------------------------------------------------
inline bool testFlattenBlockCharRects()
{
    qDebug() << "=== Test: Flatten Block Char Rects (4E) ===";
    bool ok = true;

    // Latin with an inter-word space: "hi yo".
    {
        OcrTextBlock block = OcrTextBlock::create();
        block.text = QStringLiteral("hi yo");
        block.boundingRect = QRectF(0, 0, 110, 20);
        OcrTextBlock::WordSegment s0;
        s0.text = QStringLiteral("hi");
        s0.charBoundingBoxes = {QRectF(0, 0, 25, 20), QRectF(25, 0, 25, 20)};
        OcrTextBlock::WordSegment s1;
        s1.text = QStringLiteral("yo");
        s1.charBoundingBoxes = {QRectF(60, 0, 25, 20), QRectF(85, 0, 25, 20)};
        block.wordSegments = {s0, s1};

        const QVector<QRectF> flat = flattenOcrBlockCharRects(block);
        bool latinOk = flat.size() == 5;                 // 'h','i',' ','y','o'
        latinOk = latinOk && nearlyEqual(flat[0].x(), 0.0, 0.01);
        latinOk = latinOk && nearlyEqual(flat[1].x(), 25.0, 0.01);
        latinOk = latinOk && nearlyEqual(flat[3].x(), 60.0, 0.01);
        latinOk = latinOk && nearlyEqual(flat[4].x(), 85.0, 0.01);
        // Space gap spans from box[1].right (50) to box[2].left (60).
        latinOk = latinOk && nearlyEqual(flat[2].left(), 50.0, 0.01)
                          && nearlyEqual(flat[2].width(), 10.0, 0.01)
                          && nearlyEqual(flat[2].height(), 20.0, 0.01);
        qDebug() << "  Latin + space:" << (latinOk ? "ok" : "FAIL");
        ok = ok && latinOk;
    }

    // CJK: "你好" -> one box each, no spaces.
    {
        OcrTextBlock block = OcrTextBlock::create();
        block.text = QString::fromUtf8("\xE4\xBD\xA0\xE5\xA5\xBD"); // 你好
        block.boundingRect = QRectF(0, 0, 40, 20);
        OcrTextBlock::WordSegment g0;
        g0.text = block.text.mid(0, 1);
        g0.charBoundingBoxes = {QRectF(0, 0, 20, 20)};
        OcrTextBlock::WordSegment g1;
        g1.text = block.text.mid(1, 1);
        g1.charBoundingBoxes = {QRectF(20, 0, 20, 20)};
        block.wordSegments = {g0, g1};

        const QVector<QRectF> flat = flattenOcrBlockCharRects(block);
        bool cjkOk = flat.size() == 2
                  && nearlyEqual(flat[0].x(), 0.0, 0.01)
                  && nearlyEqual(flat[1].x(), 20.0, 0.01);
        qDebug() << "  CJK glyphs:" << (cjkOk ? "ok" : "FAIL");
        ok = ok && cjkOk;
    }

    // Fallback: single line-level segment with no char boxes -> empty result.
    {
        OcrTextBlock block = OcrTextBlock::create();
        block.text = QStringLiteral("fallback");
        block.boundingRect = QRectF(0, 0, 80, 20);
        OcrTextBlock::WordSegment seg;
        seg.text = QStringLiteral("fallback");  // no charBoundingBoxes
        block.wordSegments = {seg};

        const bool fbOk = flattenOcrBlockCharRects(block).isEmpty();
        qDebug() << "  Missing geometry -> empty:" << (fbOk ? "ok" : "FAIL");
        ok = ok && fbOk;
    }

    // Mismatch: box count != text length -> empty result.
    {
        OcrTextBlock block = OcrTextBlock::create();
        block.text = QStringLiteral("ab");
        block.boundingRect = QRectF(0, 0, 50, 20);
        OcrTextBlock::WordSegment seg;
        seg.text = QStringLiteral("ab");
        seg.charBoundingBoxes = {QRectF(0, 0, 25, 20)};  // only 1 box for 2 chars
        block.wordSegments = {seg};

        const bool mmOk = flattenOcrBlockCharRects(block).isEmpty();
        qDebug() << "  Size mismatch -> empty:" << (mmOk ? "ok" : "FAIL");
        ok = ok && mmOk;
    }

    qDebug() << (ok ? "PASS" : "FAIL") << "- flatten block char rects";
    return ok;
}

// ----------------------------------------------------------------------------
// Test: the flattenOcrCharRects(text, segments) overload (used by the OCR text
// overlay, OcrTextObject::render) is bit-for-bit consistent with the block
// helper that search + selection use. Same geometry source -> same rects.
// (Phase 4D overlay consumer wiring.)
// ----------------------------------------------------------------------------
inline bool testFlattenCharRectsOverload()
{
    qDebug() << "=== Test: Flatten Char Rects Overload (4D overlay) ===";
    bool ok = true;

    OcrTextBlock block = OcrTextBlock::create();
    block.text = QStringLiteral("hi yo");
    block.boundingRect = QRectF(0, 0, 110, 20);
    OcrTextBlock::WordSegment s0;
    s0.text = QStringLiteral("hi");
    s0.charBoundingBoxes = {QRectF(0, 0, 25, 20), QRectF(25, 0, 25, 20)};
    OcrTextBlock::WordSegment s1;
    s1.text = QStringLiteral("yo");
    s1.charBoundingBoxes = {QRectF(60, 0, 25, 20), QRectF(85, 0, 25, 20)};
    block.wordSegments = {s0, s1};

    const QVector<QRectF> viaBlock = flattenOcrBlockCharRects(block);
    const QVector<QRectF> viaOverload = flattenOcrCharRects(block.text, block.wordSegments);

    bool parity = viaBlock.size() == viaOverload.size() && !viaOverload.isEmpty();
    for (int i = 0; parity && i < viaOverload.size(); ++i) {
        parity = nearlyEqual(viaBlock[i].x(), viaOverload[i].x(), 0.01)
              && nearlyEqual(viaBlock[i].y(), viaOverload[i].y(), 0.01)
              && nearlyEqual(viaBlock[i].width(), viaOverload[i].width(), 0.01)
              && nearlyEqual(viaBlock[i].height(), viaOverload[i].height(), 0.01);
    }
    qDebug() << "  Overload parity with block helper:" << (parity ? "ok" : "FAIL");
    ok = ok && parity;

    // Empty segments -> empty result (overlay falls back to run-merge layout).
    const bool emptyOk = flattenOcrCharRects(QStringLiteral("xy"), {}).isEmpty();
    qDebug() << "  Empty segments -> empty:" << (emptyOk ? "ok" : "FAIL");
    ok = ok && emptyOk;

    qDebug() << (ok ? "PASS" : "FAIL") << "- flatten char rects overload";
    return ok;
}

// ----------------------------------------------------------------------------
// Test: resolveAutoLanguage maps a system locale to a backend tag. Vision-style
// available list uses script-tagged Chinese (zh-Hans/zh-Hant), so a naive exact
// match on "zh-CA" fails - this verifies the tiered match picks the right tag.
// ----------------------------------------------------------------------------
inline bool testResolveAutoLanguage()
{
    qDebug() << "=== Test: Resolve Auto Language ===";
    bool ok = true;

    // Vision-like supported set.
    const QStringList vision = {
        QStringLiteral("en-US"), QStringLiteral("fr-FR"), QStringLiteral("de-DE"),
        QStringLiteral("ja-JP"), QStringLiteral("ko-KR"),
        QStringLiteral("zh-Hans"), QStringLiteral("zh-Hant"),
    };

    struct Case {
        QString lang, script, bcp47, name, expected, label;
    };
    const QVector<Case> cases = {
        // The reported bug: Simplified-Chinese system shown as zh_CA.
        {QStringLiteral("zh"), QStringLiteral("Hans"), QStringLiteral("zh-Hans"),
         QStringLiteral("zh_CA"), QStringLiteral("zh-Hans"), QStringLiteral("zh_CA + Hans")},
        // No script from QLocale -> infer from region.
        {QStringLiteral("zh"), QString(), QString(),
         QStringLiteral("zh_TW"), QStringLiteral("zh-Hant"), QStringLiteral("zh_TW (region-inferred)")},
        {QStringLiteral("zh"), QString(), QString(),
         QStringLiteral("zh_CN"), QStringLiteral("zh-Hans"), QStringLiteral("zh_CN (region-inferred)")},
        // Subtag fallback: fr-CA not present, fr-FR is.
        {QStringLiteral("fr"), QString(), QStringLiteral("fr-CA"),
         QStringLiteral("fr_CA"), QStringLiteral("fr-FR"), QStringLiteral("fr_CA -> fr-FR")},
        // Exact match wins when present.
        {QStringLiteral("ja"), QString(), QStringLiteral("ja-JP"),
         QStringLiteral("ja_JP"), QStringLiteral("ja-JP"), QStringLiteral("ja_JP exact")},
        // No backend support -> empty (engine default).
        {QStringLiteral("xx"), QString(), QString(),
         QStringLiteral("xx_YY"), QString(), QStringLiteral("unknown -> empty")},
    };

    for (const Case& c : cases) {
        const QString got = RasterOcrEngine::resolveAutoLanguage(
            c.lang, c.script, c.bcp47, c.name, vision);
        const bool caseOk = (got == c.expected);
        qDebug() << "  " << c.label << ":" << (caseOk ? "ok" : "FAIL")
                 << "got" << (got.isEmpty() ? QStringLiteral("(empty)") : got);
        ok = ok && caseOk;
    }

    // Empty available list -> empty (no crash).
    ok = ok && RasterOcrEngine::resolveAutoLanguage(
                   QStringLiteral("zh"), QStringLiteral("Hans"),
                   QStringLiteral("zh-Hans"), QStringLiteral("zh_CN"), {}).isEmpty();

    qDebug() << (ok ? "PASS" : "FAIL") << "- resolve auto language";
    return ok;
}

inline bool runAllTests()
{
    qDebug() << "\n========================================";
    qDebug() << "Running OCR Raster Pipeline Tests (Phase 4A)";
    qDebug() << "========================================\n";

    bool allPass = true;
    allPass &= testGrouping();
    allPass &= testRenderNormalization();
    allPass &= testTransformRoundTrip();
    allPass &= testCacheHitEvict();
    allPass &= testSegmentAssembly();
    allPass &= testCharBoxJsonRoundTrip();
    allPass &= testFlattenBlockCharRects();
    allPass &= testFlattenCharRectsOverload();
    allPass &= testResolveAutoLanguage();

    qDebug() << "\n========================================";
    qDebug() << (allPass ? "ALL TESTS PASSED!" : "SOME TESTS FAILED!");
    qDebug() << "========================================\n";
    return allPass;
}

} // namespace OcrRasterTests
