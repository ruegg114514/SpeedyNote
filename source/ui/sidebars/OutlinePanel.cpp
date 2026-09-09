#include "OutlinePanel.h"
#include "OutlinePanelTreeWidget.h"
#include "OutlineItemDelegate.h"
// Note: PdfProvider.h already included via OutlinePanel.h

#include <QHeaderView>
#include <QApplication>
#include <QPalette>

// ============================================================================
// Constructor
// ============================================================================

OutlinePanel::OutlinePanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

// ============================================================================
// Setup
// ============================================================================

void OutlinePanel::setupUi()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Create tree widget (custom subclass that handles touch scrolling properly)
    m_tree = new OutlinePanelTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setIndentation(20);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setExpandsOnDoubleClick(false);  // We handle expand via arrow only
    m_tree->setAnimated(true);
    m_tree->setUniformRowHeights(true);
    
    // Enable mouse tracking for proper hover effects (mouse, stylus)
    m_tree->setMouseTracking(true);
    m_tree->viewport()->setMouseTracking(true);
    m_tree->setAttribute(Qt::WA_Hover, true);
    m_tree->viewport()->setAttribute(Qt::WA_Hover, true);
    
    // Don't use QScroller - it conflicts with QTreeWidget and causes bounce-back
    // Manual touch scrolling is implemented in OutlinePanelTreeWidget
    m_tree->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_tree->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);

    // Set custom item delegate for page numbers with leader dots
    m_delegate = new OutlineItemDelegate(this);
    m_tree->setItemDelegate(m_delegate);

    // Connect signals
    connect(m_tree, &QTreeWidget::itemClicked, this, &OutlinePanel::onItemClicked);
    connect(m_tree, &QTreeWidget::itemExpanded, this, &OutlinePanel::onItemExpanded);
    connect(m_tree, &QTreeWidget::itemCollapsed, this, &OutlinePanel::onItemCollapsed);

    layout->addWidget(m_tree);

    // Apply initial theme
    updateTheme(false);
}

// ============================================================================
// Outline Data
// ============================================================================

QString OutlinePanel::keyFor(const QString& sourceId, int originalPage)
{
    return sourceId + QChar('#') + QString::number(originalPage);
}

void OutlinePanel::setOutline(const QVector<PdfOutlineItem>& outline,
                              const QHash<QString, int>& sourceSlots,
                              const QSet<QString>& unavailableKeys)
{
    m_outline = outline;
    m_sourceSlots = sourceSlots;
    m_unavailableKeys = unavailableKeys;
    m_tree->clear();
    m_lastHighlightedPage = -1;
    m_lastHighlightSource.clear();
    
    // Clear previous document's expansion state
    // (State is per-document, not persistent across documents)
    m_expandedItems.clear();

    if (outline.isEmpty()) {
        return;
    }

    // Populate tree (applyDefaultExpansion sets initial expansion state)
    populateTree(outline, nullptr);
}

void OutlinePanel::updateAvailability(const QSet<QString>& unavailableKeys)
{
    if (m_unavailableKeys == unavailableKeys) {
        return;
    }
    m_unavailableKeys = unavailableKeys;

    // Refresh the flag on every existing item without rebuilding the tree
    // (preserves expansion + selection state).
    QTreeWidgetItemIterator it(m_tree);
    while (*it) {
        QTreeWidgetItem* item = *it;
        const int page = item->data(0, PageRole).toInt();
        const QString src = item->data(0, SourceIdRole).toString();
        item->setData(0, UnavailableRole,
                      page >= 0 && m_unavailableKeys.contains(keyFor(src, page)));
        ++it;
    }
    m_tree->viewport()->update();
}

void OutlinePanel::clearOutline()
{
    m_outline.clear();
    m_tree->clear();
    m_expandedItems.clear();
    m_lastHighlightedPage = -1;
    m_lastHighlightSource.clear();
}

void OutlinePanel::populateTree(const QVector<PdfOutlineItem>& items, QTreeWidgetItem* parent)
{
    for (const PdfOutlineItem& item : items) {
        QTreeWidgetItem* treeItem;
        
        if (parent) {
            treeItem = new QTreeWidgetItem(parent);
        } else {
            treeItem = new QTreeWidgetItem(m_tree);
        }

        // Set display text (title only - delegate will add page number)
        treeItem->setText(0, item.title);
        treeItem->setToolTip(0, item.title);  // Full title on hover

        // Store navigation data (targetPage is in ORIGINAL page space for the source)
        treeItem->setData(0, PageRole, item.targetPage);
        treeItem->setData(0, PositionXRole, item.targetPosition.x());
        treeItem->setData(0, PositionYRole, item.targetPosition.y());

        // OUT1: owning source + its palette slot (-1 when single-source / no accent).
        treeItem->setData(0, SourceIdRole, item.sourceId);
        treeItem->setData(0, SourceSlotRole, m_sourceSlots.value(item.sourceId, -1));

        // Plan A2 / OUT1: mark entries whose target PDF page is absent from the notebook.
        treeItem->setData(0, UnavailableRole,
                          item.targetPage >= 0 &&
                          m_unavailableKeys.contains(keyFor(item.sourceId, item.targetPage)));

        // Apply default expansion from PDF
        applyDefaultExpansion(treeItem, item);

        // Recursively add children
        if (!item.children.isEmpty()) {
            populateTree(item.children, treeItem);
        }
    }
}

void OutlinePanel::applyDefaultExpansion(QTreeWidgetItem* item, const PdfOutlineItem& outlineItem)
{
    // Use PDF's isOpen hint if available
    if (outlineItem.isOpen) {
        item->setExpanded(true);
    } else if (item->parent() == nullptr) {
        // Fallback: expand first level items
        item->setExpanded(true);
    }
}

