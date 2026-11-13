#pragma once

// ============================================================================
// MemoManager - owns the floating memo cards shown over the page
// ============================================================================
// The cards are viewport-space overlays, NOT document objects: they live as
// children of the canvas container (the widget that also hosts the action bars
// and the search bar), they stay put while the page scrolls under them, and
// they are never captured by the screenshot tool (which renders the viewport
// only). That is what "the page keeps scrolling normally" means in practice.
//
// The manager is a plain QObject rather than a widget: a full-size transparent
// overlay would swallow every click meant for the page. Each card is parented
// directly to the host and only its own rectangle receives input.
//
// One Document owns the memos; the manager mirrors the open/closed state for
// the session (it is not persisted - reopening the notebook starts with no
// cards open, which is less surprising than a screen full of old ones).
// ============================================================================

#include <QHash>
#include <QObject>
#include <QRect>
#include <QSet>
#include <QString>

class Document;
class DocumentViewport;
class MemoEntry;
class MemoFloatWidget;
class QWidget;

class MemoManager : public QObject {
    Q_OBJECT

public:
    /**
     * @param host  Widget the cards are parented to (the canvas container).
     *              Must outlive the manager.
     */
    explicit MemoManager(QWidget* host, QObject* parent = nullptr);
    ~MemoManager() override;

    /**
     * @brief Point the cards at the active document + viewport.
     *
     * Cards of the previous document are removed (their open state is kept in
     * memory, so switching back reopens them). Passing nullptr (no document)
     * hides everything.
     */
    void setContext(Document* document, DocumentViewport* viewport);

    /**
     * @brief Tell the manager where the active viewport sits inside the host.
     *
     * Called whenever the pane layout changes (split view, sidebar toggle,
     * window resize). Card positions are stored in viewport coordinates, so the
     * cards are shifted by the delta and clamped into the new rectangle.
     */
    void updateHostGeometry(const QRect& viewportRectInHost);

    /// Open (or re-raise) the card for @p memoId. False when the memo is not in
    /// the current document.
    bool openMemo(const QString& memoId);

    /// Remove the card from the canvas (the memo itself is untouched).
    void closeMemo(const QString& memoId);

    /// Close every card of the current document.
    void closeAll();

    bool isMemoOpen(const QString& memoId) const;

    /// Forget a memo that was deleted (closes its card if open).
    void handleMemoRemoved(const QString& memoId);

    /// Refresh titles of already-open cards after a rename.
    void handleMemoRenamed(const QString& memoId, const QString& title);

    void setDarkMode(bool darkMode);

    /// Resize keeps the memo's own proportions while true (the small lock button
    /// on a card toggles this). A global preference, persisted in QSettings:
    /// it describes how the reader wants to manipulate cards, not the notebook.
    void setAspectRatioLocked(bool locked);
    bool isAspectRatioLocked() const { return m_aspectRatioLocked; }

private:
    void clearCards();
    MemoFloatWidget* createCard(const MemoEntry& entry);
    void activateCard(MemoFloatWidget* card);
    void deactivateAll();
    /// @param entry    memo metadata (natural size decides the opening size).
    /// @param imageSize decoded image size, passed in so the image is decoded
    ///        only once per open.
    QRectF defaultCardRectFor(const MemoEntry& entry,
                              const QSize& imageSize) const;
    void clampCardIntoHost(MemoFloatWidget* card);
    QPoint viewportOffset() const { return m_viewportRect.topLeft(); }

    bool eventFilter(QObject* watched, QEvent* event) override;

    QWidget* m_host = nullptr;
    Document* m_document = nullptr;
    DocumentViewport* m_viewport = nullptr;

    /// Active viewport rect in host coordinates. Card geometry is stored in
    /// viewport coordinates and translated by this offset when applied.
    QRect m_viewportRect;

    bool m_darkMode = false;
    bool m_aspectRatioLocked = true;

    QHash<QString, MemoFloatWidget*> m_cards;
    /// Memos whose cards were open, keyed by Document::sessionId() (session-only
    /// memory). A QString key is used instead of the Document pointer because a
    /// closed document's pointer can be reused by the next one.
    QHash<QString, QSet<QString>> m_openByDocument;
};
