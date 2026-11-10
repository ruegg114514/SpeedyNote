#include "MemoFloatWidget.h"

#include "../compat/qt_compat.h"      // SN_MOUSE_POS: Qt5 localPos() / Qt6 position()

#include <QCoreApplication>
#include <QCursor>
#include <QEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSizeF>
#include <QString>
#include <QWheelEvent>
#include <QtMath>

// ============================================================================
// MemoFloatWidget.cpp - floating memo card geometry, painting and gestures
// ============================================================================
// Geometry is kept in two spaces, and every function below states which one it
// works in:
//   * PARENT space  - m_center/m_cardSize/m_rotation describe the unrotated
//                     card rect in the overlay's (viewport's) coordinates.
//   * WIDGET space  - what paintEvent/mouse events see. The widget rect is the
//                     axis-aligned bounding box of the rotated card plus PAD,
//                     so parent <-> widget conversion is always a subtraction
//                     of geometry().topLeft() (see the centerLocal locals
//                     below). Painting then rotates the painter around the
//                     card centre, which gives a third, card-local space
//                     (-w/2..w/2, -h/2..h/2) that is never rotated.
//
// The rotation/resize math deliberately mirrors
// DocumentViewport::objectHandleAtPoint() / updateObjectResize() so a card
// behaves like any other rotated object in the app (same 15 degree snap, same
// rotated handle layout).
// ============================================================================

namespace {

/// Margin around the rotated card inside the widget rect. It has to cover the
/// drop shadow (5 px + 3 px offset) and the half of the resize handles that sits
/// outside the card edge, nothing more: every padding pixel is a click that
/// would not reach the page underneath.
constexpr qreal PAD = 9.0;

/// Inset of the close button centre from the card's top and right edges.
constexpr qreal CLOSE_BUTTON_INSET = 14.0;

/// Corner radius shared by the card body and the image clip.
constexpr qreal CARD_RADIUS = 8.0;

/// Rotation snap used by the canvas for rotated objects, in degrees.
constexpr qreal ROTATION_SNAP = 15.0;

/// Distance kept between the card and the edge of the parent when dragging.
constexpr qreal PARENT_EDGE_MARGIN = 4.0;

/**
 * @brief Rotate a vector by @p degrees.
 *
 * Clockwise-positive, matching the app convention (screen y points down, so
 * the standard QTransform rotation matrix is clockwise on screen). Mirrors the
 * inline rotatePoint() lambdas used in DocumentViewport.
 */
QPointF rotateVector(const QPointF& v, qreal degrees)
{
    if (qFuzzyIsNull(degrees)) {
        return v;
    }
    const qreal radians = qDegreesToRadians(degrees);
    const qreal c = qCos(radians);
    const qreal s = qSin(radians);
    return QPointF(v.x() * c - v.y() * s, v.x() * s + v.y() * c);
}

} // namespace

// ============================================================================
// Construction / stored state
// ============================================================================

MemoFloatWidget::MemoFloatWidget(const QString& memoId, const QPixmap& image,
                                 QWidget* parent)
    : QWidget(parent)
    , m_memoId(memoId)
    , m_image(image)
{
    // A card must never take keyboard focus: clicking one would otherwise eat
    // the canvas shortcuts (Ctrl+Z, tool keys). See the header contract.
    setFocusPolicy(Qt::NoFocus);

    // Hover cursors over the handles need move events without a press.
    setMouseTracking(true);

    // "Drag me" affordance over the card body; updateCursor() refines it as
    // soon as the pointer is over a handle.
    setCursor(Qt::OpenHandCursor);

    // The widget geometry and the scaled cache are derived from m_cardSize and
    // the parent size, neither of which exists yet: the overlay calls
    // applyStoredGeometry() right after construction, and the first
    // paintEvent/resizeEvent builds the cache.
}

QRectF MemoFloatWidget::cardRect() const
{
    // The card is the UNROTATED rect in parent coordinates; the rotation is
    // applied by the painter, never stored in the rect.
    const QPointF topLeft(m_center.x() - m_cardSize.width() / 2.0,
                          m_center.y() - m_cardSize.height() / 2.0);
    return QRectF(topLeft, m_cardSize);
}

