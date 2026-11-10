#include "MemoGalleryWidget.h"

#include "../core/Document.h"
#include "../core/MemoEntry.h"
#include "ThemeColors.h"

#include <QAction>
#include <QColor>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QString>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>
#include <QtGlobal>

#include <algorithm>
#include <functional>
#include <utility>

// ============================================================================
// MemoGalleryWidget.cpp - implementation notes
// ============================================================================
// The widget is deliberately stateless: it renders Document::memos() and asks
// Document::memoImage() for pixels on every rebuild, so a memo that only exists
// in memory (document never saved) looks exactly like one already on disk.
//
// Two constraints shaped the structure below:
//
//  1. The header is frozen, so every helper that would normally be a private
//     slot/member lives in this file: the re-flow hook is a tiny event-filter
//     QObject and the re-flow itself is a free function. Nothing here needs moc.
//  2. A class declared in a .cpp must stay Q_OBJECT-free (AUTOMOC only reliably
//     picks up headers), so MemoCard reports clicks through std::function
//     callbacks instead of signals. It therefore also has no tr() context of
//     its own and uses QCoreApplication::translate("MemoGalleryWidget", ...).
// ============================================================================

namespace {

// ============================================================================
// Geometry
// ============================================================================
// The card width is fixed: the responsive behaviour is "how many 132 px tiles
// fit", never "how large is a tile". A sidebar can be dragged between 220 and
// 600 px, and tiles that changed size while dragging would re-decode/re-scale
// every thumbnail on every pixel of the drag.
constexpr int kCardWidth = 132;
constexpr int kThumbWidth = kCardWidth - 8;  ///< Card margins are 4 px per side
constexpr int kThumbHeight = 96;
constexpr int kGridMargin = 10;
constexpr int kGridSpacing = 10;
constexpr int kMaxColumns = 4;
constexpr int kCardRadius = 10;

/// Dynamic property holding a card's position in the newest-first order. The
/// grid layout is the only handle the resize filter has on the cards, and
/// QGridLayout does not promise to enumerate items in insertion order, so the
/// order is carried on the widget itself.
constexpr const char* kMemoOrderProperty = "memoOrder";

/// tr() for the file-local card class, which has no Q_OBJECT (see file header).
QString memoString(const char* text)
{
    return QCoreApplication::translate("MemoGalleryWidget", text);
}

// ============================================================================
// Theme
// ============================================================================
// The card colours come from the spec'd memos palette (white/#2b2b2b card on
// #d0d5dd/#4a4a4a border); text colours match ThemeColors so the gallery sits
// comfortably next to the rest of the sidebar.
struct MemoColors {
    QColor text;             ///< Header + card title
    QColor textMuted;        ///< Date line, empty state
    QColor cardBg;
    QColor cardBorder;
    QColor cardHover;
    QColor thumbBg;
    QColor thumbBorder;
    QColor placeholderBg;
    QColor placeholderText;
    QColor buttonBg;
    QColor buttonText;
    QColor buttonBorder;
    QColor pressed;
    QColor deleteHover;      ///< Destructive affordance stays red in both themes
    QColor deleteHoverText;
};

MemoColors memoColorsFor(bool darkMode)
{
    MemoColors c;
    if (darkMode) {
        c.text = QColor(0xe0, 0xe0, 0xe0);
        c.textMuted = QColor(0x9e, 0x9e, 0x9e);
        c.cardBg = QColor(0x2b, 0x2b, 0x2b);
        c.cardBorder = QColor(0x4a, 0x4a, 0x4a);
        c.cardHover = QColor(0x3a, 0x3a, 0x3a);
        c.thumbBg = QColor(0x1e, 0x1e, 0x1e);
        c.thumbBorder = QColor(0x4a, 0x4a, 0x4a);
        c.placeholderBg = QColor(0x33, 0x33, 0x33);
        c.placeholderText = QColor(0x8a, 0x8a, 0x8a);
        c.buttonBg = QColor(0x3a, 0x3a, 0x3a);
        c.buttonText = QColor(0xe0, 0xe0, 0xe0);
        c.buttonBorder = QColor(0x4a, 0x4a, 0x4a);
        c.pressed = QColor(0x55, 0x55, 0x55);
    } else {
        c.text = QColor(0x33, 0x33, 0x33);
        c.textMuted = QColor(0x88, 0x88, 0x88);
        c.cardBg = QColor(0xff, 0xff, 0xff);
        c.cardBorder = QColor(0xd0, 0xd5, 0xdd);
        c.cardHover = QColor(0xf2, 0xf4, 0xf7);
        c.thumbBg = QColor(0xfa, 0xfa, 0xfa);
        c.thumbBorder = QColor(0xe0, 0xe0, 0xe0);
        c.placeholderBg = QColor(0xf0, 0xf0, 0xf0);
        c.placeholderText = QColor(0x9a, 0x9a, 0x9a);
        c.buttonBg = QColor(0xf0, 0xf0, 0xf0);
        c.buttonText = QColor(0x33, 0x33, 0x33);
        c.buttonBorder = QColor(0xd0, 0xd5, 0xdd);
        c.pressed = QColor(0xd8, 0xd8, 0xd8);
    }
    c.deleteHover = QColor(0xe0, 0x4a, 0x4a);
    c.deleteHoverText = QColor(0xff, 0xff, 0xff);
    return c;
}

// ============================================================================
// Small helpers
// ============================================================================

/// Local position of a mouse event: Qt6 moved to QPointF positions/globalPos.
QPoint mouseLocalPos(const QMouseEvent* event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position().toPoint();
#else
    return event->pos();
#endif
}

/// How many 132 px tiles fit into @p viewportWidth, clamped to 1..kMaxColumns.
int columnsForViewport(int viewportWidth)
{
    const int usableWidth = viewportWidth - 2 * kGridMargin;
    const int columns = (usableWidth + kGridSpacing) / (kCardWidth + kGridSpacing);
    return qBound(1, columns, kMaxColumns);
}

// ============================================================================
// MemoCard - one thumbnail tile
// ============================================================================
// A plain QFrame subclass without Q_OBJECT: it emits nothing, it only forwards
// user intent to the gallery through std::function callbacks. Hover state and
// click handling are done here (not with a gallery-side event filter) because
// the frozen header cannot declare an eventFilter() override.
class MemoCard final : public QFrame {
public:
    using IdHandler = std::function<void(const QString&)>;
    using ContextHandler = std::function<void(const QString&, const QPoint&)>;

