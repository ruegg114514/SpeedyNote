#pragma once

// ============================================================================
// TouchGestureHandler - Handles touch gestures for DocumentViewport
// ============================================================================
// Part of the Touch Gesture Hub (Phase TG)
//
// This class encapsulates all touch gesture logic:
// - Single-finger pan with inertia
// - Pinch-to-zoom (in Full mode)
// - 3-finger tap detection
//
// It calls back to DocumentViewport's deferred gesture APIs for smooth rendering.
// ============================================================================

#include <QObject>
#include <QPointF>
#include <QVector>
#include <QSet>
#include <QHash>
#include <QElapsedTimer>
#include <QTimer>

// Forward declarations
class QTouchEvent;
class DocumentViewport;

// TouchGestureMode enum (shared definition with MainWindow.h)
#ifndef TOUCHGESTUREMODE_DEFINED
#define TOUCHGESTUREMODE_DEFINED
enum class TouchGestureMode {
    Disabled,     // Touch gestures completely off
    YAxisOnly,    // Only Y-axis panning allowed (no X-axis, no zoom)
    Full          // Full touch gestures (pan + pinch-to-zoom)
};
#endif

/**
 * @brief Handles touch gestures for DocumentViewport.
 * 
 * Processes touch events and translates them into pan/zoom operations
 * by calling the viewport's deferred gesture API. This keeps touch logic
 * separate from the main viewport rendering code.
 */
class TouchGestureHandler : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Construct a touch gesture handler.
     * @param viewport The viewport to control (not owned).
     * @param parent QObject parent for memory management.
     */
    explicit TouchGestureHandler(DocumentViewport* viewport, QObject* parent = nullptr);
    ~TouchGestureHandler();
    
    /**
     * @brief Handle a touch event.
     * @param event The touch event to process.
     * @return True if event was handled, false otherwise.
     */
    bool handleTouchEvent(QTouchEvent* event);
    
    /**
     * @brief Reset all gesture state.
     * 
     * Call this when the viewport is hidden, document changes, or app resumes.
     * Clears all tracking state, stops inertia, and ends any active gestures.
     */
    void reset();
    
    /**
     * @brief Set the touch gesture mode.
     * @param mode New mode (Disabled, YAxisOnly, Full).
     * 
     * Ends any active gesture if mode changes.
     */
    void setMode(TouchGestureMode mode);
    
    /**
     * @brief Get the current touch gesture mode.
     */
    TouchGestureMode mode() const { return m_mode; }
    
    /**
     * @brief Check if a touch gesture is currently active.
     * Includes pending activation, active pan, pinch, or inertia animation.
     */
    bool isActive() const { return m_activationPending || m_panActive || m_pinchActive || (m_inertiaTimer && m_inertiaTimer->isActive()); }

    /**
     * @brief Check if the free-scroll inertia animation is currently running.
     * Distinct from isActive(): the finger is up and the viewport is being
     * driven purely by the inertia timer. Callers use this to relax or skip
     * work (e.g. background PDF preload) that would fight the per-frame
     * repaint on low-end devices.
     */
    bool isInertiaActive() const { return m_inertiaTimer && m_inertiaTimer->isActive(); }

    /**
     * @brief Cancel any pending or in-flight gesture immediately, without inertia.
     *
     * Called by the viewport when the stylus enters proximity or presses while
     * a touch gesture is running: that gesture was a palm all along, so it is
     * torn down instead of finalized. Position tracking is left intact; only
     * gesture state is cleared.
     */
    void cancelActiveGesture();

private slots:
    /**
     * @brief Activate the deferred gesture after the grace period expires.
     * Decides pan vs pinch from the currently tracked finger count.
     */
    void activatePendingGesture();

    /**
     * @brief Handle inertia animation frame.
     * Called by m_inertiaTimer to apply friction and update pan position.
     */
    void onInertiaFrame();

