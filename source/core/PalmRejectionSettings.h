#pragma once

// ============================================================================
// PalmRejectionSettings - Runtime-adjustable anti-mistouch guards
// ============================================================================
// Every accidental-touch guard in the app used to be a `static constexpr`
// baked into TouchGestureHandler / DocumentViewport, so a user could neither
// switch one off nor tune how long it waits. This module turns the whole set
// into one persisted struct: the settings panel edits it, and the input code
// reads it at the moment it needs a decision.
//
// Defaults reproduce the previous hardcoded behaviour exactly, with one
// deliberate exception documented on stylusSuppressEnabled below.
//
// Header-only on purpose. The values are read from a handful of translation
// units on input hot paths (every touch move), so an inline struct beats a
// call out to another .cpp, and it leaves the build files untouched.
// ============================================================================

#include <QSettings>
#include <QString>
#include <QtGlobal>

struct PalmRejectionSettings {
    // ---- 1. Stylus presence suppresses touch gestures (the master guard) ----
    // While the pen is in proximity or down, touch gestures are forced off and
    // restored once it leaves, after `stylusRestoreDelayMs`.
    //
    // Kept OFF by default to preserve existing behaviour: the old Linux-only
    // setting also defaulted to false. Everything below defaults to ON, which
    // is what the previously-hardcoded guards did.
    bool stylusSuppressEnabled = false;
    int  stylusRestoreDelayMs  = 500;

    // ---- 2. Gesture activation grace ("touch veto window") ----
    // A palm lands as an ordinary touch before the stylus is noticed, so the
    // gesture is not activated until this window passes; the stylus arriving
    // inside it cancels the gesture outright. 0 = activate immediately.
    bool gestureGraceEnabled = true;
    int  gestureGraceMs      = 100;

    // ---- 3. Stylus activity guard ----
    // Restarted by every stylus event. On expiry the pen has been silent this
    // long, so the per-sequence touch latch is cleared and any in-flight
    // gesture is cancelled.
    bool stylusGuardEnabled = true;
    int  stylusGuardMs      = 200;

    // ---- 4. Stylus proximity watchdog ----
    // Restarted by every tablet event; on expiry the pen counts as gone and
    // the touch lock is released. The primary unlock path on Windows/Wacom,
    // whose drivers often omit TabletLeaveProximity.
    bool stylusProximityEnabled = true;
    int  stylusProximityMs      = 200;

    // ---- 5. Palm contact detection ----
    // This many simultaneous touch points is a resting hand, not fingers: any
    // in-flight stroke is voided rather than being drawn.
    bool palmContactEnabled = true;
    int  palmContactPoints  = 3;

    // ---- 6. Touch cooldown ----
    // Touch input is ignored for this long after a gesture was rejected, so the
    // hand settling back down cannot immediately re-trigger a pan.
    bool touchCooldownEnabled = true;
    int  touchCooldownMs      = 300;

    // ---- 7. Single-finger scroll axis lock ----
    // Locks a one-finger drag to one axis so a slightly diagonal swipe does not
    // scroll and drift at once. `breakout` is how far off-axis the finger must
    // travel before the lock releases.
    bool scrollAxisLockEnabled = true;
    int  scrollLockDecidePx    = 10;
    int  scrollLockBreakoutPx  = 36;

    // ---- 8. Pinch zoom dead zone ----
    // Finger distance must change by this percentage before zoom engages, so a
    // two-finger pan does not jitter the zoom level.
    bool zoomDeadZoneEnabled   = true;
    int  zoomActivationPercent = 10;

    // ---- 9. Multi-touch tap detection ----
    // Longest press that still counts as a tap rather than a pan/pinch.
    bool tapDetectionEnabled = true;
    int  tapMaxDurationMs    = 300;

    // ---- 10. Free-scroll inertia ----
    // A released pan keeps gliding, decaying by INERTIA_FRICTION per frame.
    // Turning it off makes the canvas stop dead when the finger lifts. Two
    // reasons to offer this: on a slippery glass panel the glide can feel out
    // of control, and it is the second half of the "resting hand drift"
    // complaint - micro-movement of a resting finger produces velocity
    // samples, and the release then glides on them.
    bool inertiaEnabled = true;

    // ---- 11. Pan dead zone ----
    // A resting finger or palm is never perfectly still: the digitiser reports
    // a pixel or two of wobble, and every wobble used to move the canvas.
    // With this many pixels of tolerance the canvas does not move at all until
    // the finger has travelled this far from where the pan started, and no
    // velocity is sampled inside the zone (so releasing there cannot start
    // inertia either). 0 = previous behaviour (no tolerance).
    int panDeadZonePx = 0;