    MemoCard(const MemoEntry& entry, const QImage& image, bool darkMode,
             QWidget* parent = nullptr);

    QString memoId() const { return m_memoId; }

    void setOpenHandler(IdHandler handler) { m_openHandler = std::move(handler); }
    void setDeleteHandler(IdHandler handler) { m_deleteHandler = std::move(handler); }
    void setContextMenuHandler(ContextHandler handler) { m_contextMenuHandler = std::move(handler); }

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool event(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void buildUi(const MemoEntry& entry, const QImage& image);
    void applyTheme();
    void setHovered(bool hovered);
    void positionDeleteButton();

    QPixmap renderedThumbnail(const QImage& image) const;
    QPixmap placeholderThumbnail() const;

    QString m_memoId;
    QString m_fullTitle;
    bool m_hovered = false;

    /// True between a left press on the card and its release; a release outside
    /// the card (a drag that left the tile) is not a click.
    bool m_pressed = false;

    MemoColors m_colors;
    QLabel* m_thumbnailLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_dateLabel = nullptr;
    QToolButton* m_deleteButton = nullptr;

    IdHandler m_openHandler;
    IdHandler m_deleteHandler;
    ContextHandler m_contextMenuHandler;
};

MemoCard::MemoCard(const MemoEntry& entry, const QImage& image, bool darkMode,
                   QWidget* parent)
    : QFrame(parent)
    , m_memoId(entry.id)
    , m_fullTitle(entry.title.isEmpty() ? memoString("Untitled memo") : entry.title)
    , m_colors(memoColorsFor(darkMode))
{
    setObjectName(QStringLiteral("MemoCard"));
    setCursor(Qt::PointingHandCursor);
    // The card paints its own background (see paintEvent), but WA_Hover is what
    // arms QEvent::Enter/Leave for the hover-only delete button.
    setAttribute(Qt::WA_Hover, true);
    setContextMenuPolicy(Qt::DefaultContextMenu);
    setFixedWidth(kCardWidth);
    setToolTip(m_fullTitle);

    buildUi(entry, image);
    applyTheme();
}

void MemoCard::buildUi(const MemoEntry& entry, const QImage& image)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    // The tile is a fixed-width card whose height is whatever the three text/
    // image rows need; SetFixedSize keeps the layout from stretching it later.
    layout->setSizeConstraint(QLayout::SetFixedSize);

