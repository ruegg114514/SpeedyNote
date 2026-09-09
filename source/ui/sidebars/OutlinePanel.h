#pragma once

// ============================================================================
// OutlinePanel - PDF Table of Contents navigation widget
// ============================================================================
// Part of the SpeedyNote document architecture (Phase E.2)
//
// OutlinePanel displays the PDF outline (table of contents) and allows users
// to navigate the document by clicking entries. It also highlights the current
// section as the user scrolls through the document.
//
// Features:
// - Hierarchical tree view of PDF outline
// - Touch-friendly (36px row height, kinetic scrolling)
// - Click to navigate to page/position
// - Automatic highlighting of current section
// - Session-only state persistence (expand/collapse)
//
// Usage:
// 1. MainWindow creates OutlinePanel in LeftSidebarContainer
// 2. When document changes: call setOutline() with PDF outline data
// 3. Connect navigationRequested signal to viewport navigation
// 4. Connect viewport's currentPageChanged to highlightPage()
// ============================================================================

#include <QWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QSet>
#include <QHash>
#include <QPointF>

#include "../../pdf/PdfProvider.h"  // For PdfOutlineItem (QVector requires complete type)

class OutlineItemDelegate;
class OutlinePanelTreeWidget;

/**
 * @brief Widget for displaying and navigating PDF outline (table of contents).
 * 
 * Provides a tree view of the PDF outline with navigation capabilities.
 * Users can click items to jump to specific pages/positions in the document.
 */
class OutlinePanel : public QWidget {
    Q_OBJECT

public:
    explicit OutlinePanel(QWidget* parent = nullptr);
    ~OutlinePanel() override = default;

    // =========================================================================
    // Outline Data
    // =========================================================================

    /**
     * @brief Set the outline data to display.
     * @param outline The (aggregated) PDF outline items (hierarchical). Each item
     *        carries its owning sourceId and its targetPage in ORIGINAL page space
     *        (see Document::aggregatedOutline).
     * @param sourceSlots Per-source palette slot (sourceId -> slot). Empty for a
     *        single-source outline, so no accent chip is drawn (OUT1 / Q13.3).
     * @param unavailableKeys keyFor(sourceId, originalPage) for entries whose target
     *        page is no longer present in the notebook; rendered greyed and inert.
     *
     * Clears any existing outline and populates the tree with new data.
     * Applies default expansion state from PDF or first-level expansion.
     */
    void setOutline(const QVector<PdfOutlineItem>& outline,
                    const QHash<QString, int>& sourceSlots = {},
                    const QSet<QString>& unavailableKeys = {});

    /**
     * @brief Refresh which entries are greyed/inert without rebuilding the tree.
     * @param unavailableKeys keyFor(sourceId, originalPage) of absent targets.
     *
     * Preserves expansion/selection state. Call after a page delete/undo/reorder
     * changes which PDF pages are present.
     */
    void updateAvailability(const QSet<QString>& unavailableKeys);

    /**
     * @brief Stable key combining a source id and an original PDF page.
     */
    static QString keyFor(const QString& sourceId, int originalPage);

    /**
     * @brief Clear the outline display.
     * 
     * Call when switching to a document without an outline.
     */
    void clearOutline();

    /**
     * @brief Check if an outline is currently loaded.
     * @return True if outline data is present.
     */
    bool hasOutline() const { return !m_outline.isEmpty(); }

    // =========================================================================
    // Navigation Highlighting
    // =========================================================================

    /**
     * @brief Highlight the outline item for the current page's source (OUT1).
     * @param sourceId The current page's PDF source id (empty = primary).
     * @param originalPage The current page's ORIGINAL PDF page (0-based).
     *
     * Floor-match scoped to @p sourceId: highlights the entry of that source with
     * the highest targetPage <= originalPage. Auto-expands parents if visible.
     */
    void highlightPage(const QString& sourceId, int originalPage);

    // =========================================================================
    // State Management (for multi-tab support)
    // =========================================================================

    /**
     * @brief Save current expansion state.
     * 
     * Call before switching to another document/tab.
     */
    void saveState();

    /**
     * @brief Restore previously saved expansion state.
     * 
     * Call after switching back to this document/tab.
     */
    void restoreState();

    // =========================================================================
    // Theme
    // =========================================================================

    /**
     * @brief Update theme colors.
     * @param darkMode True for dark theme.
     */
    void updateTheme(bool darkMode);

signals:
    /**
     * @brief Emitted when user clicks an outline item to navigate (OUT1).
     * @param sourceId The target entry's PDF source id (empty = primary).
     * @param originalPage The target ORIGINAL PDF page (0-based) within that source.
     * @param position The target position within page (normalized 0-1),
     *                 or (-1,-1) if not specified.
     */
    void navigationRequested(const QString& sourceId, int originalPage, QPointF position);

private slots:
    void onItemClicked(QTreeWidgetItem* item, int column);
    void onItemExpanded(QTreeWidgetItem* item);
    void onItemCollapsed(QTreeWidgetItem* item);

private:
    void setupUi();
    void populateTree(const QVector<PdfOutlineItem>& items, QTreeWidgetItem* parent = nullptr);
    void applyDefaultExpansion(QTreeWidgetItem* item, const PdfOutlineItem& outlineItem);
    QTreeWidgetItem* findItemForPage(const QString& sourceId, int originalPage);
    QString getItemPath(QTreeWidgetItem* item) const;

    OutlinePanelTreeWidget* m_tree = nullptr;
    OutlineItemDelegate* m_delegate = nullptr;
    QVector<PdfOutlineItem> m_outline;  // Cached for state restoration

    // State per document (session only)
    QSet<QString> m_expandedItems;      // Track expanded items by path
    QHash<QString, int> m_sourceSlots;  // OUT1: sourceId -> palette slot (empty = no accent)
    QSet<QString> m_unavailableKeys;    // OUT1: keyFor(sourceId, originalPage) absent from notebook
    QString m_lastHighlightSource;      // OUT1: source of last highlight
    int m_lastHighlightedPage = -1;
    bool m_darkMode = false;

    // Custom data roles for tree items
    static constexpr int PageRole = Qt::UserRole;
    static constexpr int PositionXRole = Qt::UserRole + 1;
    static constexpr int PositionYRole = Qt::UserRole + 2;
    static constexpr int UnavailableRole = Qt::UserRole + 3;  // Plan A2: greyed/inert entry
    static constexpr int SourceIdRole = Qt::UserRole + 4;     // OUT1: owning PDF source id
    static constexpr int SourceSlotRole = Qt::UserRole + 5;   // OUT1: palette slot (-1 = none)
};