void MemoFloatWidget::applyStoredGeometry(const QRectF& rect, qreal rotation)
{
    // Clamp the CARD size, not the widget rect: the widget rect is derived
    // from the card, so a stored rect that is too small/large only has to be
    // corrected once, here.
    const qreal width = qBound<qreal>(MIN_CARD_SIZE, rect.width(), MAX_CARD_SIZE);
    const qreal height = qBound<qreal>(MIN_CARD_SIZE, rect.height(), MAX_CARD_SIZE);

    // Keep the stored centre as-is. Deriving the centre from a clamped rect
    // instead would slide a minimum-size card towards its old corner.
    m_center = rect.center();
    m_cardSize = QSizeF(width, height);
    m_rotation = rotation;

    updateWidgetGeometry();
    invalidateScaledCache();

    // Deliberately silent: restoring stored geometry is not a user edit, so
    // cardGeometryChanged() must not fire (otherwise every open would mark the
    // notebook dirty).
}

void MemoFloatWidget::setTitle(const QString& title)
{
    m_title = title;
    // The card paints no text, so the title is only reachable as a tooltip.
    setToolTip(title);
}

void MemoFloatWidget::setDarkMode(bool darkMode)
{
    if (m_darkMode == darkMode) {
        return;
    }
    m_darkMode = darkMode;
    update();
}

void MemoFloatWidget::setActiveCard(bool active)
{
    m_active = active;
    if (!m_active) {
        // A deactivated card keeps no gesture state: the press that started a
        // resize may have been stolen by another card becoming active.
        m_activeHandle = Handle::None;
        setCursor(Qt::OpenHandCursor);
    }
    update();
}

// ============================================================================
// Geometry model
// ============================================================================

QPointF MemoFloatWidget::cardCenter() const
{
    return m_center;
}

qreal MemoFloatWidget::cardRadius() const
{
    // Half diagonal of the unrotated card. The rotated card never reaches
    // further than this from its centre.
    return 0.5 * qSqrt(m_cardSize.width() * m_cardSize.width()
                       + m_cardSize.height() * m_cardSize.height());
}

void MemoFloatWidget::updateWidgetGeometry()
{
    const QPointF halfSize(m_cardSize.width() / 2.0, m_cardSize.height() / 2.0);
    const QPointF localCorners[4] = {
        QPointF(-halfSize.x(), -halfSize.y()),   // top left
        QPointF(halfSize.x(), -halfSize.y()),    // top right
        QPointF(halfSize.x(), halfSize.y()),     // bottom right
        QPointF(-halfSize.x(), halfSize.y())     // bottom left
    };

    // Parent-space points the widget rect has to cover: the 4 rotated corners,
    // the rotation handle (ROTATE_HANDLE_OFFSET above the rotated top edge) and
    // the close button (inside the rotated top-right corner).
    QPointF points[6];
    for (int i = 0; i < 4; ++i) {
        points[i] = m_center + rotateVector(localCorners[i], m_rotation);
    }
    points[4] = m_center + rotateVector(
        QPointF(0.0, -halfSize.y() - ROTATE_HANDLE_OFFSET), m_rotation);
    points[5] = m_center + rotateVector(
        QPointF(halfSize.x() - CLOSE_BUTTON_INSET,
                -halfSize.y() + CLOSE_BUTTON_INSET), m_rotation);

    qreal minX = points[0].x();
    qreal maxX = minX;
    qreal minY = points[0].y();
    qreal maxY = minY;
    for (int i = 1; i < 6; ++i) {
        minX = qMin(minX, points[i].x());
        maxX = qMax(maxX, points[i].x());
        minY = qMin(minY, points[i].y());
        maxY = qMax(maxY, points[i].y());
    }

    const QRectF bounds = QRectF(QPointF(minX, minY), QPointF(maxX, maxY))
                              .adjusted(-PAD, -PAD, PAD, PAD);

    // toAlignedRect() grows the rect to whole pixels. Painting and hit testing
    // convert between spaces by subtracting geometry().topLeft(), so a
    // fractional widget origin would blur the card by up to half a pixel (and
    // make the handles feel off by the same amount at high DPR).
    setGeometry(bounds.toAlignedRect());
}

