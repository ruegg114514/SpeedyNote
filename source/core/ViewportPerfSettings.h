#pragma once

// ============================================================================
// ViewportPerfSettings - Runtime-adjustable viewport performance knobs
// ============================================================================
// Low-spec devices (e.g. Atom-class Windows tablets) cannot repaint the full
// viewport every pan frame: a pan frame must touch every pixel on screen, so
// its cost is memory traffic (~4.3x the full-screen RGBA buffer per frame)
// and no amount of stroke caching changes that. The one software lever that
// does help is shrinking the gesture snapshot: during an active pan/zoom
// gesture the cached frame is captured at a fraction of the device resolution
// and blitted back scaled up, cutting the read side of that traffic by the
// square of the factor. The image is soft while the fingers move and snaps
// back to full clarity the moment the gesture ends and the normal render
// path takes over.
//
// Header-only on purpose, mirroring PalmRejectionSettings: the value is read
// on the gesture hot path, so an inline struct beats a call out to another
// .cpp, and it leaves the build files untouched.
// ============================================================================

#include <QSettings>
#include <QString>
#include <QtGlobal>

struct ViewportPerfSettings {
    // ---- Gesture frame resolution (percent of device pixels) ----
    // 100 = capture the gesture snapshot at full device resolution (previous
    // behaviour, sharpest while dragging). 50 = quarter the pixels, the
    // recommended value for Atom-class tablets. Applies to both pan and zoom
    // gestures; the post-gesture full repaint is unaffected.
    int gestureFrameScalePercent = 100;

    // ---- Render only the page background in the pan strip ----
    // While a pan gesture is actually moving (finger down), the band of content
    // that the shifted snapshot exposes is repainted with the page background
    // layer only (paper colour, PDF/custom background image, grid or ruled
    // pattern) instead of a full page composite: ink and objects are dropped
    // for those frames and restored by the gesture-end repaint. The stroke
    // cache blit and the objects are the expensive part of that band on
    // low-end hardware. Visually the incoming content still reads as a page
    // scrolling in - the band shows paper, and a ruled page even shows its
    // lines - it just has no ink in it until the finger stops. Inertia (finger
    // up, still gliding) always renders the band in full, so the release
    // animation stays intact.
    bool skipStripDuringPan = false;

    // ===== Access =====

    /// Process-wide instance. Main thread only (QSettings and the settings
    /// dialog both touch it).
    static ViewportPerfSettings& instance() {
        static ViewportPerfSettings s;
        return s;
    }

    /// Pristine defaults - the behaviour that predates this being configurable.
    static ViewportPerfSettings defaults() { return ViewportPerfSettings(); }

    // ===== Values the render code actually applies =====

    /// Snapshot scale as a factor in (0, 1]. 1.0 disables the optimisation.
    qreal gestureFrameScale() const {
        return qBound(25, gestureFrameScalePercent, 100) / 100.0;
    }

    // ===== Persistence (QSettings group "viewportPerf") =====

    void load() {
        QSettings s(QStringLiteral("SpeedyNote"), QStringLiteral("App"));
        const ViewportPerfSettings d;
        s.beginGroup(QStringLiteral("viewportPerf"));
        gestureFrameScalePercent =
            s.value(QStringLiteral("gestureFrameScalePercent"),
                    d.gestureFrameScalePercent).toInt();
        skipStripDuringPan =
            s.value(QStringLiteral("skipStripDuringPan"),
                    d.skipStripDuringPan).toBool();
        s.endGroup();
        clamp();
    }

    void save() const {
        QSettings s(QStringLiteral("SpeedyNote"), QStringLiteral("App"));
        s.beginGroup(QStringLiteral("viewportPerf"));
        s.setValue(QStringLiteral("gestureFrameScalePercent"),
                   gestureFrameScalePercent);
        s.setValue(QStringLiteral("skipStripDuringPan"), skipStripDuringPan);
        s.endGroup();
    }

    void resetToDefaults() {
        *this = defaults();
        save();
    }

    /// Keep a hand-edited config file inside the range the UI offers, so the
    /// render code never sees a zero or >100% snapshot scale.
    void clamp() {
        gestureFrameScalePercent = qBound(25, gestureFrameScalePercent, 100);
    }
};

/// Shorthand for the process-wide instance.
inline ViewportPerfSettings& viewportPerf() {
    return ViewportPerfSettings::instance();
}
