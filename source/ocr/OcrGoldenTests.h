#pragma once

// ============================================================================
// OcrGoldenTests - Golden-image regression tests for the stroke rasterizer
// ============================================================================
// Part of OCR Phase 4D. Locks the normalized rasterization (OcrStrokeRasterizer)
// against silent drift in scale / padding / pen width / ink placement, which
// would otherwise quietly change recognition accuracy AND the line-signature
// cache keys (QA Q10.1).
//
// Each case renders a fixed set of synthetic strokes through rasterizeStrokes()
// and compares the resulting Grayscale8 strip to a committed baseline PNG under
// tests/ocr/golden/. Comparison is tolerance-based: image dimensions must match
// exactly (the primary normalization lock), while per-pixel intensity may differ
// slightly to absorb antialiasing differences across Qt patch versions.
//
// Baselines are generated on the developer machine:
//   - If a baseline is missing it is created automatically and the case passes
//     with a GENERATED note (re-run to actually compare).
//   - To force-regenerate existing baselines:  SPEEDYNOTE_GEN_GOLDEN=1 speedynote --test-ocr-golden
//
// Run with:  speedynote --test-ocr-golden   (debug, non-mobile builds only)
// ============================================================================

#include "OcrStrokeRasterizer.h"
#include "../strokes/VectorStroke.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPointF>
#include <QString>
#include <QVector>

#include <cmath>

namespace OcrGoldenTests {

// Tolerances: dimensions are a hard match; intensity diffs absorb AA jitter.
constexpr int    kMaxPixelDiff  = 64;    ///< max abs per-pixel diff allowed (0-255)
constexpr double kMeanPixelDiff = 1.5;   ///< max mean abs per-pixel diff allowed

struct GoldenCase {
    QString name;
    QVector<VectorStroke> strokes;
    QVector<int> indices;
    int targetHeight;
};

inline VectorStroke makeStroke(const QString& id, const QVector<QPointF>& pts)
{
    VectorStroke s;
    s.id = id;
    s.baseThickness = 3.0;
    for (const QPointF& p : pts)
        s.points.append({p, 0.5});
    s.updateBoundingBox();
    return s;
}

inline QVector<GoldenCase> goldenCases()
{
    QVector<GoldenCase> cases;

    // 1. A zigzag "word"-like stroke with real x-height (so normalization
    //    produces a sanely-proportioned strip, like a real text line).
    cases.append({QStringLiteral("horizontal_line"),
                  {makeStroke(QStringLiteral("h"),
                              {QPointF(0, 20), QPointF(25, 0), QPointF(50, 20),
                               QPointF(75, 0), QPointF(100, 20)})},
                  {0}, 48});

    // 2. A steep diagonal (exercises non-trivial aspect + scaling).
    cases.append({QStringLiteral("diagonal"),
                  {makeStroke(QStringLiteral("d"),
                              {QPointF(0, 0), QPointF(60, 40), QPointF(120, 80)})},
                  {0}, 48});

    // 3. Two strokes forming a small shape (multi-stroke bounds + spacing).
    cases.append({QStringLiteral("two_strokes"),
                  {makeStroke(QStringLiteral("a"),
                              {QPointF(0, 30), QPointF(20, 0), QPointF(40, 30)}),
                   makeStroke(QStringLiteral("b"),
                              {QPointF(10, 18), QPointF(30, 18)})},
                  {0, 1}, 48});

    return cases;
}

inline QString goldenDir()
{
#ifdef SPEEDYNOTE_SOURCE_DIR
    return QString::fromUtf8(SPEEDYNOTE_SOURCE_DIR "/tests/ocr/golden");
#else
    return QStringLiteral("tests/ocr/golden");
#endif
}

struct DiffStats {
    bool sameGeometry = false;
    int  maxDiff = 255;
    double meanDiff = 255.0;
};

inline DiffStats compareGray(const QImage& a, const QImage& b)
{
    DiffStats d;
    if (a.size() != b.size())
        return d;  // sameGeometry stays false -> hard fail
    d.sameGeometry = true;

    const QImage ga = a.convertToFormat(QImage::Format_Grayscale8);
    const QImage gb = b.convertToFormat(QImage::Format_Grayscale8);
    const int w = ga.width();
    const int h = ga.height();
    double sum = 0.0;
    int maxv = 0;
    for (int y = 0; y < h; ++y) {
        const uchar* ra = ga.constScanLine(y);
        const uchar* rb = gb.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            const int diff = std::abs(static_cast<int>(ra[x]) - static_cast<int>(rb[x]));
            sum += diff;
            if (diff > maxv) maxv = diff;
        }
    }
    d.maxDiff = maxv;
    d.meanDiff = (w > 0 && h > 0) ? sum / (static_cast<double>(w) * h) : 0.0;
    return d;
}

inline bool runAllTests()
{
    qDebug() << "\n========================================";
    qDebug() << "Running OCR Rasterizer Golden Tests (Phase 4D)";
    qDebug() << "========================================\n";

    const bool regen = qEnvironmentVariableIntValue("SPEEDYNOTE_GEN_GOLDEN") != 0;
    const QString dir = goldenDir();
    qDebug() << "Golden dir:" << dir << (regen ? "(REGENERATE mode)" : "");

    bool allPass = true;
    for (const GoldenCase& c : goldenCases()) {
        const RasterStrip strip = rasterizeStrokes(c.strokes, c.indices, c.targetHeight);
        if (strip.image.isNull()) {
            qDebug() << "FAIL" << c.name << "- rasterizer produced a null image";
            allPass = false;
            continue;
        }

        const QString path = dir + QStringLiteral("/") + c.name + QStringLiteral(".png");
        const bool exists = QFileInfo::exists(path);

        if (regen || !exists) {
            QDir().mkpath(dir);
            if (!strip.image.save(path, "PNG")) {
                qDebug() << "FAIL" << c.name << "- could not write baseline to" << path;
                allPass = false;
                continue;
            }
            qDebug() << "GENERATED" << c.name << "->" << path
                     << strip.image.size() << "(re-run to compare)";
            continue;  // generated baseline counts as a pass for this run
        }

        QImage baseline(path);
        if (baseline.isNull()) {
            qDebug() << "FAIL" << c.name << "- could not load baseline" << path;
            allPass = false;
            continue;
        }

        const DiffStats d = compareGray(strip.image, baseline);
        const bool ok = d.sameGeometry
                     && d.maxDiff <= kMaxPixelDiff
                     && d.meanDiff <= kMeanPixelDiff;
        if (!ok) {
            allPass = false;
            if (!d.sameGeometry) {
                qDebug() << "FAIL" << c.name << "- size drift: got" << strip.image.size()
                         << "vs baseline" << baseline.size();
            } else {
                qDebug() << "FAIL" << c.name << "- pixel drift: maxDiff" << d.maxDiff
                         << "meanDiff" << d.meanDiff
                         << "(limits" << kMaxPixelDiff << "/" << kMeanPixelDiff << ")";
            }
        } else {
            qDebug() << "PASS" << c.name << "- size" << strip.image.size()
                     << "maxDiff" << d.maxDiff << "meanDiff" << d.meanDiff;
        }
    }

    qDebug() << "\n========================================";
    qDebug() << (allPass ? "ALL TESTS PASSED!" : "SOME TESTS FAILED!");
    qDebug() << "========================================\n";
    return allPass;
}

} // namespace OcrGoldenTests