QPointF MemoFloatWidget::cardPoint(Handle handle) const
{
    const QPointF centerLocal = cardCenter() - QPointF(geometry().topLeft());
    const qreal halfW = m_cardSize.width() / 2.0;
    const qreal halfH = m_cardSize.height() / 2.0;

    QPointF offset;
    switch (handle) {
    case Handle::TopLeft:
        offset = QPointF(-halfW, -halfH);
        break;
    case Handle::Top:
        offset = QPointF(0.0, -halfH);
        break;
    case Handle::TopRight:
        offset = QPointF(halfW, -halfH);
        break;
    case Handle::Left:
        offset = QPointF(-halfW, 0.0);
        break;
    case Handle::Right:
        offset = QPointF(halfW, 0.0);
        break;
    case Handle::BottomLeft:
        offset = QPointF(-halfW, halfH);
        break;
    case Handle::Bottom:
        offset = QPointF(0.0, halfH);
        break;
    case Handle::BottomRight:
        offset = QPointF(halfW, halfH);
        break;
    case Handle::Rotate:
        return rotateHandlePos();
    case Handle::Close:
        return closeButtonPos();
    case Handle::Move:
    case Handle::None:
        return centerLocal;
    }
    return centerLocal + rotateVector(offset, m_rotation);
}

QPointF MemoFloatWidget::rotateHandlePos() const
{
    const QPointF centerLocal = cardCenter() - QPointF(geometry().topLeft());
    const qreal halfH = m_cardSize.height() / 2.0;
    // Straight "up" in card space is (0, -1); rotating that vector keeps the
    // handle above the card's top edge for any rotation.
    return centerLocal
           + rotateVector(QPointF(0.0, -halfH - ROTATE_HANDLE_OFFSET), m_rotation);
}

QPointF MemoFloatWidget::closeButtonPos() const
{
    const QPointF centerLocal = cardCenter() - QPointF(geometry().topLeft());
    const qreal halfW = m_cardSize.width() / 2.0;
    const qreal halfH = m_cardSize.height() / 2.0;
    return centerLocal + rotateVector(
        QPointF(halfW - CLOSE_BUTTON_INSET, -halfH + CLOSE_BUTTON_INSET),
        m_rotation);
}

// ============================================================================
// Hit testing
// ============================================================================

bool MemoFloatWidget::pointInCard(const QPointF& localPos) const
{
    const QPointF centerLocal = cardCenter() - QPointF(geometry().topLeft());
    // Undo the card rotation to compare against the plain card rect. This is
    // the inverse of what the painter does, so a point under the pointer is a
    // point on the painted image.
    const QPointF cardLocal = rotateVector(localPos - centerLocal, -m_rotation);
    const QRectF rect(-m_cardSize.width() / 2.0, -m_cardSize.height() / 2.0,
                      m_cardSize.width(), m_cardSize.height());
    return rect.contains(cardLocal);
}

MemoFloatWidget::Handle MemoFloatWidget::handleAt(const QPointF& localPos) const
{
    // The close button is tested first: it sits inside the top-right corner,
    // where the corner resize handle (whose hit radius reaches into the card)
    // would otherwise win, making the close button unreachable.
    if (QLineF(localPos, closeButtonPos()).length() <= CLOSE_BUTTON_SIZE / 2.0 + 4.0) {
        return Handle::Close;
    }

    // Rotation handle next: it lives outside the card, but in a crowded corner
    // it can overlap a resize handle's hit area, and rotating is the gesture
    // users expect when grabbing the handle they can see.
    if (QLineF(localPos, rotateHandlePos()).length() <= HANDLE_HIT_SIZE / 2.0) {
        return Handle::Rotate;
    }

    // Compare against the ALREADY ROTATED handle positions (cardPoint() does
    // the rotation) instead of rotating the pointer into card space: for a
    // point test the two are equivalent, and this form is the same shape as
    // DocumentViewport::objectHandleAtPoint().
    static const Handle resizeHandles[8] = {
        Handle::TopLeft, Handle::Top, Handle::TopRight,
        Handle::Left, Handle::Right,
        Handle::BottomLeft, Handle::Bottom, Handle::BottomRight
    };
    const qreal hitRadius = HANDLE_HIT_SIZE / 2.0;
    for (int i = 0; i < 8; ++i) {
        if (QLineF(localPos, cardPoint(resizeHandles[i])).length() <= hitRadius) {
            return resizeHandles[i];
        }
    }

    return pointInCard(localPos) ? Handle::Move : Handle::None;
}

