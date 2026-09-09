#ifndef PAGEPANEL_H
#define PAGEPANEL_H

#include <QWidget>
#include <QHash>
#include <QSet>
#include <QList>
#include <QPixmap>

class PagePanelListView;
class QTimer;
class QLabel;
class QPushButton;
class QItemSelection;
class Document;
class PageThumbnailModel;
class PageThumbnailDelegate;

/**
 * @brief Main page panel widget displaying page thumbnails.
 * 
 * Provides a thumbnail view of all pages in a paged document, allowing
 * users to navigate by clicking and reorder pages via drag-and-drop.
 * 
 * Features:
 * - QListView with custom model and delegate
 * - Manual touch scrolling with kinetic deceleration (see PagePanelListView)
 * - Auto-scroll to the current page when it falls offscreen
 * - Debounced thumbnail invalidation (INVALIDATION_DELAY_MS)
 * - Long-press drag-and-drop reorder (PDF background pages excluded)
 * - Width-responsive thumbnail sizing
 * - Auto 1-column / 2-column layout based on sidebar width, with
 *   hysteresis to avoid flicker while dragging the splitter handle
 * - Per-tab scroll position state
 * 
 * Usage:
 * 1. MainWindow creates PagePanel in left sidebar
 * 2. Call setDocument() when document changes
 * 3. Connect pageClicked to navigation
 * 4. Connect viewport's currentPageChanged to onCurrentPageChanged()
 */
class PagePanel : public QWidget {
    Q_OBJECT

public:
    explicit PagePanel(QWidget* parent = nullptr);
    ~PagePanel() override;

    // =========================================================================
    // Document Binding
    // =========================================================================

    /**
     * @brief Set the document to display pages from.
     * @param doc Document pointer (not owned).
     */
    void setDocument(Document* doc);
    
    /**
     * @brief Get the current document.
     */
    Document* document() const { return m_document; }

    // =========================================================================
    // Current Page
    // =========================================================================

    /**
     * @brief Set the current page index (for highlighting).
     * @param index 0-based page index.
     */
    void setCurrentPageIndex(int index);
    
    /**
     * @brief Get the current page index.
     */
    int currentPageIndex() const { return m_currentPageIndex; }

    // =========================================================================
    // Scroll Position State (per-tab)
    // =========================================================================

    /**
     * @brief Get the current scroll position.
     * @return Vertical scroll bar value.
     */
    int scrollPosition() const;
    
    /**
     * @brief Set the scroll position.
     * @param pos Vertical scroll bar value.
     */
    void setScrollPosition(int pos);
    
    /**
     * @brief Save the scroll position for a tab.
     * @param tabId Unique tab identifier (from TabManager::tabIdAt()).
     */
    void saveTabState(int tabId);
    
    /**
     * @brief Restore the scroll position for a tab.
     * @param tabId Unique tab identifier (from TabManager::tabIdAt()).
     */
    void restoreTabState(int tabId);
    
    /**
     * @brief Clear saved state for a closed tab.
     * @param tabId Unique tab identifier (from TabManager::tabIdAt()).
     */
    void clearTabState(int tabId);

    // =========================================================================
    // Theme
    // =========================================================================

    /**
     * @brief Set dark mode appearance.
     * @param dark True for dark mode.
     */
    void setDarkMode(bool dark);

    // =========================================================================
    // Multi-Select Mode (Plan C)
    // =========================================================================

    /**
     * @brief Enable or disable multi-page selection mode.
     * @param enabled True to enter select mode.
     *
     * In select mode the list uses NoSelection (selection is driven entirely by
     * tick-badge taps / Range / Clear so it works without a keyboard), reorder
     * drag is disabled, and the in-panel selection header is shown. Toggling
     * always clears the current selection.
     */
    void setSelectMode(bool enabled);

    /**
     * @brief Whether multi-page selection mode is active.
     */
    bool isSelectMode() const { return m_selectMode; }

    /**
     * @brief Clear the current selection (e.g. after a delete that invalidated
     *        the selected indices) and refresh the header.
     */
    void clearSelectionAfterDelete();

    /**
     * @brief Set whether PDF thumbnails should be dark-mode inverted.
     * @param enabled True to invert PDF backgrounds in thumbnails.
     */
    void setPdfDarkMode(bool enabled);

    // =========================================================================
    // Thumbnail Access
    // =========================================================================

    /**
     * @brief Get the cached thumbnail for a page.
     * @param pageIndex 0-based page index.
     * @return Cached thumbnail, or null pixmap if not cached.
     */
    QPixmap thumbnailForPage(int pageIndex) const;

    // =========================================================================
    // Thumbnail Invalidation
    // =========================================================================

    /**
     * @brief Invalidate the thumbnail for a specific page.
     * @param pageIndex 0-based page index.
     */
    void invalidateThumbnail(int pageIndex);
    
    /**
     * @brief Invalidate all thumbnails.
     */
    void invalidateAllThumbnails();
    
