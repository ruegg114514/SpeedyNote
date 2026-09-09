#include "PagePanel.h"
#include "PagePanelListView.h"
#include "../PageThumbnailModel.h"
#include "../PageThumbnailDelegate.h"
#include "../ThumbnailRenderer.h"
#include "../dialogs/PageRangeSelectDialog.h"
#include "../../core/Document.h"
#include "../../core/PageTransferMime.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QItemSelectionModel>
#include <QScrollBar>
#include <QTimer>
#include <QResizeEvent>
#include <QDialog>
#include <QDrag>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <algorithm>

// ============================================================================
// Constructor / Destructor
// ============================================================================

PagePanel::PagePanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    setupConnections();
}

PagePanel::~PagePanel()
{
    // Children are parented, will be deleted automatically
}

// ============================================================================
// Setup
// ============================================================================

void PagePanel::setupUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Create model
    m_model = new PageThumbnailModel(this);
    
    // Create delegate
    m_delegate = new PageThumbnailDelegate(this);
    
    // Create the in-panel selection header (Plan C). Hidden until select mode.
    m_selectionHeader = new QWidget(this);
    QHBoxLayout* headerLayout = new QHBoxLayout(m_selectionHeader);
    headerLayout->setContentsMargins(8, 4, 8, 4);
    headerLayout->setSpacing(6);
    m_selectionCountLabel = new QLabel(tr("0 selected"), m_selectionHeader);
    m_rangeButton = new QPushButton(tr("Range..."), m_selectionHeader);
    m_clearButton = new QPushButton(tr("Clear"), m_selectionHeader);
    m_copyButton = new QPushButton(tr("Copy to..."), m_selectionHeader);
    m_deleteButton = new QPushButton(tr("Delete"), m_selectionHeader);
    m_rangeButton->setCursor(Qt::PointingHandCursor);
    m_clearButton->setCursor(Qt::PointingHandCursor);
    m_copyButton->setCursor(Qt::PointingHandCursor);
    m_deleteButton->setCursor(Qt::PointingHandCursor);
    headerLayout->addWidget(m_selectionCountLabel);
    headerLayout->addStretch(1);
    headerLayout->addWidget(m_rangeButton);
    headerLayout->addWidget(m_clearButton);
    headerLayout->addWidget(m_copyButton);
    headerLayout->addWidget(m_deleteButton);
    m_selectionHeader->setVisible(false);
    layout->addWidget(m_selectionHeader);

    // Create list view (custom class with long-press drag support)
    m_listView = new PagePanelListView(this);
    configureListView();
    
    // Set model and delegate
    m_listView->setModel(m_model);
    m_listView->setItemDelegate(m_delegate);
    
    layout->addWidget(m_listView);
    
    // Create invalidation timer
    m_invalidationTimer = new QTimer(this);
    m_invalidationTimer->setSingleShot(true);
    m_invalidationTimer->setInterval(INVALIDATION_DELAY_MS);
    
    // Create resize debounce timer
    m_resizeDebounceTimer = new QTimer(this);
    m_resizeDebounceTimer->setSingleShot(true);
    m_resizeDebounceTimer->setInterval(RESIZE_DEBOUNCE_MS);
    connect(m_resizeDebounceTimer, &QTimer::timeout, this, [this]() {
        if (m_pendingThumbnailWidth > 0) {
            m_model->setThumbnailWidth(m_pendingThumbnailWidth);
            m_pendingThumbnailWidth = 0;
        }
        
        // After the resize has settled, re-center on the current page if it
        // is no longer visible. Uses the same offscreen-only policy as
        // onCurrentPageChanged() so we never steal the scroll position when
        // the user is intentionally browsing the thumbnail list.
        if (m_document && m_currentPageIndex >= 0 && m_listView) {
            QModelIndex idx = m_model->index(m_currentPageIndex, 0);
            if (idx.isValid()) {
                QRect itemRect = m_listView->visualRect(idx);
                QRect viewRect = m_listView->viewport()->rect();
                if (!viewRect.intersects(itemRect)) {
                    scrollToCurrentPage();
                }
            }
        }
    });
    
    // Apply initial theme
    applyTheme();
}