    // ---- Thumbnail ----
    m_thumbnailLabel = new QLabel(this);
    m_thumbnailLabel->setObjectName(QStringLiteral("MemoThumbnail"));
    m_thumbnailLabel->setAlignment(Qt::AlignCenter);
    m_thumbnailLabel->setFixedSize(kThumbWidth, kThumbHeight);
    // Clicks must reach the card itself; labels would otherwise eat the press
    // (and the tooltip of the card would never show).
    m_thumbnailLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    // Document::memoImage() is the single source of truth: it serves both the
    // persisted PNG and bytes that are still only in memory.
    m_thumbnailLabel->setPixmap(renderedThumbnail(image));
    layout->addWidget(m_thumbnailLabel);

    // ---- Title (single elided line, full text in the tooltip) ----
    m_titleLabel = new QLabel(this);
    m_titleLabel->setObjectName(QStringLiteral("MemoCardTitle"));
    m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    const QFontMetrics titleMetrics(m_titleLabel->font());
    m_titleLabel->setText(titleMetrics.elidedText(m_fullTitle, Qt::ElideRight, kThumbWidth));
    m_titleLabel->setToolTip(m_fullTitle);
    layout->addWidget(m_titleLabel);

    // ---- Date ----
    m_dateLabel = new QLabel(this);
    m_dateLabel->setObjectName(QStringLiteral("MemoCardDate"));
    m_dateLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    if (entry.created.isValid()) {
        m_dateLabel->setText(entry.created.toString(QStringLiteral("MM-dd HH:mm")));
        m_dateLabel->setToolTip(entry.created.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    }
    layout->addWidget(m_dateLabel);

    // ---- Delete button (overlay, only while hovered) ----
    // Not added to the layout: it floats above the thumbnail's top-right corner.
    m_deleteButton = new QToolButton(this);
    m_deleteButton->setObjectName(QStringLiteral("MemoDeleteButton"));
    m_deleteButton->setText(QStringLiteral("\u00D7"));
    m_deleteButton->setFixedSize(20, 20);
    m_deleteButton->setCursor(Qt::PointingHandCursor);
    m_deleteButton->setToolTip(memoString("Delete memo"));
    m_deleteButton->setFocusPolicy(Qt::NoFocus);
    m_deleteButton->hide();
    connect(m_deleteButton, &QToolButton::clicked, this, [this]() {
        if (m_deleteHandler) m_deleteHandler(m_memoId);
    });

    positionDeleteButton();
}

void MemoCard::applyTheme()
{
    // One stylesheet for all children: the card frame itself is painted by
    // paintEvent(), so no rule targets QFrame#MemoCard.
    const QString sheet = QStringLiteral(
        "QLabel#MemoThumbnail {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 6px;"
        "}"
        "QLabel#MemoCardTitle {"
        "  color: %3;"
        "  background: transparent;"
        "}"
        "QLabel#MemoCardDate {"
        "  color: %4;"
        "  background: transparent;"
        "  font-size: 10px;"
        "}"
        "QToolButton#MemoDeleteButton {"
        "  background-color: %5;"
        "  color: %6;"
        "  border: 1px solid %7;"
        "  border-radius: 10px;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "  padding: 0px;"
        "}"
        "QToolButton#MemoDeleteButton:hover {"
        "  background-color: %8;"
        "  color: %9;"
        "  border-color: %8;"
        "}"
    )
        .arg(m_colors.thumbBg.name())
        .arg(m_colors.thumbBorder.name())
        .arg(m_colors.text.name())
        .arg(m_colors.textMuted.name())
        .arg(m_colors.buttonBg.name())
        .arg(m_colors.buttonText.name())
        .arg(m_colors.buttonBorder.name())
        .arg(m_colors.deleteHover.name())
        .arg(m_colors.deleteHoverText.name());
    setStyleSheet(sheet);

    update();
}

QPixmap MemoCard::renderedThumbnail(const QImage& image) const
{
    if (image.isNull()) return placeholderThumbnail();

    // Scale to device pixels so the (cached) pixmap stays crisp on HiDPI; the
    // device pixel ratio is carried on the pixmap, so QLabel still lays it out
    // as a kThumbWidth x kThumbHeight box.
    const qreal dpr = devicePixelRatioF();
    const QSize target(qRound(kThumbWidth * dpr), qRound(kThumbHeight * dpr));
    QImage scaled = image.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (scaled.isNull()) return placeholderThumbnail();
    scaled.setDevicePixelRatio(dpr);
    QPixmap pixmap = QPixmap::fromImage(scaled);
    // fromImage() does not always carry the ratio over; state it explicitly so
    // the label keeps its logical 124x96 box on HiDPI screens.
    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
}

QPixmap MemoCard::placeholderThumbnail() const
{
    const qreal dpr = devicePixelRatioF();
    QPixmap pixmap(QSize(qRound(kThumbWidth * dpr), qRound(kThumbHeight * dpr)));
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF box(0.5, 0.5, kThumbWidth - 1.0, kThumbHeight - 1.0);
    painter.setPen(QPen(m_colors.thumbBorder, 1.0));
    painter.setBrush(m_colors.placeholderBg);
    painter.drawRoundedRect(box, 6.0, 6.0);

    QFont font = painter.font();
    font.setPointSize(qMax(8, font.pointSize() - 1));
    painter.setFont(font);
    painter.setPen(m_colors.placeholderText);
    painter.drawText(box, Qt::AlignCenter, memoString("Missing image"));
    return pixmap;
}

void MemoCard::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    // Painted by hand rather than through a QSS rule: a QFrame subclass only
    // gets its own background from a stylesheet via a QStyle::PE_Widget
    // round-trip, and switching the hover colour here is far cheaper than
    // re-polishing (or re-building) the card.
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Inset by half a pixel so the 1 px border lands on the pixel grid.
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(m_colors.cardBorder, 1.0));
    painter.setBrush(m_hovered ? m_colors.cardHover : m_colors.cardBg);
    painter.drawRoundedRect(box, kCardRadius, kCardRadius);
}

