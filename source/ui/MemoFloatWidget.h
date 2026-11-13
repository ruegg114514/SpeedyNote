#pragma once

// ============================================================================
// MemoFloatWidget - one memo (随记) shown as a floating card over the page
// ============================================================================
// The card is a viewport-space overlay: it does NOT scroll with the page and it
// does NOT become a sticky note on the document. It can be dragged, resized
// from any of its 8 handles, and rotated freely from its rotation handle
// (15° steps, the same snap the canvas uses for rotated objects). A close
// button removes it from the canvas without deleting the memo.
//
// Geometry model
// --------------
// The card itself is an UNROTATED rectangle (`cardRect`, parent coordinates).
// The painting is that rectangle rotated by `rotation()` degrees around its
// centre. Because a rotated rectangle is not axis-aligned, the widget's own
// geometry is the axis-aligned bounding box of the rotated card, plus a small
// margin for the drop shadow and the rotation handle:
//
//        widget rect (setGeometry)
//        +-----------------------------+
//        |    +--------------------+   |   <- cardRect rotated by rotation()
//        |    |                    |   |
//        |    +--------------------+   |
//        +-----------------------------+
//
// `cardRect()` and `rotation()` are the only geometry a caller should persist;
// the widget keeps its own geometry derived from those two values.
//
// Input contract
// --------------
// * Mouse/stylus press inside the rotated card starts a move (or activates the
//   card and starts a handle when one is hit).
// * Wheel events are forwarded to the scroll target (the current viewport), so
//   a card lying on the page never eats the reader's scrolling.
// * The widget has Qt::NoFocus: clicking a card must not steal keyboard
//   shortcuts (Ctrl+Z, tool keys) from the main window.
// ============================================================================

#include <QPixmap>
#include <QRectF>
#include <QString>
#include <QWidget>

class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class QPaintEvent;
class QResizeEvent;
class QEvent;
class QEnterEvent;

class MemoFloatWidget : public QWidget {
    Q_OBJECT

public:
    /// Minimum and maximum card edge length, in logical pixels. The maximum is
    /// well below any screen: the card's scaled cache is card * dpr pixels, and
    /// a 6000 px card on a HiDPI screen would allocate hundreds of megabytes.
    static constexpr qreal MIN_CARD_SIZE = 48.0;
    static constexpr qreal MAX_CARD_SIZE = 3000.0;

    /// Size of the square resize handles at the card corners/edges.
    static constexpr qreal HANDLE_VISUAL_SIZE = 11.0;
    /// Touch-friendly hit radius for resize handles and the rotate handle.
    static constexpr qreal HANDLE_HIT_SIZE = 22.0;
    /// Distance of the rotate handle above the card's top edge.
    static constexpr qreal ROTATE_HANDLE_OFFSET = 28.0;
    /// Diameter of the close button drawn in the card's top-right corner.
    static constexpr qreal CLOSE_BUTTON_SIZE = 22.0;

    /**
     * @param memoId  Document memo id (the card keeps no pointer to the entry).
     * @param image   Full-resolution memo image. May be null for a broken memo,
     *                in which case a placeholder card is painted.
     */
    MemoFloatWidget(const QString& memoId, const QPixmap& image,
                    QWidget* parent = nullptr);
    ~MemoFloatWidget() override = default;

    QString memoId() const { return m_memoId; }

    QString title() const { return m_title; }
    void setTitle(const QString& title);

    void setDarkMode(bool darkMode);
    bool isDarkMode() const { return m_darkMode; }

    /// The card's UNROTATED rect in parent coordinates.
    QRectF cardRect() const;

    /// Rotation angle in degrees (clockwise-positive).
    qreal rotation() const { return m_rotation; }

    /**
     * @brief Restore a stored card rect + rotation.
     *
     * Places the card exactly at @p rect (parent coordinates) and re-derives
     * the widget geometry from it. Does not emit cardGeometryChanged().
     * Sizes below MIN_CARD_SIZE are grown to the minimum.
     */
    void applyStoredGeometry(const QRectF& rect, qreal rotation);

    /**
     * @brief The widget receives wheel events on behalf of this target.
     *
     * Expected to be the active DocumentViewport; wheel events are re-posted to
     * it so the page scrolls under the card. Null = ignore wheel events.
     */
    void setScrollTarget(QWidget* target) { m_scrollTarget = target; }

