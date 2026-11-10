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
#include <QKeyEvent>
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
#include <QScrollBar>
#include <QSizePolicy>
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
// One memo per row. The card takes the full sidebar content width and derives
// its thumbnail height from the memo's own aspect ratio, so a wide screenshot
// gets a short strip and a tall handwriting page gets a taller picture - while
// a very tall memo is capped (see kThumbMaxHeightRatio) and scaled down instead
// of eating the whole sidebar.
constexpr int kCardPadding = 6;          ///< Space inside the card frame
constexpr int kGridMargin = 10;
constexpr int kGridSpacing = 10;
constexpr int kCardRadius = 10;
constexpr int kThumbMinHeight = 96;
/// Tallest thumbnail, as a fraction of the visible gallery height.
constexpr qreal kThumbMaxHeightRatio = 0.6;

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

/// Tallest a thumbnail may become for a @p viewportHeight px tall gallery.
int maxThumbnailHeightFor(int viewportHeight)
{
    return qMax(kThumbMinHeight,
                qRound(qMax(1, viewportHeight) * kThumbMaxHeightRatio));
}

/// Single-line title label that keeps its full text and elides to the width the
/// layout gives it (the card width changes with the sidebar, so the elision has
/// to be recomputed on every resize).
///
/// It also owns the double-click rename gesture and deliberately swallows single
/// clicks: a click on the title must not open the memo, or the first half of a
/// double-click would already have opened a floating card.
class ElidedTitleLabel final : public QLabel {
public:
    explicit ElidedTitleLabel(QWidget* parent = nullptr)
        : QLabel(parent)
    {
        // Ignored horizontal: the label must never impose its (full) text width
        // as a minimum, otherwise a long title would stop the card - and with it
        // the whole gallery - from narrowing to the sidebar.
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        setTextInteractionFlags(Qt::NoTextInteraction);
        setMinimumWidth(0);
    }

    void setFullText(const QString& text)
    {
        m_fullText = text;
        setToolTip(text);
        updateElidedText();
    }
    QString fullText() const { return m_fullText; }

    /// Invoked on double-click (the card replaces the label with an editor).
    std::function<void()> onDoubleClick;

    // Ignored policy already reports a 0 minimum width; keeping the height to a
    // single text line makes the card's sizeHint independent of the title.
    QSize minimumSizeHint() const override
    {
        return QSize(0, fontMetrics().height());
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        updateElidedText();
    }

    void mousePressEvent(QMouseEvent* event) override { event->accept(); }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        event->accept();
        if (onDoubleClick) {
            onDoubleClick();
        }
    }

private:
    void updateElidedText()
    {
        QLabel::setText(fontMetrics().elidedText(m_fullText, Qt::ElideRight,
                                                 qMax(0, width())));
    }

    QString m_fullText;
};

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
    using RenameHandler = std::function<void(const QString&, const QString&)>;
    using ContextHandler = std::function<void(const QString&, const QPoint&)>;

    MemoCard(const MemoEntry& entry, const QImage& image, bool darkMode,
             QWidget* parent = nullptr);

    QString memoId() const { return m_memoId; }

    void setOpenHandler(IdHandler handler) { m_openHandler = std::move(handler); }
    void setDeleteHandler(IdHandler handler) { m_deleteHandler = std::move(handler); }
    void setRenameHandler(RenameHandler handler) { m_renameHandler = std::move(handler); }
    void setContextMenuHandler(ContextHandler handler) { m_contextMenuHandler = std::move(handler); }

    /// Cap for the thumbnail height; recomputed by the gallery whenever the
    /// visible height changes (the tall-image limit is relative to it).
    void setMaxThumbnailHeight(int height);
    int maxThumbnailHeight() const { return m_maxThumbnailHeight; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void buildUi(const MemoEntry& entry, const QImage& image);
    void applyTheme();
    void setHovered(bool hovered);
    void positionDeleteButton();
    void updateThumbnail();

    void beginTitleEdit();
    void finishTitleEdit(bool commit);

    /// Thumbnail pixmap for a @p width x @p height box, preserving the memo's
    /// aspect ratio (the unused axis is letterboxed by the label's centre
    /// alignment, which is what "very tall memo, capped height" looks like).
    QPixmap buildThumbnail(int width, int height) const;
    QPixmap buildPlaceholder(int width, int height) const;

    QString m_memoId;
    QString m_fullTitle;
    QImage m_image;
    qreal m_imageAspect = 4.0 / 3.0;
    int m_maxThumbnailHeight = 320;

    bool m_hovered = false;

    /// True between a left press on the card and its release; a release outside
    /// the card (a drag that left the tile) is not a click.
    bool m_pressed = false;

    MemoColors m_colors;
    QLabel* m_thumbnailLabel = nullptr;
    ElidedTitleLabel* m_titleLabel = nullptr;
    QLabel* m_dateLabel = nullptr;
    QToolButton* m_deleteButton = nullptr;
    QLineEdit* m_titleEditor = nullptr;

    IdHandler m_openHandler;
    IdHandler m_deleteHandler;
    RenameHandler m_renameHandler;
    ContextHandler m_contextMenuHandler;
};