void MemoCard::resizeEvent(QResizeEvent* event)
{
    QFrame::resizeEvent(event);
    positionDeleteButton();
}

void MemoCard::positionDeleteButton()
{
    if (!m_deleteButton) return;
    m_deleteButton->move(width() - m_deleteButton->width() - 8, 8);
    m_deleteButton->raise();
}

bool MemoCard::event(QEvent* event)
{
    switch (event->type()) {
    case QEvent::Enter:
        setHovered(true);
        break;
    case QEvent::Leave:
        // Qt only sends Leave once the pointer is outside the card *and* all of
        // its children, so moving onto the delete button keeps the card hot and
        // the button clickable.
        setHovered(false);
        break;
    default:
        break;
    }
    return QFrame::event(event);
}

void MemoCard::setHovered(bool hovered)
{
    if (m_hovered == hovered) return;
    m_hovered = hovered;
    if (m_deleteButton) m_deleteButton->setVisible(hovered);
    update();
}

void MemoCard::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        event->accept();
        return;
    }
    QFrame::mousePressEvent(event);
}

void MemoCard::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_pressed) {
        m_pressed = false;
        // Only a release inside the card counts as a click: a press that dragged
        // out (e.g. the reader started scrolling from the tile) must not open a
        // floating card.
        if (rect().contains(mouseLocalPos(event))) {
            event->accept();
            if (m_openHandler) m_openHandler(m_memoId);
            return;
        }
    }
    QFrame::mouseReleaseEvent(event);
}

void MemoCard::contextMenuEvent(QContextMenuEvent* event)
{
    if (m_contextMenuHandler) {
        event->accept();
        m_contextMenuHandler(m_memoId, event->globalPos());
        return;
    }
    QFrame::contextMenuEvent(event);
}

// ============================================================================
// Grid re-flow
// ============================================================================
/**
 * @brief Re-place every card held by @p grid for a @p viewportWidth px viewport.
 *
 * The cards are the grid's items; they are taken out and re-added so the column
 * count can follow the sidebar width. Stretch factors are reset first, otherwise
 * the absorbing row/column from the previous flow keeps its share of the space.
 */
