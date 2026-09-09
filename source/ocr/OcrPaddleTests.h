#pragma once

// ============================================================================
// OcrPaddleTests - Sanity check for the Phase 4B PaddleOCR backend (Linux)
// ============================================================================
// Renders typed text (which the recognizer handles deterministically, unlike
// handwriting) into a normalized Grayscale8 strip and runs it straight through
// PaddleOcrEngine::recognizeImage(). This locks the ONNX Runtime wiring, the
// preprocessing, and the CTC decode + strip-pixel box mapping without depending
// on handwriting quality. Handwriting accuracy itself is verified manually
// end-to-end.
//
// Run with:  speedynote --test-ocr-paddle   (Linux, debug builds only)
// ============================================================================

#if defined(SPEEDYNOTE_HAS_PADDLE_OCR)

#include "engines/PaddleOcrEngine.h"

#include <QDebug>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QRectF>
#include <QString>

namespace OcrPaddleTests {

// Exposes the protected recognizeImage() for direct strip testing.
class PaddleTestAccess : public PaddleOcrEngine {
public:
    auto runRecognize(const QImage& strip, const QString& lang)
    {
        return recognizeImage(strip, lang);
    }
    static QString mapLanguage(const QString& tag)
    {
        return modelFileForLanguage(tag);
    }
};

// Pure-function test: language tag -> script model file. No I/O or models, so
// this runs even when the vendored runtime / models are absent (Phase 4D).
inline bool testLanguageMapping()
{
    qDebug() << "=== Test: Language -> Model Mapping (4D) ===";
    struct Case { const char* tag; const char* file; };
    const Case cases[] = {
        {"en-US", "latin_rec.onnx"},
        {"fr-FR", "latin_rec.onnx"},
        {"de-DE", "latin_rec.onnx"},
        {"zh-CN", "ch_rec.onnx"},
        {"ja-JP", "ch_rec.onnx"},
        {"ko-KR", "korean_rec.onnx"},
        {"ru-RU", "cyrillic_rec.onnx"},
        {"uk-UA", "cyrillic_rec.onnx"},
        {"ar-SA", "arabic_rec.onnx"},
        {"fa-IR", "arabic_rec.onnx"},
        {"hi-IN", "devanagari_rec.onnx"},
        {"mr-IN", "devanagari_rec.onnx"},
        {"xx-YY", "latin_rec.onnx"}, // unknown -> default latin
    };
    bool ok = true;
    for (const auto& c : cases) {
        const QString got = PaddleTestAccess::mapLanguage(QString::fromUtf8(c.tag));
        const bool pass = (got == QString::fromUtf8(c.file));
        if (!pass) {
            ok = false;
            qDebug() << "  FAIL:" << c.tag << "->" << got << "expected" << c.file;
        }
    }
    qDebug() << (ok ? "PASS" : "FAIL") << "- language mapping";
    return ok;
}

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
    qDebug() << "Running OCR PaddleOCR Tests (Phase 4B/4D)";
    qDebug() << "========================================\n";

    // Pure-function checks first: these run regardless of model availability.
    bool mappingOk = testLanguageMapping();

    PaddleTestAccess engine;

    if (!engine.isAvailable()) {
        qDebug() << "SKIP: PaddleOCR recognition (vendored ONNX Runtime / models "
                    "not found). Run linux/fetch-onnxruntime.sh + "
                    "linux/fetch-ocr-models.sh.";
        return mappingOk;
    }

    qDebug() << "Available languages:" << engine.availableLanguages();

    const QString expected = QStringLiteral("Hello");
    const int W = 320;
    const int H = 64;
    const QImage strip = renderTypedStrip(expected, W, H);

    // Sanity: confirm the strip actually contains ink, so an empty result can
    // be attributed to recognition, not a blank input (process lesson from 4C).
    int darkPixels = 0;
    for (int y = 0; y < strip.height(); ++y)
        for (int x = 0; x < strip.width(); ++x)
            if (qGray(strip.pixel(x, y)) < 128)
                ++darkPixels;
    qDebug() << "Rendered strip dark pixels:" << darkPixels;

    const auto rec = engine.runRecognize(strip, QStringLiteral("en-US"));
    qDebug() << "Recognized text:" << rec.text
             << "| char boxes:" << rec.charBoxesImage.size();

    bool ok = !rec.text.isEmpty();
    if (!ok)
        qDebug() << "FAIL: PaddleOCR returned empty text for printed input.";

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
            const bool inside = b.left() >= -1.0 && b.top() >= -1.0
                             && b.right() <= W + 1.0 && b.bottom() <= H + 1.0;
            if (!inside) {
                ok = false;
                qDebug() << "FAIL: char box out of strip bounds:" << b;
                break;
            }
        }
    }

    ok = ok && mappingOk;

    qDebug() << "\n========================================";
    qDebug() << (ok ? "ALL TESTS PASSED!" : "SOME TESTS FAILED!");
    qDebug() << "========================================\n";
    return ok;
}

} // namespace OcrPaddleTests

#endif // SPEEDYNOTE_HAS_PADDLE_OCR
