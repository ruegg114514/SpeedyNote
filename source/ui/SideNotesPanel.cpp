#include "SideNotesPanel.h"
#include <QPainter>
#include <QPainterPath>
#include <QTabletEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QtMath>

SideNotesPanel::SideNotesPanel(QWidget *parent)
    : QWidget(parent)
{
    setMinimumWidth(200);
    setMaximumWidth(500);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_AcceptTouchEvents, false);
    setFocusPolicy(Qt::StrongFocus);
}

SideNotesPanel::~SideNotesPanel()
{
    if (m_dirty) {
        saveNotes();
    }
}

// ===== Page synchronization =====

void SideNotesPanel::setCurrentPage(int pageIndex)
{
    if (m_currentPage != pageIndex) {
        // Auto-save current page strokes if dirty
        if (m_dirty) {
            saveNotes();
        }
        m_currentPage = pageIndex;
        update();
    }
}

// ===== Scroll synchronization =====

void SideNotesPanel::setScrollOffset(qreal offsetY)
{
    m_scrollOffsetY = offsetY;
    update();
}

void SideNotesPanel::setViewportZoom(qreal zoom)
{
    m_zoomLevel = zoom;
    update();
}

// ===== Tool settings =====

void SideNotesPanel::setCurrentColor(const QColor &color)
{
    m_penColor = color;
}

void SideNotesPanel::setCurrentThickness(qreal thickness)
{
    m_penThickness = thickness;
}

void SideNotesPanel::setToolType(ToolType tool)
{
    // Only accept drawing tools
    if (tool == ToolType::Pen || tool == ToolType::Marker ||
        tool == ToolType::Highlighter || tool == ToolType::Eraser) {
        m_currentTool = tool;
    }
}

// ===== Content management =====

void SideNotesPanel::clearCurrentPage()
{
    if (m_pageStrokes.contains(m_currentPage) && !m_pageStrokes[m_currentPage].isEmpty()) {
        m_pageStrokes[m_currentPage].clear();
        m_dirty = true;
        emit contentChanged();
        update();
    }
}

bool SideNotesPanel::hasContent(int pageIndex) const
{
    return m_pageStrokes.contains(pageIndex) && !m_pageStrokes[pageIndex].isEmpty();
}

int SideNotesPanel::totalPagesWithContent() const
{
    int count = 0;
    for (auto it = m_pageStrokes.constBegin(); it != m_pageStrokes.constEnd(); ++it) {
        if (!it.value().isEmpty()) count++;
    }
    return count;
}

// ===== Persistence =====

void SideNotesPanel::setNotesDir(const QString &dir)
{
    if (m_dirty) saveNotes();
    m_notesDir = dir;
    m_pageStrokes.clear();
    m_dirty = false;
    if (!dir.isEmpty()) {
        loadNotes();
    }
    update();
}

QString SideNotesPanel::notesFilePath() const
{
    if (m_notesDir.isEmpty()) return QString();
    return m_notesDir + "/side_notes.json";
}

void SideNotesPanel::saveNotes()
{
    QString path = notesFilePath();
    if (path.isEmpty()) return;

    // Ensure directory exists
    QDir dir(m_notesDir);
    if (!dir.exists()) dir.mkpath(".");

    QJsonObject root;
    for (auto it = m_pageStrokes.constBegin(); it != m_pageStrokes.constEnd(); ++it) {
        if (it.value().isEmpty()) continue;
        QJsonArray strokesArray;
        for (const VectorStroke &stroke : it.value()) {
            QJsonObject strokeObj;
            strokeObj["color"] = stroke.color.name(QColor::HexArgb);
            strokeObj["thickness"] = stroke.baseThickness;
            QJsonArray pointsArray;
            for (const StrokePoint &pt : stroke.points) {
                pointsArray.append(pt.toJson());
            }
            strokeObj["points"] = pointsArray;
            strokesArray.append(strokeObj);
        }
        root[QString::number(it.key())] = strokesArray;
    }

    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        file.close();
        m_dirty = false;
    }
}

void SideNotesPanel::loadNotes()
{
    QString path = notesFilePath();
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) return;

    QJsonObject root = doc.object();
    m_pageStrokes.clear();

    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        bool ok;
        int pageIdx = it.key().toInt(&ok);
        if (!ok) continue;

        QJsonArray strokesArray = it.value().toArray();
        QVector<VectorStroke> strokes;
        strokes.reserve(strokesArray.size());

        for (const QJsonValue &val : strokesArray) {
            QJsonObject strokeObj = val.toObject();
            VectorStroke stroke;
            stroke.color = QColor(strokeObj["color"].toString());
            stroke.baseThickness = strokeObj["thickness"].toDouble(2.5);

            QJsonArray pointsArray = strokeObj["points"].toArray();
            stroke.points.reserve(pointsArray.size());
            for (const QJsonValue &ptVal : pointsArray) {
                stroke.points.append(StrokePoint::fromJson(ptVal.toObject()));
            }
            stroke.updateBoundingBox();
            strokes.append(stroke);
        }

        if (!strokes.isEmpty()) {
            m_pageStrokes[pageIdx] = strokes;
        }
    }
}