    /**
     * @brief Mark this card as the one whose handles are showing.
     *
     * The overlay keeps exactly one card active; clicking a card activates it,
     * clicking elsewhere on the canvas deactivates it.
     */
    void setActiveCard(bool active);
    bool isActiveCard() const { return m_active; }

    /// Convenience: fit a new card's aspect to @p imageSize at @p maxEdge.
    static QSizeF naturalCardSize(const QSize& imageSize,
                                  qreal maxEdge = 340.0);

    /**
     * @brief Lock resizing to the memo image's aspect ratio.
     *
     * Mirrors ImageObject::maintainAspectRatio (the lock button in the object
     * select action bar): while locked, every resize handle scales both axes
     * together, so the memo is never stretched. Unlocked resizing keeps the
     * historical free behaviour.
     */
    void setAspectLocked(bool locked);
    bool isAspectLocked() const { return m_aspectLocked; }

    /// The memo image's width/height ratio (1.0 when the image is missing).
    qreal imageAspect() const { return m_imageAspect; }

signals:
    /// Emitted after a move/resize/rotate gesture finishes.
    void cardGeometryChanged(const QString& memoId);
    /// Close button pressed; the memo itself is NOT deleted.
    void closeRequested(const QString& memoId);
    /// The card was pressed (bring to front / show handles).
    void activated(const QString& memoId);
    /// The little lock button was toggled (manager persists the preference and
    /// applies it to the other cards).
    void aspectLockToggled(bool locked);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    // Qt6 replaced QWidget::enterEvent(QEvent*) with enterEvent(QEnterEvent*);
    // Qt5 builds keep the old signature. Same guard as ToggleButton.h etc.
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent* event) override;
#else
    void enterEvent(QEvent* event) override;
#endif
    void leaveEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    enum class Handle {
        None,
        Move,
        Rotate,
        TopLeft,
        Top,
        TopRight,
        Left,
        Right,
        BottomLeft,
        Bottom,
        BottomRight,
        AspectLock,
        Close
    };

    /// Widget (local) coordinates of a rotated card corner.
    QPointF cardPoint(Handle handle) const;
    /// Widget (local) coordinates of the rotate handle.
    QPointF rotateHandlePos() const;
    /// Widget (local) coordinates of the close button centre.
    QPointF closeButtonPos() const;
    /// Widget (local) coordinates of the aspect-lock button centre.
    QPointF lockButtonPos() const;
    /// Which handle (if any) is at @p localPos.
    Handle handleAt(const QPointF& localPos) const;
    /// True when @p localPos is inside the rotated card.
    bool pointInCard(const QPointF& localPos) const;

    /// Recompute the widget geometry from m_center/m_cardSize/m_rotation.
    void updateWidgetGeometry();
    /// Rebuild the scaled image cache after a resize / dpr change.
    void invalidateScaledCache();
    /// Centre of the card in parent coordinates.
    QPointF cardCenter() const;
    /// Half diagonal of the card, used for hit-testing paddings.
    qreal cardRadius() const;

    void updateCursor(const QPointF& localPos);

    QString m_memoId;
    QString m_title;
    QPixmap m_image;
    QPixmap m_lockGlyph;         ///< White padlock used by the aspect-lock button
    QPixmap m_scaledCache;       ///< m_image scaled to the card size * dpr
    QSizeF  m_scaledCacheSize;   ///< Card size the cache was built for

    QPointF m_center;            ///< Card centre, parent coordinates
    QSizeF  m_cardSize = QSizeF(200.0, 150.0);
    qreal   m_rotation = 0.0;    ///< Degrees, clockwise-positive
    qreal   m_imageAspect = 4.0 / 3.0;  ///< Memo image width/height

    bool m_darkMode = false;
    bool m_active = false;
    bool m_hovered = false;
    bool m_aspectLocked = true;

    Handle m_activeHandle = Handle::None;
    QPointF m_pressParentPos;     ///< Press point, parent coordinates
    QPointF m_pressLocalPos;      ///< Press point, widget coordinates
    QRectF  m_pressCardRect;      ///< Card rect when the gesture started
    qreal   m_pressRotation = 0.0;
    qreal   m_pressRotateAngle = 0.0;

    QWidget* m_scrollTarget = nullptr;
};