void relayoutMemoGrid(QGridLayout* grid, int viewportWidth)
{
    if (!grid) return;

    QVector<QWidget*> cards;
    cards.reserve(grid->count());
    while (QLayoutItem* item = grid->takeAt(0)) {
        if (QWidget* widget = item->widget()) cards.append(widget);
        delete item;  // takeAt() hands ownership of the layout item to us
    }

    // Newest first, independent of whatever order the layout hands items back.
    std::sort(cards.begin(), cards.end(), [](QWidget* left, QWidget* right) {
        return left->property(kMemoOrderProperty).toInt()
             < right->property(kMemoOrderProperty).toInt();
    });

    for (int row = 0; row < grid->rowCount(); ++row) grid->setRowStretch(row, 0);
    for (int column = 0; column < grid->columnCount(); ++column) grid->setColumnStretch(column, 0);

    const int columns = columnsForViewport(viewportWidth);
    for (int index = 0; index < cards.size(); ++index) {
        grid->addWidget(cards.at(index), index / columns, index % columns,
                        Qt::AlignTop | Qt::AlignHCenter);
    }

    // An empty trailing row soaks up the extra height when the viewport is taller
    // than the grid, which keeps the tiles packed against the header.
    const int usedRows = cards.isEmpty()
        ? 0
        : static_cast<int>((cards.size() + columns - 1) / columns);
    grid->setRowStretch(usedRows, 1);
}

/**
 * @brief Reports viewport resizes to a callback.
 *
 * The gallery header is frozen, so the re-flow hook cannot be a member
 * eventFilter() override; a throwaway QObject owned by the gallery does the same
 * job. It needs no Q_OBJECT - eventFilter() is a plain QObject virtual.
 */
class ViewportResizeFilter final : public QObject {
public:
    explicit ViewportResizeFilter(std::function<void()> onResize,
                                  QObject* parent = nullptr)
        : QObject(parent)
        , m_onResize(std::move(onResize))
    {
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Resize && m_onResize) m_onResize();
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void()> m_onResize;
};

}  // namespace

// ============================================================================
// Construction
// ============================================================================

MemoGalleryWidget::MemoGalleryWidget(QWidget* parent)
    : QWidget(parent)
{
    // Same heuristic the sidebar uses, so the very first paint already matches
    // the application palette even if setDarkMode() never arrives.
    m_darkMode = palette().color(QPalette::Window).lightness() < 128;

    setupUi();
    applyStyle();
    refresh();  // Nothing to show yet: renders the empty state.
}

// ============================================================================
// UI construction
// ============================================================================

void MemoGalleryWidget::setupUi()
{
    m_rootLayout = new QVBoxLayout(this);
    m_rootLayout->setContentsMargins(0, 0, 0, 0);
    m_rootLayout->setSpacing(0);

    // ---- Header row: "Memos (n)" + import button ----
    auto* headerRow = new QWidget(this);
    headerRow->setObjectName(QStringLiteral("MemoHeaderRow"));
    auto* headerLayout = new QHBoxLayout(headerRow);
    headerLayout->setContentsMargins(12, 12, 12, 8);
    headerLayout->setSpacing(8);

    m_headerLabel = new QLabel(tr("Memos (%1)").arg(0), headerRow);
    m_headerLabel->setObjectName(QStringLiteral("MemoHeaderLabel"));

    m_importButton = new QPushButton(QStringLiteral("+"), headerRow);
    m_importButton->setObjectName(QStringLiteral("MemoImportButton"));
    m_importButton->setFixedSize(32, 32);
    m_importButton->setCursor(Qt::PointingHandCursor);
    m_importButton->setFocusPolicy(Qt::NoFocus);
    m_importButton->setToolTip(tr("Import an image as a memo"));
    // clicked(bool) -> importImageRequested() without the unused bool.
    connect(m_importButton, &QPushButton::clicked, this, [this]() {
        emit importImageRequested();
    });

    headerLayout->addWidget(m_headerLabel);
    headerLayout->addStretch(1);
    headerLayout->addWidget(m_importButton);
    m_rootLayout->addWidget(headerRow);

    // ---- Thumbnail grid inside a scroll area ----
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName(QStringLiteral("MemoScrollArea"));
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setContentsMargins(0, 0, 0, 0);
    // The sidebar's own surface must show through: neither the viewport nor the
    // grid host may paint a background of its own.
    m_scrollArea->viewport()->setAutoFillBackground(false);

    m_gridHost = new QWidget();  // setWidget() reparents it into the viewport
    m_gridHost->setObjectName(QStringLiteral("MemoGridHost"));
    m_gridHost->setAutoFillBackground(false);
    m_scrollArea->setWidget(m_gridHost);

    m_gridLayout = new QGridLayout(m_gridHost);
    m_gridLayout->setContentsMargins(kGridMargin, kGridMargin, kGridMargin, kGridMargin);
    m_gridLayout->setSpacing(kGridSpacing);

    m_scrollArea->hide();
    m_rootLayout->addWidget(m_scrollArea, 1);

    // ---- Empty state ----
    m_emptyLabel = new QLabel(this);
    m_emptyLabel->setObjectName(QStringLiteral("MemoEmptyLabel"));
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    m_emptyLabel->hide();
    m_rootLayout->addWidget(m_emptyLabel, 1);

    // The column count depends on the viewport width, which only the scroll area
    // knows about; an event filter is the one hook that needs no header change.
    auto* resizeFilter = new ViewportResizeFilter([this]() {
        if (!m_scrollArea || !m_gridLayout) return;
        relayoutMemoGrid(m_gridLayout, m_scrollArea->viewport()->width());
    }, this);
    m_scrollArea->viewport()->installEventFilter(resizeFilter);
}