void PagePanel::configureListView()
{
    // Basic configuration
    m_listView->setViewMode(QListView::ListMode);
    m_listView->setFlow(QListView::TopToBottom);
    m_listView->setWrapping(false);
    m_listView->setResizeMode(QListView::Adjust);
    // SinglePass (not Batched) keeps wrap calculations consistent and avoids
    // a class of scroll jumps we hit while iterating on this panel.
    m_listView->setLayoutMode(QListView::SinglePass);
    
    // Selection
    m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_listView->setSelectionBehavior(QAbstractItemView::SelectRows);
    
    // Scrolling
    m_listView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_listView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    // Drag and drop
    m_listView->setDragEnabled(true);
    m_listView->setAcceptDrops(true);
    m_listView->setDropIndicatorShown(true);
    m_listView->setDragDropMode(QAbstractItemView::InternalMove);
    m_listView->setDefaultDropAction(Qt::MoveAction);
    
    // Appearance
    m_listView->setFrameShape(QFrame::NoFrame);
    m_listView->setSpacing(0);
    m_listView->setUniformItemSizes(false);  // Items may have different heights
    
    // Enable mouse tracking for hover effects
    m_listView->setMouseTracking(true);
    m_listView->viewport()->setMouseTracking(true);
    m_listView->setAttribute(Qt::WA_Hover, true);
    m_listView->viewport()->setAttribute(Qt::WA_Hover, true);
    
    // Note: Touch scrolling is handled manually by PagePanelListView
}

void PagePanel::setupConnections()
{
    // Item click
    connect(m_listView, &QListView::clicked, this, &PagePanel::onItemClicked);
    
    // Long-press drag request (touch input)
    connect(m_listView, &PagePanelListView::dragRequested,
            this, &PagePanel::onDragRequested);

    // Plan D2: multi-page cross-document transfer drag (select mode)
    connect(m_listView, &PagePanelListView::selectionDragRequested,
            this, &PagePanel::startSelectionDrag);
    
    // Page dropped from model
    connect(m_model, &PageThumbnailModel::pageDropped, 
            this, &PagePanel::onModelPageDropped);
    
    // Invalidation timer
    connect(m_invalidationTimer, &QTimer::timeout, 
            this, &PagePanel::performPendingInvalidation);

    // Selection changes (Plan C): the view's selection model is the single
    // source of truth for the multi-select set.
    if (m_listView->selectionModel()) {
        connect(m_listView->selectionModel(), &QItemSelectionModel::selectionChanged,
                this, [this](const QItemSelection&, const QItemSelection&) {
                    onSelectionChanged();
                });
    }

    // Selection header buttons
    connect(m_clearButton, &QPushButton::clicked, this, [this]() {
        m_listView->clearSelection();
    });
    connect(m_rangeButton, &QPushButton::clicked, this, [this]() {
        openRangeDialog();
    });
    connect(m_deleteButton, &QPushButton::clicked, this, [this]() {
        const QList<int> rows = selectedRows();
        if (!rows.isEmpty()) {
            emit deleteSelectedRequested(rows);
        }
    });
    connect(m_copyButton, &QPushButton::clicked, this, [this]() {
        const QList<int> rows = selectedRows();
        if (!rows.isEmpty()) {
            emit copySelectedRequested(rows);
        }
    });
}

// ============================================================================
// Document Binding
// ============================================================================

void PagePanel::setDocument(Document* doc)
{
    if (m_document == doc) {
        return;
    }
    
    // Selection indices are document-specific: leaving select mode here also
    // resyncs the action-bar toggle via selectModeChanged.
    if (m_selectMode) {
        setSelectMode(false);
    }
    
    m_document = doc;
    m_currentPageIndex = 0;
    
    // Update model
    m_model->setDocument(doc);
    m_model->setCurrentPageIndex(0);
    
    // setDocument() performs a begin/endResetModel like onPageCountChanged,
    // which can disrupt the QListView's internal wrap-mode bookkeeping
    // (especially with setUniformItemSizes(false)) and silently fall back
    // to a single column. Refresh delegate width and force a relayout.
    refreshLayoutAfterStructuralChange();
    
    // Clear pending invalidations
    m_pendingInvalidations.clear();
    m_needsFullRefresh = false;
}

