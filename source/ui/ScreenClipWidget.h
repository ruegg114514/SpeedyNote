#pragma once

// ============================================================================
// ScreenClipWidget - Fullscreen screenshot region selector
// ============================================================================
// Shows a grabbed desktop pixmap fullscreen and lets the user drag out a
// region. Enter / double-click confirms, Esc cancels. The captured region is
// returned device-pixel-accurate via capturedRegion().
// ============================================================================

#include <QDialog>
#include <QPixmap>
#include <QRect>
#include <QPoint>

class QMouseEvent;
class QKeyEvent;
class QShowEvent;

class ScreenClipWidget : public QDialog {
    Q_OBJECT

public:
    explicit ScreenClipWidget(const QPixmap& screenShot,
                              QWidget* parent = nullptr);

    /**
     * @brief The selected region, cropped from the original screenshot.
     *
     * The crop is scaled by the screenshot's device pixel ratio, so on a
     * DPR 2 display a logical 100x100 selection yields a 200x200 pixel
     * pixmap. Null pixmap if nothing was selected.
     */
    QPixmap capturedRegion() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    QRect normalizedRect(const QPoint& a, const QPoint& b) const;

    QPixmap m_shot;         ///< The grabbed desktop (device-pixel sized)
    QPoint  m_anchor;       ///< Selection start (widget logical coords)
    QRect   m_selection;    ///< Current selection (widget logical coords)
    bool    m_selecting = false;
};