// ===== Appearance =====

void SideNotesPanel::setDarkMode(bool dark)
{
    m_darkMode = dark;
    update();
}

void SideNotesPanel::setContentHeight(qreal height)
{
    m_contentHeight = height;
    update();
}

// ===== Drawing =====

void SideNotesPanel::drawStroke(QPainter &painter, const VectorStroke &stroke)
{
    if (stroke.points.isEmpty()) return;

    painter.setPen(Qt::NoPen);

    QColor strokeColor = stroke.color;
    if (stroke.baseThickness < 0) return;

    // For highlighter, use composition
    QPainter::CompositionMode oldMode = painter.compositionMode();
    if (strokeColor.alpha() < 255) {
        painter.setCompositionMode(QPainter::CompositionMode_Multiply);
    }

    // Draw stroke as variable-width path
    if (stroke.points.size() == 1) {
        // Single point - draw a dot
        qreal radius = stroke.baseThickness * stroke.points[0].pressure / 2.0;
        painter.setBrush(strokeColor);
        painter.drawEllipse(stroke.points[0].pos, radius, radius);
    } else {
        // Multi-point stroke - draw as series of ellipses connected by lines
        for (int i = 0; i < stroke.points.size(); ++i) {
            const StrokePoint &pt = stroke.points[i];
            qreal radius = stroke.baseThickness * pt.pressure / 2.0;
            if (radius < 0.3) radius = 0.3;

            painter.setBrush(strokeColor);
            painter.drawEllipse(pt.pos, radius, radius);

            // Connect to previous point with a filled quad
            if (i > 0) {
                const StrokePoint &prev = stroke.points[i - 1];
                qreal prevRadius = stroke.baseThickness * prev.pressure / 2.0;
                if (prevRadius < 0.3) prevRadius = 0.3;

                // Draw a filled polygon connecting the two circles
                QPointF diff = pt.pos - prev.pos;
                qreal len = qSqrt(diff.x() * diff.x() + diff.y() * diff.y());
                if (len < 0.1) continue;

                QPointF perp(-diff.y() / len, diff.x() / len);
                QPainterPath path;
                path.moveTo(prev.pos + perp * prevRadius);
                path.lineTo(pt.pos + perp * radius);
                path.lineTo(pt.pos - perp * radius);
                path.lineTo(prev.pos - perp * prevRadius);
                path.closeSubpath();
                painter.drawPath(path);
            }
        }
    }

    painter.setCompositionMode(oldMode);
}

QPointF SideNotesPanel::widgetToCanvas(QPointF widgetPos) const
{
    // Convert widget coordinates to canvas coordinates
    // The canvas is scrolled vertically based on m_scrollOffsetY
    return QPointF(widgetPos.x(), widgetPos.y() + m_scrollOffsetY);
}

void SideNotesPanel::addPointToStroke(QPointF canvasPos, qreal pressure)
{
    StrokePoint pt;
    pt.pos = canvasPos;
    pt.pressure = pressure;
    m_currentStroke.points.append(pt);
}

// ===== Paint =====

void SideNotesPanel::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Background
    QColor bgColor = m_darkMode ? QColor(40, 40, 40) : QColor(250, 250, 245);
    painter.fillRect(rect(), bgColor);

    // Draw margin lines (like a notepad)
    QColor lineColor = m_darkMode ? QColor(60, 60, 60) : QColor(220, 220, 215);
    painter.setPen(QPen(lineColor, 1));
    qreal lineHeight = 30.0 * m_zoomLevel;
    qreal startY = -(m_scrollOffsetY * m_zoomLevel);
    startY = fmod(startY, lineHeight);
    if (startY < 0) startY += lineHeight;
    for (qreal y = startY; y < height(); y += lineHeight) {
        painter.drawLine(QPointF(0, y), QPointF(width(), y));
    }

    // Left margin line
    QColor marginColor = m_darkMode ? QColor(80, 60, 60) : QColor(230, 200, 200);
    painter.setPen(QPen(marginColor, 2));
    painter.drawLine(QPointF(40, 0), QPointF(40, height()));

    // Draw page label
    painter.setPen(m_darkMode ? QColor(150, 150, 150) : QColor(150, 150, 150));
    QFont labelFont = painter.font();
    labelFont.setPointSize(8);
    painter.setFont(labelFont);
    painter.drawText(QRectF(width() - 80, 5, 75, 15), Qt::AlignRight,
                     QString("P%1 Notes").arg(m_currentPage + 1));

    // Apply scroll transform for strokes
    painter.save();
    painter.translate(0, -(m_scrollOffsetY * m_zoomLevel));
    painter.scale(m_zoomLevel, m_zoomLevel);

    // Draw existing strokes for current page
    if (m_pageStrokes.contains(m_currentPage)) {
        for (const VectorStroke &stroke : m_pageStrokes[m_currentPage]) {
            drawStroke(painter, stroke);
        }
    }

    // Draw current stroke being drawn
    if (m_isDrawing && !m_currentStroke.points.isEmpty()) {
        drawStroke(painter, m_currentStroke);
    }

    painter.restore();

    // Draw border
    painter.setPen(QPen(m_darkMode ? QColor(80, 80, 80) : QColor(200, 200, 200), 1));
    painter.drawLine(QPointF(0, 0), QPointF(0, height()));
}