void MemoFloatWidget::updateCursor(const QPointF& localPos)
{
    switch (handleAt(localPos)) {
    case Handle::Close:
        setCursor(Qt::PointingHandCursor);
        break;
    case Handle::Rotate:
        setCursor(Qt::CrossCursor);
        break;
    case Handle::Move:
        // The body is a drag handle; ClosedHandCursor is set while dragging.
        setCursor(Qt::OpenHandCursor);
        break;
    case Handle::TopLeft:
    case Handle::BottomRight:
        setCursor(Qt::SizeFDiagCursor);
        break;
    case Handle::TopRight:
    case Handle::BottomLeft:
        setCursor(Qt::SizeBDiagCursor);
        break;
    case Handle::Top:
    case Handle::Bottom:
        setCursor(Qt::SizeVerCursor);
        break;
    case Handle::Left:
    case Handle::Right:
        setCursor(Qt::SizeHorCursor);
        break;
    case Handle::None:
        // Transparent padding around the card: clicks go through to the page.
        setCursor(Qt::ArrowCursor);
        break;
    }
}

// ============================================================================
// Painting
// ============================================================================

void MemoFloatWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QPointF centerLocal = cardCenter() - QPointF(geometry().topLeft());
    const qreal halfW = m_cardSize.width() / 2.0;
    const qreal halfH = m_cardSize.height() / 2.0;
    const QRectF cardRect(-halfW, -halfH, m_cardSize.width(), m_cardSize.height());

    painter.save();
    // Everything below this point is drawn in card-local coordinates.
    painter.translate(centerLocal);
    painter.rotate(m_rotation);

    // --- Drop shadow -------------------------------------------------------
    // Three concentric rounded rects instead of a blur: a few flat fills that
    // are cheap enough to repaint on every drag frame of a low-end tablet, and
    // the 20px pad guarantees they never reach the widget edge.
    // {light, dark} - a dark page needs a denser shadow to stay visible.
    static const int shadowAlphas[3][2] = {{40, 74}, {25, 46}, {15, 28}};
    painter.setPen(Qt::NoPen);
    for (int i = 0; i < 3; ++i) {
        const qreal grow = 1.0 + 2.0 * i;
        painter.setBrush(QColor(0, 0, 0, shadowAlphas[i][m_darkMode ? 1 : 0]));
        painter.drawRoundedRect(
            cardRect.adjusted(-grow, -grow, grow, grow).translated(0.0, 3.0),
            10.0, 10.0);
    }

    // --- Card body and border ---------------------------------------------
    const QColor backgroundColor =
        m_darkMode ? QColor(0x2B, 0x2B, 0x2B) : QColor(0xFF, 0xFF, 0xFF);
    const QColor borderColor = m_active
        ? QColor(0x00, 0x78, 0xD4)   // app accent, same blue as the sidebar
        : (m_darkMode ? QColor(255, 255, 255, 60) : QColor(0, 0, 0, 55));
    const qreal borderWidth = m_active ? 2.0 : 1.0;
    // Half-pixel inset keeps a 1px border crisp while antialiasing is on.
    const qreal borderInset = borderWidth / 2.0;
    painter.setPen(QPen(borderColor, borderWidth));
    painter.setBrush(backgroundColor);
    painter.drawRoundedRect(
        cardRect.adjusted(borderInset, borderInset, -borderInset, -borderInset),
        CARD_RADIUS, CARD_RADIUS);

    // --- Memo image --------------------------------------------------------
    invalidateScaledCache();   // no-op unless the card size or the dpr changed
    painter.save();
    QPainterPath clip;
    clip.addRoundedRect(cardRect, CARD_RADIUS, CARD_RADIUS);
    painter.setClipPath(clip);
    if (!m_scaledCache.isNull()) {
        // Explicit target/source rects: the cache is normally cardSize * dpr,
        // but it is capped at a maximum side, so the source may be smaller than
        // the card and must be stretched over the whole card rect.
        painter.drawPixmap(cardRect, m_scaledCache,
                           QRectF(m_scaledCache.rect()));
    } else {
        painter.fillRect(cardRect,
                         m_darkMode ? QColor(0x3A, 0x3A, 0x3A)
                                    : QColor(0xF0, 0xF0, 0xF0));
        painter.setPen(m_darkMode ? QColor(0x9E, 0x9E, 0x9E)
                                  : QColor(0x66, 0x66, 0x66));
        painter.drawText(cardRect, Qt::AlignCenter, tr("Missing image"));
    }
    painter.restore();

    // --- Close button ------------------------------------------------------
    // Always visible: a card with no obvious way to dismiss it reads as stuck.
    const QPointF closeCenter(halfW - CLOSE_BUTTON_INSET,
                              -halfH + CLOSE_BUTTON_INSET);
    const qreal closeRadius = CLOSE_BUTTON_SIZE / 2.0;
    int closeAlpha = m_darkMode ? 150 : 90;
    if (m_hovered) {
        closeAlpha = qMin(255, closeAlpha + 40);   // subtle "I am clickable"
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, closeAlpha));
    painter.drawEllipse(closeCenter, closeRadius, closeRadius);

    const qreal crossArm = closeRadius * 0.45;
    // Named colour: "QPen pen(QColor(Qt::white));" would parse as a function
    // declaration (most vexing parse).
    const QColor crossColor(Qt::white);
    QPen crossPen(crossColor);
    crossPen.setWidthF(1.6);
    crossPen.setCapStyle(Qt::RoundCap);
    painter.setPen(crossPen);
    painter.drawLine(closeCenter + QPointF(-crossArm, -crossArm),
                     closeCenter + QPointF(crossArm, crossArm));
    painter.drawLine(closeCenter + QPointF(crossArm, -crossArm),
                     closeCenter + QPointF(-crossArm, crossArm));

    // --- Handles (active card only) ---------------------------------------
    if (m_active) {
        // Drawn in card-local space, i.e. they rotate with the card, exactly
        // like the object handles in DocumentViewport::renderObjectSelection().
        const QPointF handlePoints[8] = {
            QPointF(-halfW, -halfH), QPointF(0.0, -halfH), QPointF(halfW, -halfH),
            QPointF(-halfW, 0.0),    QPointF(halfW, 0.0),
            QPointF(-halfW, halfH),  QPointF(0.0, halfH),  QPointF(halfW, halfH)
        };
        const qreal halfHandle = HANDLE_VISUAL_SIZE / 2.0;
        painter.setPen(QPen(QColor(0, 0, 0, 160), 1.0));
        painter.setBrush(QColor(Qt::white));
        for (int i = 0; i < 8; ++i) {
            painter.drawRect(QRectF(handlePoints[i].x() - halfHandle,
                                    handlePoints[i].y() - halfHandle,
                                    HANDLE_VISUAL_SIZE, HANDLE_VISUAL_SIZE));
        }

        // Rotation handle: a circle above the top edge on a short stalk.
        const QPointF topCenter(0.0, -halfH);
        const QPointF rotateCenter(0.0, -halfH - ROTATE_HANDLE_OFFSET);
        painter.setPen(QPen(QColor(0, 0, 0, 160), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(topCenter, rotateCenter);
        painter.setBrush(QColor(Qt::white));
        painter.drawEllipse(rotateCenter, halfHandle, halfHandle);
    }

    painter.restore();   // back to widget coordinates
}

void MemoFloatWidget::invalidateScaledCache()
{
    if (m_image.isNull()) {
        m_scaledCache = QPixmap();
        m_scaledCacheSize = QSizeF();
        return;
    }

    const qreal dpr = qMax<qreal>(1.0, devicePixelRatioF());

    // Rebuilding costs a smooth rescale of the full-resolution memo image, and
    // the widget rect changes on EVERY move (the bounding box moves with it),
    // so only a card-size or dpr change may invalidate the cache.
    const bool cacheIsFresh = !m_scaledCache.isNull()
                              && m_scaledCacheSize == m_cardSize
                              && qFuzzyCompare(m_scaledCache.devicePixelRatio(), dpr);
    if (cacheIsFresh) {
        return;
    }

    const QSize deviceSize(qMax(1, qRound(m_cardSize.width() * dpr)),
                           qMax(1, qRound(m_cardSize.height() * dpr)));
    // Bound the cache independently of the card size: a card dragged to its
    // maximum on a HiDPI screen would otherwise allocate an enormous pixmap.
    // The pixmap is stretched to the card rect at paint time, so the cap only
    // costs sharpness at absurd sizes.
    constexpr int MAX_CACHE_SIDE = 2560;
    QSize cacheSize = deviceSize;
    if (cacheSize.width() > MAX_CACHE_SIDE || cacheSize.height() > MAX_CACHE_SIDE) {
        cacheSize.scale(MAX_CACHE_SIDE, MAX_CACHE_SIDE, Qt::KeepAspectRatio);
        cacheSize = cacheSize.expandedTo(QSize(1, 1));
    }
    // The card clips to a rounded rect of exactly m_cardSize, so the image is
    // stretched to fill it rather than letterboxed (naturalCardSize() already
    // gave the card the image's aspect ratio at creation time).
    m_scaledCache = m_image.scaled(cacheSize, Qt::IgnoreAspectRatio,
                                   Qt::SmoothTransformation);
    m_scaledCache.setDevicePixelRatio(dpr);
    m_scaledCacheSize = m_cardSize;
}

// ============================================================================
// Interaction
// ============================================================================

void MemoFloatWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    const QPointF localPos = SN_MOUSE_POS(event);
    const Handle handle = handleAt(localPos);

    if (handle == Handle::Close) {
        // Closing only hides the card: deleting the memo is the gallery's job.
        emit closeRequested(m_memoId);
        event->accept();
        return;
    }

    if (handle == Handle::None) {
        // Transparent padding around the card (shadow area): let the page have
        // the click so drawing/selecting still works next to a card.
        event->ignore();
        return;
    }

    // Any press on the card makes it the active card (handles shown, raised).
    // The signal goes out before the local state changes so the overlay can
    // deactivate the other cards from its handler without fighting us; the
    // local setActiveCard() below is idempotent if the overlay already did it.
    emit activated(m_memoId);
    setActiveCard(true);
    raise();

    m_activeHandle = handle;
    m_pressParentPos = mapToParent(localPos.toPoint());
    m_pressLocalPos = localPos;
    m_pressCardRect = cardRect();
    m_pressRotation = m_rotation;
    m_pressRotateAngle = qRadiansToDegrees(
        qAtan2(m_pressParentPos.y() - m_center.y(),
               m_pressParentPos.x() - m_center.x()));

    if (handle == Handle::Move) {
        setCursor(Qt::ClosedHandCursor);
    }
    event->accept();
}