    /**
     * @brief Cancel all pending thumbnail renders and wait for completion.
     * 
     * Use before operations that access Document pages directly
     * to avoid race conditions with background thumbnail rendering.
     */
    void cancelPendingRenders();

signals:
    /**
     * @brief Emitted when a page thumbnail is clicked.
     * @param pageIndex The clicked page index (0-based).
     */
    void pageClicked(int pageIndex);
    
    /**
     * @brief Emitted when a page is dropped to a new position.
     * @param fromIndex Original page index.
     * @param toIndex Target page index.
     */
    void pageDropped(int fromIndex, int toIndex);

    /**
     * @brief Emitted when select mode is entered or exited.
     * @param enabled True if select mode is now active.
     */
    void selectModeChanged(bool enabled);

    /**
     * @brief Emitted when the number of selected pages changes.
     * @param count Current selection count.
     */
    void selectionCountChanged(int count);

    /**
     * @brief Emitted when the user requests deletion of the selected pages.
     * @param indices 0-based page indices to delete.
     */
    void deleteSelectedRequested(const QList<int>& indices);

    /**
     * @brief Emitted when the user requests copying the selected pages to
     *        another open document (Plan D1).
     * @param indices 0-based page indices to copy.
     */
    void copySelectedRequested(const QList<int>& indices);

public slots:
    /**
     * @brief Handle current page change from viewport.
     * @param pageIndex The new current page index.
     */
    void onCurrentPageChanged(int pageIndex);
    
    /**
     * @brief Scroll the view to show the current page.
     */
    void scrollToCurrentPage();
    
    /**
     * @brief Handle page count change in document.
     */
    void onPageCountChanged();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void onItemClicked(const QModelIndex& index);
    void onModelPageDropped(int fromIndex, int toIndex);
    void performPendingInvalidation();
    void onDragRequested(const QModelIndex& index);
    void onSelectionChanged();
    void startSelectionDrag();

private:
    void setupUI();
    void setupConnections();
    void configureListView();

    // Selection (Plan C) helpers
    QList<int> selectedRows() const;
    void updateSelectionHeader();
    void openRangeDialog();
    // Plan D2: build a drag pixmap (first thumbnail + count badge).
    QPixmap makeSelectionDragPixmap(const QList<int>& rows) const;
    void updateThumbnailWidth();
    void applyTheme();

    // Layout-mode helpers (1-column vs 2-column)
    int chooseColumnCount(int panelWidth) const;
    void applyLayoutMode(int columns, bool force = false);
    // Recompute the delegate width and ensure the list view re-lays out
    // its items. Use after structural changes that may have invalidated
    // the QListView's internal wrap state (model resets, hidden->visible
    // transitions), where Qt would not re-flow on its own after a
    // delegate sizeHint change.
    void refreshLayoutAfterStructuralChange();

    // Widgets
    PagePanelListView* m_listView = nullptr;
    PageThumbnailModel* m_model = nullptr;
    PageThumbnailDelegate* m_delegate = nullptr;

    // Selection header (Plan C) - shown only in select mode
    QWidget* m_selectionHeader = nullptr;
    QLabel* m_selectionCountLabel = nullptr;
    QPushButton* m_rangeButton = nullptr;
    QPushButton* m_clearButton = nullptr;
    QPushButton* m_copyButton = nullptr;
    QPushButton* m_deleteButton = nullptr;

    // State
    Document* m_document = nullptr;
    int m_currentPageIndex = 0;
    bool m_darkMode = false;
    bool m_selectMode = false;
    
    // Current layout column count (1 or 2). Hysteresis is applied so we don't
    // flip back and forth while the user drags the splitter handle.
    int m_currentColumns = 1;
    
    // Debounced invalidation
    QTimer* m_invalidationTimer = nullptr;
    QSet<int> m_pendingInvalidations;
    bool m_needsFullRefresh = false;
    
    // Debounced resize (avoids heavy cancel+re-render per pixel during sidebar drag)
    QTimer* m_resizeDebounceTimer = nullptr;
    int m_pendingThumbnailWidth = 0;
    
    // Per-tab scroll positions
    QHash<int, int> m_tabScrollPositions;
    
    // Constants
    static constexpr int MIN_THUMBNAIL_WIDTH = 100;
    static constexpr int THUMBNAIL_PADDING = 16;  // Padding on each side
    static constexpr int INVALIDATION_DELAY_MS = 500;
    static constexpr int RESIZE_DEBOUNCE_MS = 150;
    
    // Two-column layout thresholds (panel width in logical pixels). The
    // hysteresis band prevents flicker while dragging the splitter handle.
    static constexpr int TWO_COL_ENTER_WIDTH = 340;
    static constexpr int TWO_COL_EXIT_WIDTH  = 290;
    static constexpr int COLUMN_GAP          = 4;
};

#endif // PAGEPANEL_H