void MemoGalleryWidget::applyStyle()
{
    const MemoColors colors = memoColorsFor(m_darkMode);

    // Only the gallery-owned widgets are styled here; cards carry their own
    // palette (a card has to repaint independently when it is hovered).
    QString sheet = QStringLiteral(
        "QLabel#MemoHeaderLabel {"
        "  color: %1;"
        "  font-weight: 600;"
        "  background: transparent;"
        "}"
        "QPushButton#MemoImportButton {"
        "  background-color: %2;"
        "  color: %3;"
        "  border: 1px solid %4;"
        "  border-radius: 8px;"
        "  font-size: 17px;"
        "  font-weight: 600;"
        "  padding: 0px;"
        "}"
        "QPushButton#MemoImportButton:hover {"
        "  background-color: %5;"
        "}"
        "QPushButton#MemoImportButton:pressed {"
        "  background-color: %6;"
        "}"
        "QScrollArea#MemoScrollArea {"
        "  background: transparent;"
        "  border: none;"
        "}"
        // The standard recipe for a transparent scroll area: the viewport's
        // child (our grid host) must not paint a background either.
        "QScrollArea#MemoScrollArea > QWidget > QWidget {"
        "  background: transparent;"
        "}"
        "QLabel#MemoEmptyLabel {"
        "  color: %7;"
        "  background: transparent;"
        "}"
        "QWidget#MemoHeaderRow {"
        "  background: transparent;"
        "}"
        "QWidget#MemoGridHost {"
        "  background: transparent;"
        "}"
    );
    sheet = sheet.arg(colors.text.name())
                 .arg(colors.buttonBg.name())
                 .arg(colors.buttonText.name())
                 .arg(colors.buttonBorder.name())
                 .arg(colors.cardHover.name())
                 .arg(colors.pressed.name())
                 .arg(colors.textMuted.name());
    setStyleSheet(sheet);

    if (m_scrollArea) {
        // A QScrollArea's viewport keeps painting QPalette::Base from the style
        // unless it is addressed directly; this makes the panel one surface.
        m_scrollArea->viewport()->setStyleSheet(QStringLiteral("background: transparent;"));
    }
}

// ============================================================================
// Document / refresh
// ============================================================================

void MemoGalleryWidget::setDocument(Document* document)
{
    // Same document: the grid is already correct. Refreshes caused by memo
    // changes come through refresh() explicitly, so this avoids re-decoding
    // every thumbnail on unrelated events (sidebar toggles, pane switches).
    if (m_document == document) {
        return;
    }
    m_document = document;
    refresh();
}

