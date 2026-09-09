#pragma once

// ============================================================================
// OcrVisionTests - Sanity check for the Phase 4C Apple Vision backend (macOS)
// ============================================================================
// Renders typed text (which Vision recognizes deterministically, unlike
// handwriting) into a normalized Grayscale8 strip and runs it straight through
// VisionOcrEngine::recognizeImage(). This locks the request wiring + the
// normalized->image-pixel coordinate flip without depending on handwriting
// quality. Handwriting accuracy itself is verified manually end-to-end.
//
// Run with:  speedynote --test-ocr-vision   (macOS, debug builds only)
// ============================================================================

#if defined(SPEEDYNOTE_HAS_VISION_OCR)

#include "engines/VisionOcrEngine.h"

#include <QDebug>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QRectF>
#include <QString>

namespace OcrVisionTests {

// Exposes the protected recognizeImage() for direct strip testing.
class VisionTestAccess : public VisionOcrEngine {
public:
    auto runRecognize(const QImage& strip, const QString& lang)
    {
        return recognizeImage(strip, lang);
    }
};

inline QImage renderTypedStrip(const QString& text, int width, int height)
{
    QImage img(width, height, QImage::Format_Grayscale8);
    img.fill(255); // white, matching the rasterizer's dark-on-white convention

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    QFont font;
    font.setPixelSize(static_cast<int>(height * 0.6));
    p.setFont(font);
    p.setPen(Qt::black);
    p.drawText(QRectF(8, 0, width - 16, height), Qt::AlignVCenter | Qt::AlignLeft, text);
    p.end();
    return img;
}

inline bool runAllTests()
{
    qDebug() << "\n========================================";
    qDebug() << "Running OCR Apple Vision Tests (Phase 4C)";
    qDebug() << "========================================\n";

    VisionTestAccess engine;

    if (!engine.isAvailable()) {
        qDebug() << "SKIP: Apple Vision unavailable on this system.";
        return true;
    }

    qDebug() << "Available languages (first 10):"
             << engine.availableLanguages().mid(0, 10);

    const QString expected = QStringLiteral("Hello");
    const int W = 320;
    const int H = 64;
    const QImage strip = renderTypedStrip(expected, W, H);

    // Sanity: confirm the strip actually contains ink, so an empty Vision
    // result can be attributed to recognition (or a sandboxed XPC service),
    // not a blank input.
    int darkPixels = 0;
    for (int y = 0; y < strip.height(); ++y)
        for (int x = 0; x < strip.width(); ++x)
            if (qGray(strip.pixel(x, y)) < 128)
                ++darkPixels;
    qDebug() << "Rendered strip dark pixels:" << darkPixels;

    const auto rec = engine.runRecognize(strip, QStringLiteral("en-US"));
    qDebug() << "Recognized text:" << rec.text
             << "| char boxes:" << rec.charBoxesImage.size();

    // Vision's -boundingBoxForRange: collapses every sub-range to the whole-word
    // box, so the engine subdivides each observation uniformly per character.
    // Guard against a regression to identical (stacked) boxes: within a run of
    // non-null boxes, x must be strictly increasing.
    {
        int compared = 0, monotonic = 0;
        for (int i = 1; i < rec.charBoxesImage.size(); ++i) {
            const QRectF& a = rec.charBoxesImage[i - 1];
            const QRectF& b = rec.charBoxesImage[i];
            if (a.isNull() || b.isNull())
                continue; // inter-observation separator
            ++compared;
            if (b.x() > a.x() + 1e-3)
                ++monotonic;
        }
        if (compared > 0 && monotonic != compared) {
            qWarning() << "FAIL: per-char boxes not strictly increasing in x"
                       << "(" << monotonic << "/" << compared << ")";
            return false;
        }
        qDebug() << "Per-char box monotonicity OK (" << monotonic << "/" << compared << ")";
    }

    bool ok = !rec.text.isEmpty();
    if (!ok)
        qDebug() << "FAIL: Vision returned empty text for printed input.";

    // Soft check: printed text should usually round-trip exactly.
    if (!rec.text.contains(expected, Qt::CaseInsensitive))
        qDebug() << "WARN: recognized text does not contain" << expected
                 << "(font/rendering dependent).";

    // Hard invariant: when char boxes are present, they match the text length
    // and lie within the strip bounds.
    if (!rec.charBoxesImage.isEmpty()) {
        if (rec.charBoxesImage.size() != rec.text.length()) {
            ok = false;
            qDebug() << "FAIL: charBoxesImage.size()" << rec.charBoxesImage.size()
                     << "!= text.length()" << rec.text.length();
        }
        for (const QRectF& b : rec.charBoxesImage) {
            if (b.isNull())
                continue; // synthetic separator box
            const bool inside = b.left() >= -1.0 && b.top() >= -1.0
                             && b.right() <= W + 1.0 && b.bottom() <= H + 1.0;
            if (!inside) {
                ok = false;
                qDebug() << "FAIL: char box out of strip bounds:" << b;
                break;
            }
        }
    }

    // Regression: the "auto" sentinel must be normalized (never passed to Vision
    // as recognitionLanguages = @["auto"]). After setLanguage("auto") the engine
    // tag must not be the literal "auto", and recognition must still succeed.
    {
        engine.setLanguage(QStringLiteral("auto"));
        const QString resolved = engine.language();
        if (resolved.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0) {
            ok = false;
            qDebug() << "FAIL: 'auto' was not normalized (engine tag still 'auto').";
        } else {
            qDebug() << "Auto-detect resolved to tag:" << (resolved.isEmpty() ? "<engine default>" : resolved);
        }
        // A non-empty resolved tag must be a real Vision recognition language
        // (never a raw locale tag like "zh-CA" that Vision would reject).
        if (!resolved.isEmpty() && !engine.availableLanguages().contains(resolved)) {
            ok = false;
            qDebug() << "FAIL: resolved tag" << resolved
                     << "is not a supported Vision recognition language.";
        }
        const auto recAuto = engine.runRecognize(strip, resolved);
        if (recAuto.text.isEmpty()) {
            ok = false;
            qDebug() << "FAIL: recognition empty after auto-detect normalization.";
        }
    }

    qDebug() << "\n========================================";
    qDebug() << (ok ? "ALL TESTS PASSED!" : "SOME TESTS FAILED!");
    qDebug() << "========================================\n";
    return ok;
}

} // namespace OcrVisionTests

#endif // SPEEDYNOTE_HAS_VISION_OCR
