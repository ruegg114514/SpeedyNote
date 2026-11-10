#pragma once

// ============================================================================
// ScreenClipWidget - In-app region selector
// ============================================================================
// Shows a captured pixmap (the app's own viewport content, not the desktop)
// and lets the user drag out a region. Two families of buttons at the bottom
// work with touch, and Enter / double-click / Esc also work for keyboard
// users. The captured region is returned device-pixel-accurate via
// capturedRegion().
//
// Besides the historical "Confirm" (insert the region on the page as an image
// object), the overlay offers the memo (随记) actions:
//   * Memo (keep) - insert the region on the page AND keep a copy as a memo.
//   * Memo (cut)  - keep a copy as a memo only; nothing is inserted on the page.
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
    /// What the user confirmed the captured region should become.
    enum class Action {
        None,       ///< Nothing confirmed (rejected / closed)
        InsertOnly, ///< Historical behaviour: insert as an ImageObject
        MemoKeep,   ///< Insert as an ImageObject AND add a memo
        MemoCut     ///< Add a memo only, leave the page untouched
    };

    explicit ScreenClipWidget(const QPixmap& screenShot,
                              QWidget* parent = nullptr);

    /// The confirmed action (valid after exec() returned Accepted).
    Action action() const { return m_action; }

    /**
     * @brief The selected region, cropped from the original screenshot.
     *
     * The crop is scaled by the screenshot's device pixel ratio, so on a
     * DPR 2 display a logical 100x100 selection yields a 200x200 pixel
     * pixmap. Null pixmap if nothing was selected.
     */
    QPixmap capturedRegion() const;

    /**
     * @brief The current selection in widget logical coordinates.
     *
     * The caller positions this widget exactly over the viewport it captured,
     * so a selection here maps 1:1 onto viewport coordinates. Empty rect if
     * the user has not dragged a region yet.
     */
    QRect selectionRect() const { return m_selection; }

    /**
     * @brief Whether a usable (non-degenerate) selection exists.
     */
    bool hasValidSelection() const
    {
        return m_selection.width() >= 4 && m_selection.height() >= 4;
    }

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

    /// Bottom action buttons, in the order Cancel, Confirm, Memo (keep),
    /// Memo (cut). Hit-tested in mousePressEvent so they work with touch taps
    /// as well as mouse clicks.
    enum ButtonIndex { CancelButton = 0, ConfirmButton, MemoKeepButton,
                       MemoCutButton, ButtonCount };
    QRect actionButtonRect(int index) const;

    QPixmap m_shot;         ///< The captured viewport content (device-pixel sized)
    QPoint  m_anchor;       ///< Selection start (widget logical coords)
    QRect   m_selection;    ///< Current selection (widget logical coords)
    bool    m_selecting = false;
    Action  m_action = Action::None;
};