// ============================================================================
// Current Page
// ============================================================================

void PagePanel::setCurrentPageIndex(int index)
{
    if (m_currentPageIndex != index && m_document) {
        m_currentPageIndex = index;
        m_model->setCurrentPageIndex(index);
    }
}

void PagePanel::onCurrentPageChanged(int pageIndex)
{
    int previousPage = m_currentPageIndex;
    setCurrentPageIndex(pageIndex);
    
    // Only auto-scroll if the page change is significant (not just minor viewport scroll)
    // and if the new current page is not already visible in the list view
    if (isVisible() && previousPage != pageIndex) {
        // Check if the current page item is already visible
        QModelIndex index = m_model->index(pageIndex, 0);
        if (index.isValid()) {
            QRect itemRect = m_listView->visualRect(index);
            QRect viewRect = m_listView->viewport()->rect();
            
            // Only scroll if item is completely outside visible area
            if (!viewRect.intersects(itemRect)) {
                scrollToCurrentPage();
            }
        }
    }
}

void PagePanel::scrollToCurrentPage()
{
    if (!m_document || !m_listView || !m_model || m_currentPageIndex < 0) {
        return;
    }
    
    QModelIndex index = m_model->index(m_currentPageIndex, 0);
    if (index.isValid()) {
        m_listView->scrollTo(index, QAbstractItemView::EnsureVisible);
    }
}

// ============================================================================
// Scroll Position State
// ============================================================================

int PagePanel::scrollPosition() const
{
    return m_listView->verticalScrollBar()->value();
}

void PagePanel::setScrollPosition(int pos)
{
    m_listView->verticalScrollBar()->setValue(pos);
}

void PagePanel::saveTabState(int tabIndex)
{
    m_tabScrollPositions[tabIndex] = scrollPosition();
}

void PagePanel::restoreTabState(int tabIndex)
{
    if (m_tabScrollPositions.contains(tabIndex)) {
        setScrollPosition(m_tabScrollPositions.value(tabIndex));
    } else {
        // New tab - scroll to current page
        scrollToCurrentPage();
    }
}

void PagePanel::clearTabState(int tabIndex)
{
    m_tabScrollPositions.remove(tabIndex);
}

// ============================================================================
// Theme
// ============================================================================

void PagePanel::setDarkMode(bool dark)
{
    if (m_darkMode != dark) {
        m_darkMode = dark;
        m_delegate->setDarkMode(dark);
        applyTheme();
        m_listView->viewport()->update();
    }
}

void PagePanel::setPdfDarkMode(bool enabled)
{
    if (m_model) {
        m_model->setPdfDarkMode(enabled);
    }
}