    // ===== Access =====

    /// Process-wide instance. Main thread only (QSettings and the settings
    /// dialog both touch it).
    static PalmRejectionSettings& instance() {
        static PalmRejectionSettings s;
        return s;
    }

    /// Pristine defaults - the behaviour that predates these being configurable.
    static PalmRejectionSettings defaults() { return PalmRejectionSettings(); }

    // ===== Values the guards actually apply (switch already folded in) =====

    int effectiveGestureGraceMs() const { return gestureGraceEnabled ? gestureGraceMs : 0; }
    int effectiveStylusGuardMs() const { return stylusGuardEnabled ? stylusGuardMs : 0; }
    int effectiveStylusProximityMs() const { return stylusProximityEnabled ? stylusProximityMs : 0; }
    int effectiveTouchCooldownMs() const { return touchCooldownEnabled ? touchCooldownMs : 0; }

    /// Fractional finger-distance change needed to engage pinch zoom. Zero when
    /// the zoom guards are off, so nothing is swallowed.
    qreal zoomActivationFraction() const {
        return zoomDeadZoneEnabled ? zoomActivationPercent / 100.0 : 0.0;
    }

    /// Scale delta below which the pinch scale counts as unchanged. Zero when
    /// the zoom guards are off, so no scale change is discarded.
    qreal zoomScaleDeadZone() const {
        return zoomDeadZoneEnabled ? 0.007 : 0.0;
    }

    // ===== Persistence (QSettings group "palmRejection") =====

    void load() {
        QSettings s(QStringLiteral("SpeedyNote"), QStringLiteral("App"));
        const PalmRejectionSettings d;

        s.beginGroup(QStringLiteral("palmRejection"));
        stylusSuppressEnabled  = s.value(QStringLiteral("stylusSuppress"), d.stylusSuppressEnabled).toBool();
        stylusRestoreDelayMs   = s.value(QStringLiteral("stylusRestoreDelayMs"), d.stylusRestoreDelayMs).toInt();
        gestureGraceEnabled    = s.value(QStringLiteral("gestureGrace"), d.gestureGraceEnabled).toBool();
        gestureGraceMs         = s.value(QStringLiteral("gestureGraceMs"), d.gestureGraceMs).toInt();
        stylusGuardEnabled     = s.value(QStringLiteral("stylusGuard"), d.stylusGuardEnabled).toBool();
        stylusGuardMs          = s.value(QStringLiteral("stylusGuardMs"), d.stylusGuardMs).toInt();
        stylusProximityEnabled = s.value(QStringLiteral("stylusProximity"), d.stylusProximityEnabled).toBool();
        stylusProximityMs      = s.value(QStringLiteral("stylusProximityMs"), d.stylusProximityMs).toInt();
        palmContactEnabled     = s.value(QStringLiteral("palmContact"), d.palmContactEnabled).toBool();
        palmContactPoints      = s.value(QStringLiteral("palmContactPoints"), d.palmContactPoints).toInt();
        touchCooldownEnabled   = s.value(QStringLiteral("touchCooldown"), d.touchCooldownEnabled).toBool();
        touchCooldownMs        = s.value(QStringLiteral("touchCooldownMs"), d.touchCooldownMs).toInt();
        scrollAxisLockEnabled  = s.value(QStringLiteral("scrollAxisLock"), d.scrollAxisLockEnabled).toBool();
        scrollLockDecidePx     = s.value(QStringLiteral("scrollLockDecidePx"), d.scrollLockDecidePx).toInt();
        scrollLockBreakoutPx   = s.value(QStringLiteral("scrollLockBreakoutPx"), d.scrollLockBreakoutPx).toInt();
        zoomDeadZoneEnabled    = s.value(QStringLiteral("zoomDeadZone"), d.zoomDeadZoneEnabled).toBool();
        zoomActivationPercent  = s.value(QStringLiteral("zoomActivationPercent"), d.zoomActivationPercent).toInt();
        tapDetectionEnabled    = s.value(QStringLiteral("tapDetection"), d.tapDetectionEnabled).toBool();
        tapMaxDurationMs       = s.value(QStringLiteral("tapMaxDurationMs"), d.tapMaxDurationMs).toInt();
        inertiaEnabled         = s.value(QStringLiteral("inertiaEnabled"), d.inertiaEnabled).toBool();
        panDeadZonePx          = s.value(QStringLiteral("panDeadZonePx"), d.panDeadZonePx).toInt();
        const bool migrated    = s.value(QStringLiteral("legacyMigrated"), false).toBool();
        s.endGroup();

        // One-time migration from the original Linux-only palm rejection panel,
        // which stored a single toggle + delay under these two keys. Without
        // this an existing install would silently lose its choice.
        if (!migrated) {
            if (s.contains(QStringLiteral("palmRejection/enabled"))) {
                stylusSuppressEnabled = s.value(QStringLiteral("palmRejection/enabled")).toBool();
                stylusRestoreDelayMs  =
                    s.value(QStringLiteral("palmRejection/delayMs"), d.stylusRestoreDelayMs).toInt();
            }
            s.setValue(QStringLiteral("palmRejection/legacyMigrated"), true);
        }

        clamp();
    }