void MemoGalleryWidget::refresh()
{
    clearCards();

    // Cast: QVector::size() is qsizetype in Qt6 and these are plain ints.
    const int memoCount = m_document ? static_cast<int>(m_document->memos().size()) : 0;
    if (m_headerLabel) m_headerLabel->setText(tr("Memos (%1)").arg(memoCount));

    if (!m_document || memoCount == 0) {
        showEmptyState(tr("No memos yet"));
        return;
    }

    if (m_emptyLabel) m_emptyLabel->hide();
    if (m_scrollArea) m_scrollArea->show();

    const QVector<MemoEntry>& memos = m_document->memos();

    // Context menu lives here rather than in the card: renaming needs this widget
    // as the dialog parent and every entry ends in one of our signals.
    const auto showContextMenu = [this](const QString& memoId, const QPoint& globalPos) {
        if (!m_document) return;

        // Copy what the menu needs: the document may change while the menu is
        // modal (an action handler elsewhere could rebuild the memo list).
        QString title;
        int sourcePage = -1;
        bool found = false;
        for (const MemoEntry& entry : m_document->memos()) {
            if (entry.id != memoId) continue;
            title = entry.title;
            sourcePage = entry.sourcePage;
            found = true;
            break;
        }
        if (!found) return;

        QMenu menu(this);
        ThemeColors::styleMenu(&menu, m_darkMode);

        QAction* openAction = menu.addAction(tr("Open"));
        QAction* renameAction = menu.addAction(tr("Rename\u2026"));
        QAction* deleteAction = menu.addAction(tr("Delete\u2026"));
        QAction* locateAction = nullptr;
        if (sourcePage >= 0) {
            menu.addSeparator();
            locateAction = menu.addAction(tr("Go to source page"));
        }

        QAction* chosen = menu.exec(globalPos);
        if (!chosen) return;

        if (chosen == openAction) {
            emit memoOpenRequested(memoId);
        } else if (chosen == renameAction) {
            bool accepted = false;
            const QString newTitle = QInputDialog::getText(
                this, tr("Rename memo"), tr("Memo title:"), QLineEdit::Normal,
                title, &accepted).trimmed();
            // Empty and unchanged names are ignored, not sent to the document.
            if (accepted && !newTitle.isEmpty() && newTitle != title) {
                emit memoRenameRequested(memoId, newTitle);
            }
        } else if (chosen == deleteAction) {
            // The window asks for confirmation before anything is removed.
            emit memoDeleteRequested(memoId);
        } else if (locateAction && chosen == locateAction) {
            emit memoLocateRequested(sourcePage);
        }
    };

    // One card per memo, newest first: Document::memos() is insertion order
    // (oldest first) and a gallery should show what was just captured on top.
    if (!m_gridLayout || !m_gridHost) return;

    int order = 0;
    for (int index = memoCount - 1; index >= 0; --index) {
        const MemoEntry& entry = memos.at(index);
        if (!entry.isValid()) continue;

        auto* card = new MemoCard(entry, m_document->memoImage(entry.id),
                                  m_darkMode, m_gridHost);
        card->setProperty(kMemoOrderProperty, order++);
        card->setOpenHandler([this](const QString& memoId) {
            emit memoOpenRequested(memoId);
        });
        card->setDeleteHandler([this](const QString& memoId) {
            emit memoDeleteRequested(memoId);
        });
        card->setContextMenuHandler(showContextMenu);

        // Insert into the grid so the re-flow helper (and the resize filter,
        // which only reaches the layout) can find it again.
        m_gridLayout->addWidget(card, 0, 0, Qt::AlignTop | Qt::AlignHCenter);
    }

    // The real column count follows the viewport; a resize while the sidebar is
    // hidden is corrected by the filter right after the first show.
    const int viewportWidth = m_scrollArea ? m_scrollArea->viewport()->width() : 0;
    relayoutMemoGrid(m_gridLayout, viewportWidth);
}

void MemoGalleryWidget::clearCards()
{
    if (!m_gridLayout || !m_gridHost) return;

    while (QLayoutItem* item = m_gridLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            // The widget is out of the layout already but would stay visible for
            // one event-loop turn; hide it before deleteLater() takes over.
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }

    // Re-create the layout instead of just emptying it: QGridLayout remembers its
    // row/column geometry between flows, which would leave stale rows (and thus
    // gaps) behind after the memo count drops.
    delete m_gridLayout;
    m_gridLayout = new QGridLayout(m_gridHost);
    m_gridLayout->setContentsMargins(kGridMargin, kGridMargin, kGridMargin, kGridMargin);
    m_gridLayout->setSpacing(kGridSpacing);
}

void MemoGalleryWidget::showEmptyState(const QString& text)
{
    if (m_emptyLabel) {
        m_emptyLabel->setText(text);
        m_emptyLabel->show();
    }
    if (m_scrollArea) m_scrollArea->hide();
}

// ============================================================================
// Theme
// ============================================================================

void MemoGalleryWidget::setDarkMode(bool darkMode)
{
    if (m_darkMode == darkMode) return;
    m_darkMode = darkMode;

    applyStyle();
    // Rebuilding the cards is the simplest way to get the right thumbnail
    // borders/placeholders: a card's palette is fixed at construction. Decoding
    // every memo image again is acceptable here - a theme switch is rare and the
    // gallery only holds a sidebar's worth of thumbnails.
    refresh();
}