void PagePanel::applyTheme()
{
    // Unified gray colors: dark #2a2e32, light #F5F5F5
    QString bgColor = m_darkMode ? "#2a2e32" : "#F5F5F5";
    
    m_listView->setStyleSheet(QString(R"(
        QListView {
            background-color: %1;
            border: none;
            outline: none;
        }
        QListView::item {
            border: none;
            padding: 0px;
        }
        QListView::item:selected {
            background-color: transparent;
        }
    )").arg(bgColor));

    // Selection header (Plan C): match the panel background, readable text.
    if (m_selectionHeader) {
        const QString headerBg = m_darkMode ? "#23272b" : "#ECECEC";
        const QString textColor = m_darkMode ? "#E0E0E0" : "#202020";
        m_selectionHeader->setStyleSheet(QString(
            "QWidget { background-color: %1; }"
            "QLabel { color: %2; }")
            .arg(headerBg, textColor));
    }
}

// ============================================================================
// Multi-Select Mode (Plan C)
// ============================================================================

void PagePanel::setSelectMode(bool enabled)
{
    if (m_selectMode == enabled) {
        return;
    }
    m_selectMode = enabled;

    if (enabled) {
        // Multi-select; the list becomes a drag SOURCE only (Plan D2 custom
        // multi-page transfer drag), and does not accept reorder drops.
        // NoSelection means native input never mutates the selection: it is
        // driven entirely by tick-badge taps / Range / Clear (tablet-friendly,
        // no Ctrl/Shift needed). Programmatic selection still renders and a
        // drag from an already-selected page still starts (DraggingState).
        m_listView->setSelectionMode(QAbstractItemView::NoSelection);
        m_listView->setDragDropMode(QAbstractItemView::DragOnly);
        m_listView->setDragEnabled(true);
        m_listView->setAcceptDrops(false);
    } else {
        m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
        m_listView->setDragEnabled(true);
        m_listView->setAcceptDrops(true);
        m_listView->setDragDropMode(QAbstractItemView::InternalMove);
        m_listView->setDefaultDropAction(Qt::MoveAction);
    }
    m_listView->setSelectMode(enabled);

    m_listView->clearSelection();

    if (m_delegate) {
        m_delegate->setSelectMode(enabled);
    }
    if (m_selectionHeader) {
        m_selectionHeader->setVisible(enabled);
    }

    updateSelectionHeader();
    m_listView->viewport()->update();

    emit selectModeChanged(enabled);
}

void PagePanel::clearSelectionAfterDelete()
{
    if (m_listView) {
        m_listView->clearSelection();
    }
    updateSelectionHeader();
}

void PagePanel::onSelectionChanged()
{
    updateSelectionHeader();
    emit selectionCountChanged(selectedRows().size());
}

QList<int> PagePanel::selectedRows() const
{
    QList<int> rows;
    if (!m_listView || !m_listView->selectionModel()) {
        return rows;
    }
    const QModelIndexList indexes = m_listView->selectionModel()->selectedIndexes();
    for (const QModelIndex& idx : indexes) {
        if (idx.isValid()) {
            rows.append(idx.row());
        }
    }
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    return rows;
}

void PagePanel::updateSelectionHeader()
{
    if (!m_selectionCountLabel) {
        return;
    }
    const int count = selectedRows().size();
    m_selectionCountLabel->setText(tr("%1 selected").arg(count));
    if (m_deleteButton) {
        m_deleteButton->setEnabled(count > 0);
    }
    if (m_clearButton) {
        m_clearButton->setEnabled(count > 0);
    }
    if (m_copyButton) {
        m_copyButton->setEnabled(count > 0);
    }
}

void PagePanel::openRangeDialog()
{
    if (!m_document) {
        return;
    }
    const int pageCount = m_document->pageCount();
    if (pageCount <= 0) {
        return;
    }

    PageRangeSelectDialog dialog(pageCount, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QItemSelectionModel* sel = m_listView->selectionModel();
    if (!sel) {
        return;
    }
    const QList<int> indices = dialog.selectedIndices();
    for (int row : indices) {
        if (row >= 0 && row < pageCount) {
            const QModelIndex idx = m_model->index(row, 0);
            if (idx.isValid()) {
                sel->select(idx, QItemSelectionModel::Select | QItemSelectionModel::Rows);
            }
        }
    }
}

// ============================================================================
// Thumbnail Access
// ============================================================================

QPixmap PagePanel::thumbnailForPage(int pageIndex) const
{
    if (!m_model) {
        return QPixmap();
    }
    return m_model->thumbnailForPage(pageIndex);
}

// ============================================================================
// Thumbnail Invalidation
// ============================================================================

void PagePanel::invalidateThumbnail(int pageIndex)
{
    // Optimization: If panel is not visible, just mark for refresh when it becomes visible.
    // This avoids clearing cached thumbnails unnecessarily while the user is editing
    // on another sidebar tab, and prevents any rendering work until the panel is shown.
    if (!isVisible()) {
        m_pendingInvalidations.insert(pageIndex);
        // Don't start the timer - we'll handle it in showEvent
        return;
    }
    
    m_pendingInvalidations.insert(pageIndex);
    
    // Start debounce timer if not already running
    if (!m_invalidationTimer->isActive()) {
        m_invalidationTimer->start();
    }
}

void PagePanel::invalidateAllThumbnails()
{
    m_needsFullRefresh = true;
    m_pendingInvalidations.clear();
    
    // Start debounce timer if not already running
    if (!m_invalidationTimer->isActive()) {
        m_invalidationTimer->start();
    }
}

void PagePanel::cancelPendingRenders()
{
    // Cancel all pending thumbnail renders and wait for completion.
    // This is used before operations that access Document pages directly
    // (like MainWindow::renderPage0Thumbnail) to avoid race conditions.
    if (m_model) {
        m_model->cancelPendingRenders();
    }
}

void PagePanel::performPendingInvalidation()
{
    if (m_needsFullRefresh) {
        m_model->invalidateAllThumbnails();
        m_needsFullRefresh = false;
    } else {
        for (int pageIndex : m_pendingInvalidations) {
            m_model->invalidateThumbnail(pageIndex);
        }
    }
    
    m_pendingInvalidations.clear();
}

// ============================================================================
// Page Count Change
// ============================================================================

void PagePanel::onPageCountChanged()
{
    m_model->onPageCountChanged();
    
    // After a full model reset, QListView's internal wrap-mode state can be
    // disrupted with setUniformItemSizes(false) and silently fall back to a
    // single column. Refresh delegate width and force a relayout so
    // 2-column users don't snap back to 1-column when adding/inserting/
    // deleting a page.
    refreshLayoutAfterStructuralChange();
}

// ============================================================================
// Private Slots
// ============================================================================

void PagePanel::onItemClicked(const QModelIndex& index)
{
    if (!index.isValid()) {
        return;
    }
    
    // Select mode: selection is driven entirely by tick-badge taps handled in
    // PagePanelListView (pen/mouse at press time, touch at release). Clicks here
    // do not navigate and do not change the selection.
    if (m_selectMode) {
        return;
    }
    
    // Note: With manual touch scrolling in PagePanelListView,
    // clicked() is only emitted for taps, not during scrolling
    
    // Only respond to clicks within the thumbnail region (not the frame/padding)
    // This makes it easier to scroll without accidentally switching pages
    QPoint clickPos = m_listView->lastPressPosition();
    QRect itemRect = m_listView->visualRect(index);
    
    // Get aspect ratio for this specific page
    qreal aspectRatio = index.data(PageThumbnailModel::PageAspectRatioRole).toReal();
    if (aspectRatio <= 0) {
        aspectRatio = -1;  // Use delegate's default
    }
    
    QRect thumbRect = m_delegate->thumbnailRect(itemRect, aspectRatio);
    
    if (!thumbRect.contains(clickPos)) {
        return;  // Click was outside thumbnail - ignore (allow scrolling in padding area)
    }
    
    int pageIndex = index.data(PageThumbnailModel::PageIndexRole).toInt();
    emit pageClicked(pageIndex);
}

void PagePanel::onModelPageDropped(int fromIndex, int toIndex)
{
    // Forward the signal
    emit pageDropped(fromIndex, toIndex);
}

void PagePanel::onDragRequested(const QModelIndex& index)
{
    if (!index.isValid()) {
        return;
    }
    
    // Start drag operation (triggered by long-press on touch)
    m_listView->beginDrag(Qt::MoveAction);
}

void PagePanel::startSelectionDrag()
{
    if (!m_document) {
        return;
    }

    const QList<int> rows = selectedRows();
    if (rows.isEmpty()) {
        return;
    }

    // Resolve to stable page UUIDs (indices are momentary).
    QStringList uuids;
    uuids.reserve(rows.size());
    for (int row : rows) {
        const QString uuid = m_document->pageUuidAt(row);
        if (!uuid.isEmpty()) {
            uuids.append(uuid);
        }
    }
    if (uuids.isEmpty()) {
        return;
    }

    QMimeData* mime = new QMimeData();
    mime->setData(PageTransfer::mimeType(),
                  PageTransfer::encode(m_document->sessionId(), uuids));

    QDrag* drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->setPixmap(makeSelectionDragPixmap(rows));
    // Hotspot near the top-left so the badge trails the cursor.
    drag->setHotSpot(QPoint(16, 12));

    // Copy semantics: pages are duplicated into the destination document.
    drag->exec(Qt::CopyAction);
}

QPixmap PagePanel::makeSelectionDragPixmap(const QList<int>& rows) const
{
    const int count = rows.size();

    // Base pixmap: the first selected page's thumbnail if available.
    QPixmap base;
    if (!rows.isEmpty()) {
        base = thumbnailForPage(rows.first());
    }

    const int badgeSize = 26;
    QSize canvas = base.isNull() ? QSize(120, 90)
                                 : base.size().boundedTo(QSize(140, 180));
    // Ensure room for the badge in the top-left corner.
    canvas = canvas.expandedTo(QSize(badgeSize + 8, badgeSize + 8));

    QPixmap pixmap(canvas);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    if (!base.isNull()) {
        QPixmap scaled = base.scaled(canvas, Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation);
        painter.setOpacity(0.85);
        painter.drawPixmap(QPoint(0, 0), scaled);
        painter.setOpacity(1.0);
    } else {
        painter.setBrush(QColor(120, 120, 120, 200));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(pixmap.rect().adjusted(1, 1, -1, -1), 6, 6);
    }

    // Count badge (top-left).
    const QRect badgeRect(4, 4, badgeSize, badgeSize);
    painter.setBrush(QColor(0, 122, 255));
    painter.setPen(QPen(Qt::white, 1.5));
    painter.drawEllipse(badgeRect);

    QFont font = painter.font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 0.95);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(badgeRect, Qt::AlignCenter, QString::number(count));

    painter.end();
    return pixmap;
}

// ============================================================================
// Thumbnail Width
// ============================================================================

int PagePanel::chooseColumnCount(int panelWidth) const
{
    // Treat non-positive widths (hidden / not yet sized) as "keep current mode"
    // so we don't accidentally collapse to 1-col on startup before the splitter
    // has assigned a real size.
    if (panelWidth <= 0) {
        return m_currentColumns;
    }
    
    if (panelWidth >= TWO_COL_ENTER_WIDTH) {
        return 2;
    }
    if (panelWidth <= TWO_COL_EXIT_WIDTH) {
        return 1;
    }
    // Inside the hysteresis band: keep current mode.
    return m_currentColumns;
}

void PagePanel::applyLayoutMode(int columns, bool force)
{
    // Allow callers to force a re-application even when columns are unchanged.
    // This is needed after a full model reset (begin/endResetModel), where
    // QListView's internal layout state can lose the wrap-mode bookkeeping
    // (especially with setUniformItemSizes(false)) and silently fall back to
    // a single column even though we never asked it to.
    if (columns == m_currentColumns && !force) {
        return;
    }
    m_currentColumns = columns;
    
    if (columns >= 2) {
        // Items flow left-to-right and wrap to the next row once a row is full.
        // Combined with a per-item width hint of roughly viewport/2 this gives
        // a clean 2-column grid while letting QListView handle the per-row
        // height (which may vary if pages have mixed aspect ratios).
        m_listView->setFlow(QListView::LeftToRight);
        m_listView->setWrapping(true);
    } else {
        // Classic vertical list: one item per row, no wrapping.
        m_listView->setFlow(QListView::TopToBottom);
        m_listView->setWrapping(false);
    }
    
    // Force an immediate relayout instead of waiting for the next paint.
    m_listView->doItemsLayout();
    
    // After the new layout has been computed, re-center on the current page
    // so the user doesn't get lost when columns change. Defer to the event
    // loop so visualRect() reflects the new layout.
    QTimer::singleShot(0, this, [this]() {
        scrollToCurrentPage();
    });
}

void PagePanel::refreshLayoutAfterStructuralChange()
{
    // Recompute delegate width first. If the column count crosses the
    // hysteresis threshold, updateThumbnailWidth() itself will call
    // applyLayoutMode() which re-sets flow/wrap and runs doItemsLayout().
    const int oldColumns = m_currentColumns;
    updateThumbnailWidth();
    
    // If updateThumbnailWidth() didn't flip columns, it didn't trigger a
    // relayout -- Qt only re-flows on widget resize, not on delegate
    // sizeHint changes. Force the relayout here so the view picks up the
    // refreshed delegate sizeHint and any wrap-state recovery after a
    // model reset.
    if (m_currentColumns == oldColumns) {
        applyLayoutMode(m_currentColumns, /*force=*/true);
    }
}

void PagePanel::updateThumbnailWidth()
{
    // Use viewport width (excludes vertical scrollbar) for accurate per-column
    // sizing. Only trust the viewport when the panel is actually visible:
    // while hidden inside an inactive tab, the QListView's viewport is not
    // yet laid out for the current geometry, so its width can come back
    // unreliably small. If we used that small value, the 2-column formula
    // would clamp the delegate to MIN_THUMBNAIL_WIDTH and bake a stale
    // sizeHint into doItemsLayout, producing a 3+ column layout the first
    // time the user opens the tab. Fall back to widget width minus the
    // platform's vertical-scrollbar reservation, which matches what the
    // viewport will report once the tab is shown.
    int viewportWidth = 0;
    if (isVisible() && m_listView && m_listView->viewport()) {
        viewportWidth = m_listView->viewport()->width();
    }
    if (viewportWidth <= 0) {
        int scrollbarWidth = m_listView
            ? m_listView->verticalScrollBar()->sizeHint().width()
            : 18;
        viewportWidth = qMax(0, width() - scrollbarWidth);
    }
    if (viewportWidth <= 0) {
        viewportWidth = width();
    }
    
    const int panelWidth = width();
    const int columns = chooseColumnCount(panelWidth);
    
    int thumbnailWidth;
    if (columns >= 2) {
        const int available = viewportWidth - THUMBNAIL_PADDING * 2 - COLUMN_GAP;
        thumbnailWidth = qMax(MIN_THUMBNAIL_WIDTH, available / 2);
    } else {
        const int available = viewportWidth - THUMBNAIL_PADDING * 2;
        thumbnailWidth = qMax(MIN_THUMBNAIL_WIDTH, available);
    }
    
    qreal dpr = devicePixelRatioF();
    
    // Update delegate FIRST so the next doItemsLayout (inside applyLayoutMode)
    // uses the new per-cell sizeHint. Otherwise Qt's wrap calculation would
    // see oversized items from the previous mode and pack them 1-per-row,
    // leaving thumbnails small but still in a single column.
    m_delegate->setThumbnailWidth(thumbnailWidth);
    
    // Flip layout mode after the delegate width has been updated.
    if (columns != m_currentColumns) {
        applyLayoutMode(columns);
    }
    
    // Debounce the heavy model update (cancels renders + clears cache + re-requests)
    m_pendingThumbnailWidth = thumbnailWidth;
    m_model->setDevicePixelRatio(dpr);
    m_resizeDebounceTimer->start();
}

// ============================================================================
// Event Handlers
// ============================================================================

// Override resizeEvent to update thumbnail width
void PagePanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateThumbnailWidth();
}

// Override showEvent to handle refresh when becoming visible
void PagePanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    
    // Process any pending invalidations that accumulated while hidden
    // This is more efficient than clearing cache while hidden (thumbnails stay cached)
    if (m_needsFullRefresh) {
        m_model->invalidateAllThumbnails();
        m_needsFullRefresh = false;
        m_pendingInvalidations.clear();  // Full refresh supersedes individual invalidations
    } else if (!m_pendingInvalidations.isEmpty()) {
        // Invalidate only the pages that were modified while hidden
        for (int pageIndex : m_pendingInvalidations) {
            m_model->invalidateThumbnail(pageIndex);
        }
        m_pendingInvalidations.clear();
    }
    
    // The panel may have been resized while hidden (e.g., user widened the
    // sidebar from a different tab). In that case the delegate's sizeHint
    // and the QListView's wrap layout can be stale because the viewport
    // width was unreliable while hidden. Refresh delegate width and force
    // a relayout so the first visible frame uses the correct per-cell
    // sizeHint. applyLayoutMode() also schedules an offscreen-only
    // scrollToCurrentPage(), which preserves the user's scroll position
    // when the current page is already visible.
    refreshLayoutAfterStructuralChange();
}