    void save() const {
        QSettings s(QStringLiteral("SpeedyNote"), QStringLiteral("App"));
        s.beginGroup(QStringLiteral("palmRejection"));
        s.setValue(QStringLiteral("stylusSuppress"), stylusSuppressEnabled);
        s.setValue(QStringLiteral("stylusRestoreDelayMs"), stylusRestoreDelayMs);
        s.setValue(QStringLiteral("gestureGrace"), gestureGraceEnabled);
        s.setValue(QStringLiteral("gestureGraceMs"), gestureGraceMs);
        s.setValue(QStringLiteral("stylusGuard"), stylusGuardEnabled);
        s.setValue(QStringLiteral("stylusGuardMs"), stylusGuardMs);
        s.setValue(QStringLiteral("stylusProximity"), stylusProximityEnabled);
        s.setValue(QStringLiteral("stylusProximityMs"), stylusProximityMs);
        s.setValue(QStringLiteral("palmContact"), palmContactEnabled);
        s.setValue(QStringLiteral("palmContactPoints"), palmContactPoints);
        s.setValue(QStringLiteral("touchCooldown"), touchCooldownEnabled);
        s.setValue(QStringLiteral("touchCooldownMs"), touchCooldownMs);
        s.setValue(QStringLiteral("scrollAxisLock"), scrollAxisLockEnabled);
        s.setValue(QStringLiteral("scrollLockDecidePx"), scrollLockDecidePx);
        s.setValue(QStringLiteral("scrollLockBreakoutPx"), scrollLockBreakoutPx);
        s.setValue(QStringLiteral("zoomDeadZone"), zoomDeadZoneEnabled);
        s.setValue(QStringLiteral("zoomActivationPercent"), zoomActivationPercent);
        s.setValue(QStringLiteral("tapDetection"), tapDetectionEnabled);
        s.setValue(QStringLiteral("tapMaxDurationMs"), tapMaxDurationMs);
        s.setValue(QStringLiteral("inertiaEnabled"), inertiaEnabled);
        s.setValue(QStringLiteral("panDeadZonePx"), panDeadZonePx);
        s.endGroup();
    }

    void resetToDefaults() {
        *this = defaults();
        save();
    }

    /// Keep every value inside the range the settings UI offers, so a
    /// hand-edited config file cannot drive the input code into a nonsensical
    /// state (e.g. a zero-point palm threshold).
    void clamp() {
        stylusRestoreDelayMs  = qBound(0, stylusRestoreDelayMs, 5000);
        gestureGraceMs        = qBound(0, gestureGraceMs, 1000);
        stylusGuardMs         = qBound(0, stylusGuardMs, 2000);
        stylusProximityMs     = qBound(0, stylusProximityMs, 2000);
        palmContactPoints     = qBound(2, palmContactPoints, 10);
        touchCooldownMs       = qBound(0, touchCooldownMs, 2000);
        scrollLockDecidePx    = qBound(0, scrollLockDecidePx, 200);
        scrollLockBreakoutPx  = qBound(0, scrollLockBreakoutPx, 500);
        zoomActivationPercent = qBound(1, zoomActivationPercent, 100);
        tapMaxDurationMs      = qBound(50, tapMaxDurationMs, 2000);
        panDeadZonePx         = qBound(0, panDeadZonePx, 200);
    }
};

/// Shorthand for the process-wide instance. The name is deliberately short
/// because the guards that read it sit on the touch-move hot path.
inline PalmRejectionSettings& palmRejection() {
    return PalmRejectionSettings::instance();
}
