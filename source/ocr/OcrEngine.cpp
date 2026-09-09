#include "OcrEngine.h"

#ifdef SPEEDYNOTE_HAS_WINDOWS_INK
#include "engines/WindowsInkOcrEngine.h"
#endif

#ifdef SPEEDYNOTE_HAS_MLKIT_INK
#include "engines/MlKitOcrEngine.h"
#endif

#ifdef SPEEDYNOTE_HAS_VISION_OCR
#include "engines/VisionOcrEngine.h"
#endif

#ifdef SPEEDYNOTE_HAS_PADDLE_OCR
#include "engines/PaddleOcrEngine.h"
#endif

std::unique_ptr<OcrEngine> OcrEngine::createBest()
{
#ifdef SPEEDYNOTE_HAS_WINDOWS_INK
    {
        auto engine = std::make_unique<WindowsInkOcrEngine>();
        if (engine->isAvailable())
            return engine;
    }
#endif
#ifdef SPEEDYNOTE_HAS_MLKIT_INK
    {
        auto engine = std::make_unique<MlKitOcrEngine>();
        if (engine->isAvailable())
            return engine;
    }
#endif
#ifdef SPEEDYNOTE_HAS_VISION_OCR
    {
        auto engine = std::make_unique<VisionOcrEngine>();
        if (engine->isAvailable())
            return engine;
    }
#endif
#ifdef SPEEDYNOTE_HAS_PADDLE_OCR
    {
        auto engine = std::make_unique<PaddleOcrEngine>();
        if (engine->isAvailable())
            return engine;
    }
#endif
    return nullptr;
}
