// ============================================================================
// ViewportScrollBar Implementation
// ============================================================================

#include "ViewportScrollBar.h"

#include "../../core/DarkModeUtils.h"
#include "../../compat/qt_compat.h"  // Qt5/Qt6 mouse position shims

#include <QPainter>
#include <QMouseEvent>
#include <QToolTip>
#include <QtGlobal>
#include <algorithm>

ViewportScrollBar::ViewportScrollBar(Qt::Orientation orientation,
                                     DockEdge edge,
                                     QWidget* parent)
    : QWidget(parent)
    , m_orientation(orientation)
    , m_edge(edge)
{
    // Overlay: transparent background, floats above the viewport content.
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);
    if (isVertical()) {
        setFixedWidth(barThickness());
    } else {
        setFixedHeight(barThickness());
    }
}

void ViewportScrollBar::setDockEdge(DockEdge edge)
{
    if (m_edge == edge) return;
    m_edge = edge;
    update();
}

void ViewportScrollBar::setDarkMode(bool dark)
{
    if (m_darkMode == dark) return;
    m_darkMode = dark;
    update();
}

void ViewportScrollBar::setFraction(qreal fraction)
{
    // Programmatic update from the viewport. Ignore while the user is
    // dragging so the viewport's echo cannot fight the drag.
    if (m_dragging) return;
    fraction = qBound(0.0, fraction, 1.0);
    if (qFuzzyCompare(fraction + 1.0, m_fraction + 1.0)) return;
    m_fraction = fraction;
    update();
}

void ViewportScrollBar::setHandleFraction(qreal frac)
{
    frac = qBound(0.0, frac, 1.0);
    if (qFuzzyCompare(frac + 1.0, m_handleFraction + 1.0)) return;
    m_handleFraction = frac;
    update();
}

void ViewportScrollBar::setAccentRegions(const QVector<AccentRegion>& regions)
{
    // Document-map is a page-axis concept; ignore on horizontal bars.
    if (!isVertical()) {
        if (!m_accents.isEmpty()) { m_accents.clear(); update(); }
        return;
    }
    m_accents = regions;
    update();
}

void ViewportScrollBar::setMarkers(const QVector<BarMarker>& markers)
{
    if (!isVertical()) {
        if (!m_markers.isEmpty()) { m_markers.clear(); update(); }
        return;
    }
    m_markers = markers;
    update();
}

void ViewportScrollBar::setSearchMarkers(const QVector<BarMarker>& markers)
{
    if (!isVertical()) {
        if (!m_searchMarkers.isEmpty()) { clearSearchMarkers(); }
        return;
    }
    m_searchMarkers = markers;
    // Sort by track position so the pixel-merge pass can coalesce neighbours
    // in a single linear walk.
    std::sort(m_searchMarkers.begin(), m_searchMarkers.end(),
              [](const BarMarker& a, const BarMarker& b) { return a.pos < b.pos; });
    m_searchMergedTrackLen = -1.0;  // invalidate the merge cache
    update();
}