MemoCard::MemoCard(const MemoEntry& entry, const QImage& image, bool darkMode,
                   QWidget* parent)
    : QFrame(parent)
    , m_memoId(entry.id)
    , m_fullTitle(entry.title.isEmpty() ? memoString("Untitled memo") : entry.title)
    , m_image(image)
    , m_colors(memoColorsFor(darkMode))
{
    if (!image.isNull() && image.height() > 0) {
        m_imageAspect = qreal(image.width()) / qreal(image.height());
    }
    if (!qIsFinite(m_imageAspect) || m_imageAspect <= 0.01) {
        m_imageAspect = 4.0 / 3.0;
    }

    setObjectName(QStringLiteral("MemoCard"));
    setCursor(Qt::PointingHandCursor);
    // The card paints its own background (see paintEvent), but WA_Hover is what
    // arms QEvent::Enter/Leave for the hover-only delete button.
    setAttribute(Qt::WA_Hover, true);
    setContextMenuPolicy(Qt::DefaultContextMenu);
    // One card per row: take whatever width the column gives us, keep the height
    // the layout computes from the thumbnail + two text lines.
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setToolTip(m_fullTitle);

    buildUi(entry, image);
    applyTheme();
}

void MemoCard::setMaxThumbnailHeight(int height)
{
    const int clamped = qMax(kThumbMinHeight, height);
    if (m_maxThumbnailHeight == clamped) {
        return;
    }
    m_maxThumbnailHeight = clamped;
    updateThumbnail();
}

void MemoCard::buildUi(const MemoEntry& entry, const QImage& image)
{
    Q_UNUSED(image);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kCardPadding, kCardPadding, kCardPadding, kCardPadding);
    layout->setSpacing(4);
    // No SetFixedSize: the card is as wide as the column and as tall as its
    // rows (the thumbnail's height is driven by the card width, see
    // updateThumbnail()).

    // ---- Thumbnail ----
    m_thumbnailLabel = new QLabel(this);
    m_thumbnailLabel->setObjectName(QStringLiteral("MemoThumbnail"));
    m_thumbnailLabel->setAlignment(Qt::AlignCenter);
    // Ignored horizontal: a QLabel's minimum width is derived from its pixmap,
    // so without this the card could never become narrower than the thumbnail
    // it was last scaled for - it would overflow the sidebar when the sidebar
    // was dragged narrower (the gallery has no horizontal scrollbar). The pixmap
    // is re-scaled to the real label width by updateThumbnail().
    m_thumbnailLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_thumbnailLabel->setMinimumWidth(0);
    m_thumbnailLabel->setMinimumHeight(kThumbMinHeight);
    // Clicks must reach the card itself; labels would otherwise eat the press
    // (and the tooltip of the card would never show).
    m_thumbnailLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    layout->addWidget(m_thumbnailLabel);

    // ---- Title (single elided line, full text in the tooltip) ----
    m_titleLabel = new ElidedTitleLabel(this);
    m_titleLabel->setObjectName(QStringLiteral("MemoCardTitle"));
    m_titleLabel->setFullText(m_fullTitle);
    m_titleLabel->onDoubleClick = [this]() { beginTitleEdit(); };
    // I-beam hints "this text is editable" without a visible button.
    m_titleLabel->setCursor(Qt::IBeamCursor);
    m_titleLabel->setToolTip(
        QStringLiteral("%1\n%2").arg(m_fullTitle, memoString("Double-click to rename")));
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