// ============================================================================
// Navigation Highlighting
// ============================================================================

void OutlinePanel::highlightPage(const QString& sourceId, int originalPage)
{
    if (m_outline.isEmpty() || originalPage < 0) {
        return;
    }

    // Only update if the (source, page) changed
    if (originalPage == m_lastHighlightedPage && sourceId == m_lastHighlightSource) {
        return;
    }
    m_lastHighlightedPage = originalPage;
    m_lastHighlightSource = sourceId;

    // Find best matching item (floor match within the same source)
    QTreeWidgetItem* bestMatch = findItemForPage(sourceId, originalPage);

    if (bestMatch) {
        // Block signals to prevent triggering navigation
        m_tree->blockSignals(true);

        // Clear previous selection
        m_tree->clearSelection();

        // Select and scroll to item
        bestMatch->setSelected(true);
        m_tree->scrollToItem(bestMatch, QAbstractItemView::EnsureVisible);

        // Auto-expand parents if panel is visible
        if (isVisible()) {
            QTreeWidgetItem* parent = bestMatch->parent();
            while (parent) {
                parent->setExpanded(true);
                parent = parent->parent();
            }
        }

        m_tree->blockSignals(false);
    }
}

QTreeWidgetItem* OutlinePanel::findItemForPage(const QString& sourceId, int originalPage)
{
    QTreeWidgetItem* bestMatch = nullptr;
    int bestMatchPage = -1;

    // Iterate through all items; floor-match scoped to the current page's source.
    QTreeWidgetItemIterator it(m_tree);
    while (*it) {
        QTreeWidgetItem* item = *it;
        int itemPage = item->data(0, PageRole).toInt();
        const QString itemSource = item->data(0, SourceIdRole).toString();

        // Floor match: highest page <= current, within the same source.
        if (itemSource == sourceId && itemPage >= 0 &&
            itemPage <= originalPage && itemPage > bestMatchPage) {
            bestMatch = item;
            bestMatchPage = itemPage;
        }

        ++it;
    }

    return bestMatch;
}

// ============================================================================
// Item Interaction
// ============================================================================

void OutlinePanel::onItemClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column);

    if (!item) {
        return;
    }
    
    // Note: With manual touch scrolling in OutlinePanelTreeWidget,
    // itemClicked is only emitted for taps, not during scrolling

    int pageIndex = item->data(0, PageRole).toInt();
    if (pageIndex < 0) {
        // OUT1: synthetic source-root headers are not navigable.
        return;
    }

    // Plan A2: entries whose target page is no longer in the notebook are inert.
    if (item->data(0, UnavailableRole).toBool()) {
        return;
    }

    // Get position data
    qreal posX = item->data(0, PositionXRole).toReal();
    qreal posY = item->data(0, PositionYRole).toReal();
    QPointF position(posX, posY);

    // Emit navigation request (source-aware; pageIndex is in ORIGINAL page space)
    const QString sourceId = item->data(0, SourceIdRole).toString();
    emit navigationRequested(sourceId, pageIndex, position);
}

void OutlinePanel::onItemExpanded(QTreeWidgetItem* item)
{
    // Track expanded state by path
    QString path = getItemPath(item);
    m_expandedItems.insert(path);
}

void OutlinePanel::onItemCollapsed(QTreeWidgetItem* item)
{
    // Remove from expanded set
    QString path = getItemPath(item);
    m_expandedItems.remove(path);
}

QString OutlinePanel::getItemPath(QTreeWidgetItem* item) const
{
    // Build unique path from root to item (using titles)
    QStringList pathParts;
    QTreeWidgetItem* current = item;
    
    while (current) {
        pathParts.prepend(current->text(0));
        current = current->parent();
    }
    
    return pathParts.join("/");
}

// ============================================================================
// State Management
// ============================================================================

void OutlinePanel::saveState()
{
    // m_expandedItems is already updated via onItemExpanded/Collapsed
    // Nothing extra needed here
}

void OutlinePanel::restoreState()
{
    if (m_expandedItems.isEmpty()) {
        return;
    }

    // Collapse all first
    m_tree->collapseAll();

    // Re-expand saved items
    QTreeWidgetItemIterator it(m_tree);
    while (*it) {
        QTreeWidgetItem* item = *it;
        QString path = getItemPath(item);
        
        if (m_expandedItems.contains(path)) {
            item->setExpanded(true);
        }
        
        ++it;
    }
}

// ============================================================================
// Theme
// ============================================================================

void OutlinePanel::updateTheme(bool darkMode)
{
    m_darkMode = darkMode;

    // Update delegate theme
    if (m_delegate) {
        m_delegate->setDarkMode(darkMode);
    }

    // Unified gray colors: dark #2a2e32, light #F5F5F5
    QString bgColor = darkMode ? "#2a2e32" : "#F5F5F5";

    // Stylesheet only for tree container and branch arrows
    // Item painting is handled by the custom delegate
    m_tree->setStyleSheet(QString(R"(
        QTreeWidget {
            background-color: %1;
            border: none;
            outline: none;
        }
        QTreeWidget::item {
            height: 36px;
        }
        QTreeWidget::branch {
            background-color: %1;
        }
        QTreeWidget::branch:has-children:!has-siblings:closed,
        QTreeWidget::branch:closed:has-children:has-siblings {
            border-image: none;
            image: url(:/resources/icons/right_arrow%2.png);
        }
        QTreeWidget::branch:open:has-children:!has-siblings,
        QTreeWidget::branch:open:has-children:has-siblings {
            border-image: none;
            image: url(:/resources/icons/down_arrow%2.png);
        }
    )").arg(bgColor, darkMode ? "_reversed" : ""));

    // Force repaint to apply delegate theme changes
    m_tree->viewport()->update();
}