private:
    // ===== Viewport Reference =====
    DocumentViewport* m_viewport;  ///< Viewport to control (not owned)
    
    // ===== Mode =====
    TouchGestureMode m_mode = TouchGestureMode::Disabled;
    
    // ===== Deferred Gesture Activation (palm grace period) =====
    // A palm landing on the glass arrives as a normal 1-2 point touch BEFORE
    // the stylus is detected. Activating pan/pinch instantly at TouchBegin is
    // what turns "resting the hand" into an unwanted pan/zoom. Instead, the
    // gesture is only ACTIVATED after a short grace period; if the stylus
    // enters proximity (or presses) within the window, the viewport calls
    // cancelActiveGesture() and the touch never moves the canvas at all.
    bool m_activationPending = false;        ///< TouchBegin seen, gesture not yet activated
    QTimer* m_activationTimer = nullptr;     ///< Fires when the grace period expires
    /// Window for the stylus to veto the gesture. Deliberately short: since
    /// the drag-slop early activation was removed, this window is a fixed
    /// dead zone for every finger scroll (the canvas cannot move until it
    /// expires), so 50ms keeps fast swipes responsive while still catching a
    /// pen that arrives right after the hand lands. When the pen is ALREADY
    /// in proximity the viewport rejects the touch outright and this window
    /// is never even reached.
    static constexpr int ACTIVATION_GRACE_MS = 50;

    // ===== Single-finger Pan Tracking =====
    bool m_panActive = false;                ///< Whether a touch pan is in progress
    QPointF m_lastPos;                       ///< Last touch position (viewport coords)
    
    // ===== Pinch-to-zoom Tracking =====
    bool m_pinchActive = false;              ///< Whether a pinch gesture is in progress
    qreal m_pinchStartDistance = 0;          ///< Distance between fingers at start (for incremental scaling)
    qreal m_initialDistance = 0;             ///< Distance at gesture start (for zoom threshold)
    bool m_zoomActivated = false;            ///< Whether zoom threshold has been exceeded
    qreal m_smoothedScale = 1.0;             ///< Exponentially smoothed scale factor
    
    // Zoom dead zone: don't zoom until finger distance changes by this percentage
    // This prevents zoom "shaking" during pan-only 2-finger gestures
    static constexpr qreal ZOOM_ACTIVATION_THRESHOLD = 0.1;  ///< 10% change required to activate
    
    // Scale dead zone: treat scale values within this range of 1.0 as exactly 1.0
    // This prevents zoom jitter from small finger distance variations
    static constexpr qreal ZOOM_SCALE_DEAD_ZONE = 0.007;  ///< 0.7% dead zone
    
    // Zoom smoothing: exponential moving average factor (0-1)
    // Higher = more responsive but jittery, Lower = smoother but laggy
    static constexpr qreal ZOOM_SMOOTHING_FACTOR = 0.4;
    
    // ===== Velocity Tracking for Inertia =====
    QVector<QPointF> m_velocitySamples;      ///< Recent velocity samples (pixels/ms) for averaging
    QElapsedTimer m_velocityTimer;           ///< Timer for velocity calculation
    static constexpr int MAX_VELOCITY_SAMPLES = 5;  ///< Max samples to keep
    
    // ===== Inertia Animation =====
    QTimer* m_inertiaTimer = nullptr;        ///< Timer for inertia animation frames
    QPointF m_inertiaVelocity;               ///< Current inertia velocity (doc coords/ms)
    static constexpr qreal INERTIA_FRICTION = 0.92;     ///< Velocity multiplier per frame (lower = more resistance)
    static constexpr qreal INERTIA_MIN_VELOCITY = 0.05; ///< Stop threshold (doc coords/ms)
    static constexpr int INERTIA_INTERVAL_MS = 16;      ///< ~60 FPS
    
    // ===== Multi-touch Tracking =====
    int m_activeTouchPoints = 0;             ///< Number of active touch points (derived from tracked IDs)
    QSet<int> m_trackedTouchIds;             ///< Track touch point IDs across events (fixes Android event splitting)
    QHash<int, QPointF> m_lastTouchPositions; ///< Last known position for each touch ID (fixes Android partial updates)
    QHash<int, qint64> m_touchIdLastSeenTime; ///< Timestamp (ms) when each ID was last seen in an event
    static constexpr qint64 STALE_TIMEOUT_MS = 500; ///< Remove ID if not seen for this many milliseconds
    
    // ===== 3-Finger Tap Detection =====
    QElapsedTimer m_threeFingerTimer;        ///< Timer for 3-finger tap detection
    bool m_threeFingerTimerActive = false;   ///< Whether 3-finger timer is running
    static constexpr qint64 TAP_MAX_DURATION_MS = 300;  ///< Max duration for tap detection
    
    // ===== Helper Methods =====
    
    /**
     * @brief End the current touch pan gesture.
     * @param startInertia If true, start inertia animation if velocity is sufficient.
     */
    void endTouchPan(bool startInertia);
    
    /**
     * @brief End the current pinch-to-zoom gesture.
     */
    void endTouchPinch();
    
    /**
     * @brief Handle a 3-finger tap gesture.
     */
    void on3FingerTap();
    
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    /**
     * @brief Handle 2-finger gesture using native platform positions.
     * 
     * Called when Qt's touch tracking seems corrupted but native platform
     * reports 2 fingers. Uses platform-provided positions to ensure pinch works.
     * Android: positions via JNI from SpeedyNoteActivity.
     * iOS: positions via IOSTouchTracker gesture recognizer.
     * 
     * @param event The touch event (for accepting)
     * @param pos1 Native position of first finger (widget-local coords)
     * @param pos2 Native position of second finger (widget-local coords)
     * @return true (always handles the event)
     */
    bool handleTwoFingerGestureNative(QTouchEvent* event, QPointF pos1, QPointF pos2);
#endif
};