void MemoCard::updateThumbnail()
{
    if (!m_thumbnailLabel) {
        return;
    }
    const int contentWidth = qMax(40, width() - 2 * kCardPadding);
    const qreal naturalHeight = qreal(contentWidth) / m_imageAspect;
    const int maxHeight = qMax(kThumbMinHeight, m_maxThumbnailHeight);
    const int thumbHeight = qBound(kThumbMinHeight, qRound(naturalHeight), maxHeight);

    m_thumbnailLabel->setFixedHeight(thumbHeight);
    m_thumbnailLabel->setPixmap(buildThumbnail(contentWidth, thumbHeight));
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

QPixmap MemoCard::buildThumbnail(int width, int height) const
{
    if (m_image.isNull() || width <= 0 || height <= 0) {
        return buildPlaceholder(width, height);
    }

    // Scale to device pixels so the pixmap stays crisp on HiDPI; the ratio is
    // carried on the pixmap, so QLabel lays it out at the requested logical size.
    const qreal dpr = devicePixelRatioF();
    const QSize target(qRound(width * dpr), qRound(height * dpr));
    QImage scaled = m_image.scaled(target, Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation);
    if (scaled.isNull()) {
        return buildPlaceholder(width, height);
    }
    scaled.setDevicePixelRatio(dpr);
    QPixmap pixmap = QPixmap::fromImage(scaled);
    // fromImage() does not always carry the ratio over; state it explicitly.
    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
}

QPixmap MemoCard::buildPlaceholder(int width, int height) const
{
    const QSize logical(qMax(1, width), qMax(1, height));
    const qreal dpr = devicePixelRatioF();
    QPixmap pixmap(logical * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF box(0.5, 0.5, logical.width() - 1.0, logical.height() - 1.0);
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
    // The card width follows the sidebar, so the thumbnail's height (derived
    // from the memo's aspect ratio) and the scaled pixmap change with it.
    updateThumbnail();
    if (m_titleEditor && m_titleLabel) {
        m_titleEditor->setGeometry(m_titleLabel->geometry());
    }
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

bool MemoCard::eventFilter(QObject* watched, QEvent* event)
{
    if (m_titleEditor && watched == m_titleEditor) {
        if (event->type() == QEvent::KeyPress) {
            auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Escape) {
                finishTitleEdit(false);
                return true;
            }
        }
    }
    return QFrame::eventFilter(watched, event);
}

void MemoCard::beginTitleEdit()
{
    if (!m_titleLabel || m_titleEditor) {
        return;
    }
    auto* editor = new QLineEdit(this);
    editor->setObjectName(QStringLiteral("MemoTitleEditor"));
    editor->setText(m_fullTitle);
    editor->selectAll();
    // A line edit needs a couple of pixels more than the elided label it covers;
    // the card is tall enough (the date row sits below with spacing).
    const QRect labelRect = m_titleLabel->geometry();
    editor->setGeometry(labelRect.adjusted(-4, -3, 4, 3));
    editor->setStyleSheet(QStringLiteral(
        "QLineEdit#MemoTitleEditor {"
        "  border: 1px solid #0078d4;"
        "  border-radius: 4px;"
        "  padding: 1px 4px;"
        "  background: %1;"
        "  color: %2;"
        "  font-size: 13px;"
        "}")
        .arg(m_colors.cardBg.name(), m_colors.text.name()));
    editor->installEventFilter(this);
    editor->raise();
    editor->show();
    editor->setFocus(Qt::MouseFocusReason);

    m_titleEditor = editor;
    // editingFinished also fires on focus loss, so clicking elsewhere commits.
    connect(editor, &QLineEdit::editingFinished, this, [this]() {
        finishTitleEdit(true);
    });
}

void MemoCard::finishTitleEdit(bool commit)
{
    if (!m_titleEditor) {
        return;
    }
    QLineEdit* editor = m_titleEditor;
    m_titleEditor = nullptr;
    const QString typed = editor->text().trimmed();
    editor->hide();
    editor->deleteLater();

    if (commit && !typed.isEmpty() && typed != m_fullTitle) {
        m_fullTitle = typed;
        if (m_titleLabel) {
            m_titleLabel->setFullText(typed);
            m_titleLabel->setToolTip(QStringLiteral("%1\n%2")
                .arg(typed, memoString("Double-click to rename")));
        }
        setToolTip(typed);
        if (m_renameHandler) {
            m_renameHandler(m_memoId, typed);
        }
    }
}

// ============================================================================
// Grid re-flow
// ============================================================================
/**
 * @brief Re-place every card held by @p grid: one card per row, full width.
 *
 * The cards are taken out and re-added so a stale stretch/row state cannot
 * survive a rebuild. Cards are stretched across the single column (no alignment
 * flags), so their width follows the sidebar and their thumbnail height follows
 * the width (see MemoCard::updateThumbnail()).
 */
void relayoutMemoGrid(QGridLayout* grid)
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

    for (int index = 0; index < cards.size(); ++index) {
        grid->addWidget(cards.at(index), index, 0);
    }
    grid->setColumnStretch(0, 1);

    // An empty trailing row soaks up the extra height when the viewport is taller
    // than the grid, which keeps the cards packed against the header.
    grid->setRowStretch(cards.size(), 1);
}

/**
 * @brief Push the current visible height into every card, so a very tall memo
 *        cannot make one card eat the whole gallery.
 *
 * Cards own the thumbnail height (it also depends on their width), so the limit
 * is only a cap; MemoCard::updateThumbnail() clamps against it.
 */
void applyThumbnailLimits(QGridLayout* grid, int viewportHeight)
{
    if (!grid) return;
    const int limit = maxThumbnailHeightFor(viewportHeight);
    for (int i = 0; i < grid->count(); ++i) {
        QLayoutItem* item = grid->itemAt(i);
        if (!item) continue;
        if (auto* card = dynamic_cast<MemoCard*>(item->widget())) {
            card->setMaxThumbnailHeight(limit);
        }
    }
}

/**
 * @brief Keep the card width constant while the vertical scrollbar appears and
 *        disappears.
 *
 * The thumbnail height is derived from the card width, and the card height
 * drives whether the scrollbar is needed - a classic feedback loop: the bar
 * takes ~12 px off the viewport, which shrinks every card, which can make the
 * content fit again, which removes the bar. Reserving the bar's width on the
 * right when it is hidden keeps the content width identical in both states, so
 * the loop has nothing to oscillate.
 */
void syncScrollbarMargin(QScrollArea* area, QGridLayout* grid)
{
    if (!area || !grid) return;
    const QScrollBar* bar = area->verticalScrollBar();
    const int barWidth = bar ? bar->sizeHint().width() : 0;
    const bool barVisible = bar && bar->isVisible();
    grid->setContentsMargins(kGridMargin, kGridMargin,
                             kGridMargin + (barVisible ? 0 : barWidth),
                             kGridMargin);
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

    // Card width/height both follow the scroll area's viewport (width drives the
    // card width, height drives the tall-thumbnail cap), and only the scroll
    // area knows its viewport size. An event filter is the one hook that needs
    // no header change.
    auto* resizeFilter = new ViewportResizeFilter([this]() {
        if (!m_scrollArea || !m_gridLayout) return;
        syncScrollbarMargin(m_scrollArea, m_gridLayout);
        relayoutMemoGrid(m_gridLayout);
        applyThumbnailLimits(m_gridLayout, m_scrollArea->viewport()->height());
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
        card->setRenameHandler([this](const QString& memoId, const QString& newTitle) {
            emit memoRenameRequested(memoId, newTitle);
        });
        card->setContextMenuHandler(showContextMenu);

        // Insert into the grid so the re-flow helper (and the resize filter,
        // which only reaches the layout) can find it again.
        m_gridLayout->addWidget(card, 0, 0);
    }

    // One card per row; the width comes from the viewport and the height from
    // each memo's aspect ratio. A rebuild while the sidebar is hidden is
    // corrected by the resize filter right after the first show.
    syncScrollbarMargin(m_scrollArea, m_gridLayout);
    relayoutMemoGrid(m_gridLayout);
    applyThumbnailLimits(m_gridLayout,
                         m_scrollArea ? m_scrollArea->viewport()->height() : 0);
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