void ViewportScrollBar::clearSearchMarkers()
{
    if (m_searchMarkers.isEmpty() && m_searchMerged.isEmpty()) return;
    m_searchMarkers.clear();
    m_searchMerged.clear();
    m_searchMergedTrackLen = -1.0;
    if (m_hoveredMarkerIsSearch) {
        m_hoveredMarker = -1;
        m_hoveredMarkerIsSearch = false;
    }
    update();
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

qreal ViewportScrollBar::trackLength() const
{
    const qreal len = isVertical() ? height() : width();
    return qMax(0.0, len - 2.0 * trackMargin());
}

qreal ViewportScrollBar::handleLengthPx() const
{
    const qreal track = trackLength();
    if (track <= 0.0) return 0.0;
    qreal len = m_handleFraction * track;
    // Clamp the minimum against the track: on a very short track (e.g. before the
    // widget has been laid out) kMinHandlePx can exceed the track, which would
    // make qBound's min > max and abort. Never let the min exceed the track.
    const qreal minLen = qMin(static_cast<qreal>(kMinHandlePx), track);
    len = qBound(minLen, len, track);
    return len;
}

qreal ViewportScrollBar::handleStartPx() const
{
    const qreal track = trackLength();
    const qreal handle = handleLengthPx();
    const qreal travel = qMax(0.0, track - handle);
    return trackMargin() + m_fraction * travel;
}

qreal ViewportScrollBar::handleCenterPx() const
{
    return handleStartPx() + handleLengthPx() / 2.0;
}

qreal ViewportScrollBar::posAlongAxis(const QPointF& p) const
{
    return isVertical() ? p.y() : p.x();
}

void ViewportScrollBar::setFractionFromUser(qreal fraction)
{
    fraction = qBound(0.0, fraction, 1.0);
    if (qFuzzyCompare(fraction + 1.0, m_fraction + 1.0)) return;
    m_fraction = fraction;
    update();
    emit fractionChanged(m_fraction);
}

qreal ViewportScrollBar::fractionToPx(qreal frac) const
{
    // Same trackLength() basis as the handle top, so a page marker lands where
    // the handle sits when that page reaches the top (see SB2 derivation).
    return trackMargin() + qBound(0.0, frac, 1.0) * trackLength();
}

int ViewportScrollBar::markerAtPos(qreal pos) const
{
    int best = -1;
    qreal bestDist = kMarkerHitBandPx;
    for (int i = 0; i < m_markers.size(); ++i) {
        const qreal mp = fractionToPx(m_markers[i].pos);
        const qreal d = qAbs(pos - mp);
        if (d <= bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

const QVector<ViewportScrollBar::BarMarker>& ViewportScrollBar::mergedSearchMarkers() const
{
    const qreal track = trackLength();
    // Rebuild only when the raw set changed (cache invalidated to -1) or the
    // track length changed (px spacing shifts on resize).
    if (m_searchMergedTrackLen == track) {
        return m_searchMerged;
    }
    m_searchMerged.clear();
    m_searchMergedTrackLen = track;
    if (m_searchMarkers.isEmpty()) {
        return m_searchMerged;
    }

    // m_searchMarkers is pre-sorted by pos; coalesce runs whose pixel positions
    // fall within kSearchMergeBandPx of the band's first tick.
    int i = 0;
    while (i < m_searchMarkers.size()) {
        const qreal bandStartPx = fractionToPx(m_searchMarkers[i].pos);
        BarMarker band = m_searchMarkers[i];
        int count = m_searchMarkers[i].matchCount;
        bool anyCurrent = m_searchMarkers[i].current;
        // The representative (jump target) is the current match if present in
        // the band, else the first tick (already in `band`).
        int j = i + 1;
        for (; j < m_searchMarkers.size(); ++j) {
            if (fractionToPx(m_searchMarkers[j].pos) - bandStartPx > kSearchMergeBandPx) {
                break;
            }
            count += m_searchMarkers[j].matchCount;
            if (m_searchMarkers[j].current) {
                anyCurrent = true;
                band = m_searchMarkers[j];  // prefer the current match as target
            }
        }
        band.matchCount = count;
        band.current = anyCurrent;
        band.tooltip = tr("%n match(es)", "", count);
        m_searchMerged.push_back(band);
        i = j;
    }
    return m_searchMerged;
}

int ViewportScrollBar::searchMarkerAtPos(qreal pos) const
{
    const QVector<BarMarker>& merged = mergedSearchMarkers();
    int best = -1;
    qreal bestDist = kMarkerHitBandPx;
    for (int i = 0; i < merged.size(); ++i) {
        const qreal mp = fractionToPx(merged[i].pos);
        const qreal d = qAbs(pos - mp);
        if (d <= bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

QColor ViewportScrollBar::legibleMarkerColor(const QColor& raw) const
{
    if (!raw.isValid()) {
        return m_darkMode ? QColor(210, 210, 210) : QColor(90, 90, 90);
    }
    // LinkObject's default icon color is a translucent mid-gray (100,100,100).
    // On the mid-gray track it disappears, so substitute a theme-legible gray.
    const bool nearDefaultGray =
        qAbs(raw.red()   - 100) <= 12 &&
        qAbs(raw.green() - 100) <= 12 &&
        qAbs(raw.blue()  - 100) <= 12;
    if (nearDefaultGray) {
        return m_darkMode ? QColor(210, 210, 210) : QColor(70, 70, 70);
    }
    QColor c = raw;
    c.setAlpha(255);  // ticks are opaque regardless of the source alpha
    return c;
}

// ---------------------------------------------------------------------------
// Theming
// ---------------------------------------------------------------------------

QColor ViewportScrollBar::trackColor() const
{
    return m_darkMode ? QColor(255, 255, 255, 24)
                      : QColor(0, 0, 0, 26);
}

QColor ViewportScrollBar::handleColor() const
{
    const bool active = m_dragging || m_handleHovered;
    if (m_darkMode) {
        return active ? QColor(235, 235, 235, 220) : QColor(200, 200, 200, 150);
    }
    return active ? QColor(70, 70, 70, 220) : QColor(100, 100, 100, 160);
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void ViewportScrollBar::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);

    // Lane layout (reserved for later plans):
    //   - background/track (drawn here in SB1)
    //   - source-accent stripe + thumbnail strip (SB2/SB3, behind the handle)
    //   - marker ticks (SB2, above the strip)
    //   - the drag handle (drawn here, floats above all lanes)

    // Track background spanning the full minor axis (same rect for either
    // orientation; only the rounding axis differs).
    const QRectF track(trackMargin(), trackMargin(),
                       width() - 2.0 * trackMargin(),
                       height() - 2.0 * trackMargin());
    const qreal trackRadius = (isVertical() ? track.width() : track.height()) / 2.0;
    p.setBrush(trackColor());
    p.drawRoundedRect(track, trackRadius, trackRadius);

    // SB2: per-source accent stripes — a thin band on the docked (far) edge,
    // behind the handle. Vertical/page-axis only.
    if (isVertical() && !m_accents.isEmpty()) {
        const qreal stripeW = 3.0;
        const bool rightEdge = (m_edge == DockEdge::Right);
        const qreal stripeX = rightEdge ? (width() - trackMargin() - stripeW)
                                         : trackMargin();
        for (const AccentRegion& r : m_accents) {
            if (!r.color.isValid()) continue;
            const qreal y0 = fractionToPx(r.start);
            const qreal y1 = fractionToPx(r.end);
            if (y1 <= y0) continue;
            QColor c = r.color;
            c.setAlpha(m_darkMode ? 190 : 170);
            p.setBrush(c);
            p.drawRoundedRect(QRectF(stripeX, y0, stripeW, y1 - y0), 1.0, 1.0);
        }
    }

    // Handle.
    const qreal start = handleStartPx();
    const qreal len = handleLengthPx();

    if (len > 0.0) {
        const qreal inset = 2.0;
        QRectF handle;
        if (isVertical()) {
            handle = QRectF(inset, start, qMax(0.0, width() - 2.0 * inset), len);
        } else {
            handle = QRectF(start, inset, len, qMax(0.0, height() - 2.0 * inset));
        }
        const qreal handleRadius = (isVertical() ? handle.width() : handle.height()) / 2.0;
        p.setBrush(handleColor());
        p.drawRoundedRect(handle, handleRadius, handleRadius);
    }

    // SB2: marker ticks on top of the handle so they stay visible. Thin,
    // near-full-width opaque ticks colored per link. Vertical only.
    if (isVertical() && !m_markers.isEmpty()) {
        const qreal tickInset = 1.5;
        const qreal tickX = tickInset;
        const qreal tickW = qMax(0.0, width() - 2.0 * tickInset);
        const qreal tickH = 3.0;
        for (const BarMarker& m : m_markers) {
            const qreal y = fractionToPx(m.pos) - tickH / 2.0;
            p.setBrush(legibleMarkerColor(m.color));
            p.drawRoundedRect(QRectF(tickX, y, tickW, tickH), 1.0, 1.0);
        }
    }

    // SBS3: search-hit ticks (amber), merged by pixel proximity, above the link
    // ticks. The current Next/Prev match is emphasized (brighter + taller).
    if (isVertical() && !m_searchMarkers.isEmpty()) {
        const QColor amber = DarkModeUtils::searchHitColor(m_darkMode);
        QColor normalAmber = amber;
        normalAmber.setAlpha(220);
        // Higher-contrast variant for the active match: lighten on the dark
        // track, deepen on the light track so it pops in either theme.
        QColor currentAmber = m_darkMode ? amber.lighter(125) : amber.darker(125);
        currentAmber.setAlpha(255);
        const qreal tickInset = 1.5;
        const qreal tickX = tickInset;
        const qreal tickW = qMax(0.0, width() - 2.0 * tickInset);
        for (const BarMarker& m : mergedSearchMarkers()) {
            const qreal tickH = m.current ? 5.0 : 3.0;
            const qreal y = fractionToPx(m.pos) - tickH / 2.0;
            p.setBrush(m.current ? currentAmber : normalAmber);
            p.drawRoundedRect(QRectF(tickX, y, tickW, tickH), 1.0, 1.0);
        }
    }
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------

void ViewportScrollBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    const qreal pos = posAlongAxis(SN_MOUSE_POS(event));
    const qreal start = handleStartPx();
    const qreal len = handleLengthPx();

    if (pos >= start && pos <= start + len) {
        // Handle drag wins over marker hits when the press is on the handle.
        m_dragging = true;
        m_dragGrabOffset = pos - start;
        update();
    } else if (const int si = searchMarkerAtPos(pos); si >= 0) {
        // SBS3: search ticks take priority over link ticks during an active
        // search. Reveal + select the (representative) match.
        const BarMarker& m = mergedSearchMarkers()[si];
        if (m.pageIndex >= 0) {
            emit searchMarkerActivated(m.pageIndex, m.normY, m.matchIndex);
        }
    } else if (const int mi = markerAtPos(pos); mi >= 0) {
        // SB2: click a marker tick to jump to its page.
        const int page = m_markers[mi].pageIndex;
        if (page >= 0) emit markerActivated(page);
    } else {
        // Track click: page toward the click (QScrollBar-like feel).
        const qreal page = qMax(m_handleFraction, 0.1);
        const qreal delta = (pos < start) ? -page : page;
        setFractionFromUser(m_fraction + delta);
    }
    event->accept();
}

void ViewportScrollBar::mouseMoveEvent(QMouseEvent* event)
{
    const qreal pos = posAlongAxis(SN_MOUSE_POS(event));

    if (m_dragging) {
        const qreal track = trackLength();
        const qreal handle = handleLengthPx();
        const qreal travel = qMax(0.0, track - handle);
        if (travel > 0.0) {
            const qreal newStart = pos - m_dragGrabOffset;
            setFractionFromUser((newStart - trackMargin()) / travel);
        }
        event->accept();
        return;
    }

    // Hover feedback on the handle.
    const qreal start = handleStartPx();
    const qreal len = handleLengthPx();
    const bool hovered = (pos >= start && pos <= start + len);
    if (hovered != m_handleHovered) {
        m_handleHovered = hovered;
        update();
    }

    // SB2/SBS3: marker tooltip (only when not over the handle). Search ticks
    // resolve first, then link ticks. Debounced on the (channel, index) pair so
    // we don't re-fire QToolTip on every move.
    int si = hovered ? -1 : searchMarkerAtPos(pos);
    int mi = (hovered || si >= 0) ? -1 : markerAtPos(pos);
    const bool isSearch = (si >= 0);
    const int idx = isSearch ? si : mi;
    if (idx != m_hoveredMarker || isSearch != m_hoveredMarkerIsSearch) {
        m_hoveredMarker = idx;
        m_hoveredMarkerIsSearch = isSearch;
        QString tip;
        if (isSearch) {
            tip = mergedSearchMarkers()[si].tooltip;
        } else if (mi >= 0) {
            tip = m_markers[mi].tooltip;
        }
        if (idx >= 0 && !tip.isEmpty()) {
            QToolTip::showText(SN_MOUSE_GLOBAL_POS(event), tip, this);
        } else {
            QToolTip::hideText();
        }
    }
    QWidget::mouseMoveEvent(event);
}

void ViewportScrollBar::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_dragging && event->button() == Qt::LeftButton) {
        m_dragging = false;
        update();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ViewportScrollBar::leaveEvent(QEvent* event)
{
    if (m_handleHovered) {
        m_handleHovered = false;
        update();
    }
    m_hoveredMarker = -1;
    m_hoveredMarkerIsSearch = false;
    QWidget::leaveEvent(event);
}
