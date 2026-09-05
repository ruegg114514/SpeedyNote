#pragma once

// ============================================================================
// SideNotesPanel - Side panel for extra notes alongside PDF annotation
// ============================================================================
// Provides a freehand drawing canvas that sits next to the PDF viewport.
// Each PDF page gets its own notes canvas, scroll-synced with the viewport.
// Strokes are saved as JSON in the document's notes directory.
// ============================================================================

#include <QWidget>
#include <QVector>
#include <QMap>
#include <QPointF>
#include <QColor>
#include <QScrollArea>
#include "../strokes/VectorStroke.h"
#include "../strokes/StrokePoint.h"
#include "../core/ToolType.h"

class QPaintEvent;
class QMouseEvent;
class QTabletEvent;

/**
 * @brief A side panel widget for freehand notes alongside PDF annotation.
 *
 * Each PDF page index gets its own set of strokes. The panel scrolls in sync
 * with the main DocumentViewport so the user can annotate alongside the PDF.
 * Supports pen, marker, highlighter, and eraser tools with pressure sensitivity.
 */
class SideNotesPanel : public QWidget {
    Q_OBJECT

public:
    explicit SideNotesPanel(QWidget *parent = nullptr);
    ~SideNotesPanel() override;

    // Page synchronization
    void setCurrentPage(int pageIndex);
    int currentPageIndex() const { return m_currentPage; }

    // Scroll synchronization with the main viewport
    void setScrollOffset(qreal offsetY);
    void setViewportZoom(qreal zoom);

    // Tool settings (synced from main viewport)
    void setCurrentColor(const QColor &color);
    void setCurrentThickness(qreal thickness);
    void setToolType(ToolType tool);

    // Content management
    void clearCurrentPage();
    bool hasContent(int pageIndex) const;
    int totalPagesWithContent() const;

    // Persistence
    void setNotesDir(const QString &dir);
    void saveNotes();
    void loadNotes();

    // Appearance
    void setDarkMode(bool dark);

    // Canvas sizing
    void setContentHeight(qreal height);

signals:
    void contentChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void tabletEvent(QTabletEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    // Per-page stroke storage
    QMap<int, QVector<VectorStroke>> m_pageStrokes;
    int m_currentPage = 0;

    // Current stroke being drawn
    VectorStroke m_currentStroke;
    bool m_isDrawing = false;

    // Scroll sync
    qreal m_scrollOffsetY = 0.0;
    qreal m_zoomLevel = 1.0;
    qreal m_contentHeight = 3000.0;  // Total virtual canvas height

    // Tool state
    QColor m_penColor = Qt::black;
    qreal m_penThickness = 2.5;
    ToolType m_currentTool = ToolType::Pen;

    // Tablet state
    bool m_tabletActive = false;
    qreal m_lastPressure = 1.0;

    // Persistence
    QString m_notesDir;
    bool m_dirty = false;

    // Dark mode
    bool m_darkMode = false;

    // Helpers
    void drawStroke(QPainter &painter, const VectorStroke &stroke);
    QPointF widgetToCanvas(QPointF widgetPos) const;
    void addPointToStroke(QPointF canvasPos, qreal pressure);
    QString notesFilePath() const;
};