// ===== Input handling =====

void SideNotesPanel::tabletEvent(QTabletEvent *event)
{
    switch (event->type()) {
    case QEvent::TabletPress: {
        m_tabletActive = true;
        m_isDrawing = true;
        m_currentStroke = VectorStroke();
        m_currentStroke.color = (m_currentTool == ToolType::Eraser) ? Qt::white : m_penColor;
        m_currentStroke.baseThickness = m_penThickness;

        // Adjust thickness for marker/highlighter
        if (m_currentTool == ToolType::Marker) {
            m_currentStroke.baseThickness = m_penThickness * 2.0;
            m_currentStroke.color.setAlpha(80);
        } else if (m_currentTool == ToolType::Highlighter) {
            m_currentStroke.baseThickness = m_penThickness * 3.0;
            m_currentStroke.color.setAlpha(60);
        }

        QPointF canvasPos = widgetToCanvas(event->position());
        addPointToStroke(canvasPos, event->pressure());
        event->accept();
        update();
        break;
    }
    case QEvent::TabletMove: {
        if (!m_isDrawing) break;
        QPointF canvasPos = widgetToCanvas(event->position());

        if (m_currentTool == ToolType::Eraser) {
            // Eraser: remove strokes that intersect
            if (m_pageStrokes.contains(m_currentPage)) {
                QVector<VectorStroke> &strokes = m_pageStrokes[m_currentPage];
                qreal eraserRadius = m_penThickness * 3.0;
                for (int i = strokes.size() - 1; i >= 0; --i) {
                    if (strokes[i].containsPoint(canvasPos, eraserRadius)) {
                        strokes.removeAt(i);
                        m_dirty = true;
                    }
                }
            }
        } else {
            addPointToStroke(canvasPos, event->pressure());
        }
        event->accept();
        update();
        break;
    }
    case QEvent::TabletRelease: {
        if (m_isDrawing && m_currentTool != ToolType::Eraser &&
            !m_currentStroke.points.isEmpty()) {
            m_currentStroke.updateBoundingBox();
            m_pageStrokes[m_currentPage].append(m_currentStroke);
            m_dirty = true;
            emit contentChanged();
        }
        m_isDrawing = false;
        m_currentStroke = VectorStroke();
        m_tabletActive = false;
        event->accept();
        update();
        break;
    }
    default:
        break;
    }
}

void SideNotesPanel::mousePressEvent(QMouseEvent *event)
{
    if (m_tabletActive) return;  // Let tablet handle it
    if (event->button() != Qt::LeftButton) return;

    m_isDrawing = true;
    m_currentStroke = VectorStroke();
    m_currentStroke.color = (m_currentTool == ToolType::Eraser) ? Qt::white : m_penColor;
    m_currentStroke.baseThickness = m_penThickness;

    if (m_currentTool == ToolType::Marker) {
        m_currentStroke.baseThickness = m_penThickness * 2.0;
        m_currentStroke.color.setAlpha(80);
    } else if (m_currentTool == ToolType::Highlighter) {
        m_currentStroke.baseThickness = m_penThickness * 3.0;
        m_currentStroke.color.setAlpha(60);
    }

    QPointF canvasPos = widgetToCanvas(event->position());
    addPointToStroke(canvasPos, 1.0);
    event->accept();
    update();
}

void SideNotesPanel::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_isDrawing || m_tabletActive) return;

    QPointF canvasPos = widgetToCanvas(event->position());

    if (m_currentTool == ToolType::Eraser) {
        if (m_pageStrokes.contains(m_currentPage)) {
            QVector<VectorStroke> &strokes = m_pageStrokes[m_currentPage];
            qreal eraserRadius = m_penThickness * 3.0;
            for (int i = strokes.size() - 1; i >= 0; --i) {
                if (strokes[i].containsPoint(canvasPos, eraserRadius)) {
                    strokes.removeAt(i);
                    m_dirty = true;
                }
            }
        }
    } else {
        addPointToStroke(canvasPos, 1.0);
    }
    event->accept();
    update();
}

void SideNotesPanel::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_tabletActive) return;
    if (event->button() != Qt::LeftButton) return;

    if (m_isDrawing && m_currentTool != ToolType::Eraser &&
        !m_currentStroke.points.isEmpty()) {
        m_currentStroke.updateBoundingBox();
        m_pageStrokes[m_currentPage].append(m_currentStroke);
        m_dirty = true;
        emit contentChanged();
    }
    m_isDrawing = false;
    m_currentStroke = VectorStroke();
    event->accept();
    update();
}

void SideNotesPanel::wheelEvent(QWheelEvent *event)
{
    // Forward scroll events to parent (the main viewport should handle scrolling)
    event->ignore();
}