void MemoFloatWidget::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF localPos = SN_MOUSE_POS(event);

    if (m_activeHandle == Handle::None) {
        updateCursor(localPos);
        // Keep the page from reacting under the card (hover previews, cursor
        // changes): the card is on top, so it owns the pointer.
        event->accept();
        return;
    }

    // All gesture math runs in parent space: the press point and the current
    // point are both parent coordinates, so the delta is independent of where
    // the widget rect happens to sit after the last setGeometry().
    const QPointF parentPos = mapToParent(localPos.toPoint());
    const QPointF parentDelta = parentPos - m_pressParentPos;

    switch (m_activeHandle) {
    case Handle::Move: {
        QPointF newCenter = m_pressCardRect.center() + parentDelta;
        if (const QWidget* parent = parentWidget()) {
            // Keep the whole card on the page. If the card is wider/taller than
            // the page, centring that axis is the only stable answer: clamping
            // both edges would make the card jitter against them.
            const QRectF parentRect =
                QRectF(parent->rect())
                    .adjusted(PARENT_EDGE_MARGIN, PARENT_EDGE_MARGIN,
                              -PARENT_EDGE_MARGIN, -PARENT_EDGE_MARGIN);
            const auto clampAxis = [](qreal value, qreal halfExtent,
                                      qreal min, qreal max) {
                if (2.0 * halfExtent > max - min) {
                    return (min + max) / 2.0;
                }
                return qBound(min + halfExtent, value, max - halfExtent);
            };
            newCenter.setX(clampAxis(newCenter.x(), m_cardSize.width() / 2.0,
                                     parentRect.left(), parentRect.right()));
            newCenter.setY(clampAxis(newCenter.y(), m_cardSize.height() / 2.0,
                                     parentRect.top(), parentRect.bottom()));
        }
        m_center = newCenter;
        updateWidgetGeometry();
        update();
        break;
    }

    case Handle::Rotate: {
        // Angle from the card centre to the pointer, +90 because the handle
        // starts at 12 o'clock (same as updateObjectResize()).
        qreal angle = qRadiansToDegrees(
            qAtan2(parentPos.y() - m_center.y(), parentPos.x() - m_center.x()))
            + 90.0;
        while (angle < 0.0) {
            angle += 360.0;
        }
        while (angle >= 360.0) {
            angle -= 360.0;
        }
        m_rotation = qRound(angle / ROTATION_SNAP) * ROTATION_SNAP;
        if (m_rotation >= 360.0) {
            m_rotation -= 360.0;
        }
        updateWidgetGeometry();
        update();
        break;
    }

    case Handle::TopLeft:
    case Handle::Top:
    case Handle::TopRight:
    case Handle::Left:
    case Handle::Right:
    case Handle::BottomLeft:
    case Handle::Bottom:
    case Handle::BottomRight: {
        // Sign of the handle in card space: -1 = left/top edge, +1 = right/
        // bottom edge, 0 = the axis this handle does not move.
        int signX = 0;
        int signY = 0;
        switch (m_activeHandle) {
        case Handle::TopLeft:
            signX = -1;
            signY = -1;
            break;
        case Handle::Top:
            signY = -1;
            break;
        case Handle::TopRight:
            signX = 1;
            signY = -1;
            break;
        case Handle::Left:
            signX = -1;
            break;
        case Handle::Right:
            signX = 1;
            break;
        case Handle::BottomLeft:
            signX = -1;
            signY = 1;
            break;
        case Handle::Bottom:
            signY = 1;
            break;
        case Handle::BottomRight:
            signX = 1;
            signY = 1;
            break;
        default:
            break;
        }

        // Convert the parent-space drag into the card's own axes, so a rotated
        // card grows along its edges instead of the screen axes.
        const QPointF localDelta = rotateVector(parentDelta, -m_pressRotation);
        const qreal dragX = (signX != 0) ? localDelta.x() : 0.0;
        const qreal dragY = (signY != 0) ? localDelta.y() : 0.0;

        const qreal pressWidth = m_pressCardRect.width();
        const qreal pressHeight = m_pressCardRect.height();
        const qreal newWidth =
            qBound(MIN_CARD_SIZE, pressWidth + signX * dragX, MAX_CARD_SIZE);
        const qreal newHeight =
            qBound(MIN_CARD_SIZE, pressHeight + signY * dragY, MAX_CARD_SIZE);

        // Re-derive the shift from the CLAMPED sizes: once the pointer passes
        // MIN/MAX_CARD_SIZE the card must stop moving, not drift away from the
        // pointer. Moving the centre by half the size delta keeps the opposite
        // edge/corner visually anchored.
        const QPointF localShift(
            (signX != 0) ? signX * (newWidth - pressWidth) / 2.0 : 0.0,
            (signY != 0) ? signY * (newHeight - pressHeight) / 2.0 : 0.0);

        m_cardSize = QSizeF(newWidth, newHeight);
        m_center = m_pressCardRect.center()
                   + rotateVector(localShift, m_pressRotation);
        updateWidgetGeometry();
        update();
        break;
    }

    case Handle::None:
    case Handle::Close:
        // Unreachable: both return early above / on press.
        break;
    }

    event->accept();
}

void MemoFloatWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || m_activeHandle == Handle::None) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    m_activeHandle = Handle::None;

    // Persist the rotation normalised to [0, 360): a few clockwise turns would
    // otherwise store 720 degrees (or -30 after two counter-clockwise turns),
    // and every reader of document.json would have to normalise it again.
    while (m_rotation < 0.0) {
        m_rotation += 360.0;
    }
    while (m_rotation >= 360.0) {
        m_rotation -= 360.0;
    }
    updateWidgetGeometry();

    // Refresh the hover cursor for where the pointer actually ended up: after
    // a clamped drag/resize the pointer can sit outside the card again.
    updateCursor(SN_MOUSE_POS(event));

    emit cardGeometryChanged(m_memoId);
    event->accept();
}

void MemoFloatWidget::wheelEvent(QWheelEvent* event)
{
    if (!m_scrollTarget) {
        // No page wired up: never eat the scroll.
        event->ignore();
        return;
    }

    // Re-target the wheel at the scroll target (the active viewport) so the
    // page keeps scrolling under the card. The cross-version constructor
    // (Qt 5.12+ and Qt 6) is used because the Qt5 QWheelEvent constructors
    // with a Qt::ScrollPhase are not what Qt6 offers any more.
    const QPointF globalPos = event->globalPosition();
    const QPointF targetPos = m_scrollTarget->mapFromGlobal(globalPos.toPoint());

    QWheelEvent forwarded(targetPos, globalPos,
                          event->pixelDelta(), event->angleDelta(),
                          event->buttons(), event->modifiers(),
                          event->phase(), event->inverted(), event->source());
    QCoreApplication::sendEvent(m_scrollTarget, &forwarded);

    // Accepted: the page consumed the wheel, so it must not bubble further up
    // the parent chain (that would scroll twice).
    event->accept();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void MemoFloatWidget::enterEvent(QEnterEvent* event)
#else
void MemoFloatWidget::enterEvent(QEvent* event)
#endif
{
    // Only a hover flag: the close button is painted slightly denser on hover.
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void MemoFloatWidget::leaveEvent(QEvent* event)
{
    m_hovered = false;
    update();
    QWidget::leaveEvent(event);
}

void MemoFloatWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    // The widget rect only changes when the card size, rotation or dpr
    // changed, so this rebuilds the cache on a resize/dpr change and does
    // nothing for a plain move (invalidateScaledCache() checks staleness).
    invalidateScaledCache();
    update();
}

// ============================================================================
// Helpers
// ============================================================================

QSizeF MemoFloatWidget::naturalCardSize(const QSize& imageSize, qreal maxEdge)
{
    if (imageSize.width() <= 0 || imageSize.height() <= 0) {
        return QSizeF(MIN_CARD_SIZE, MIN_CARD_SIZE);
    }

    // Fit inside maxEdge on both axes, keeping the aspect ratio. Never upscale:
    // a tiny memo must not become a blurry full-size card.
    const qreal scale = qMin<qreal>(
        1.0, qMin(maxEdge / imageSize.width(), maxEdge / imageSize.height()));

    QSizeF size(imageSize.width() * scale, imageSize.height() * scale);

    // Degenerate aspect ratios (a 2000x8 lasso strip) would otherwise collapse
    // to an unusable card, so each axis is raised to the minimum on its own.
    size.setWidth(qMax(size.width(), MIN_CARD_SIZE));
    size.setHeight(qMax(size.height(), MIN_CARD_SIZE));
    return size;
}
