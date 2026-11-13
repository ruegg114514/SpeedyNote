#include "MemoManager.h"

#include "MemoFloatWidget.h"
#include "../core/Document.h"
#include "../core/DocumentViewport.h"
#include "../core/MemoEntry.h"

#include <QEvent>
#include <QPixmap>
#include <QSettings>
#include <QStringList>
#include <QtAlgorithms>
#include <QtGlobal>

MemoManager::MemoManager(QWidget* host, QObject* parent)
    : QObject(parent)
    , m_host(host)
{
    // Reader preference (not notebook data): cards keep their proportions when
    // resized unless the little lock button was switched off.
    QSettings settings(QStringLiteral("SpeedyNote"), QStringLiteral("App"));
    m_aspectRatioLocked = settings.value(QStringLiteral("memos/aspectRatioLocked"),
                                         true).toBool();

    if (m_host) {
        // Cards are children of the host; if the window tears the host down
        // first they are already gone, so the destructor must not touch them.
        connect(m_host, &QObject::destroyed, this, [this]() {
            m_host = nullptr;
            m_cards.clear();
        });
    }
}

MemoManager::~MemoManager()
{
    if (!m_host) {
        return;
    }
    // We own the cards logically; deleting them here is safe while the host is
    // alive (QWidget removes them from its child list itself).
    qDeleteAll(m_cards);
    m_cards.clear();
}

void MemoManager::setContext(Document* document, DocumentViewport* viewport)
{
    if (document == m_document && viewport == m_viewport) {
        return;
    }

    // Remember which cards were open for the document we are leaving, so a tab
    // switch away and back restores the cards the reader had on screen. The key
    // is the session id, not the Document pointer: a deleted document's address
    // can be reused by the next one.
    if (m_document) {
        const QString documentKey = m_document->sessionId();
        QSet<QString> open;
        for (auto it = m_cards.constBegin(); it != m_cards.constEnd(); ++it) {
            open.insert(it.key());
        }
        if (open.isEmpty()) {
            m_openByDocument.remove(documentKey);
        } else {
            m_openByDocument[documentKey] = open;
        }
    }

    clearCards();
    if (m_viewport) {
        m_viewport->removeEventFilter(this);
    }

    m_document = document;
    m_viewport = viewport;

    if (m_viewport) {
        // Deactivation on a press on the page itself (see eventFilter).
        m_viewport->installEventFilter(this);

        // The viewport is also the wheel-forwarding target of every card. A tab
        // close deletes it right after the tabCloseRequested handler ran, so the
        // manager must drop the pointer instead of leaving a dangling one for
        // the next tab switch to dereference.
        DocumentViewport* watchedViewport = m_viewport;
        connect(watchedViewport, &QObject::destroyed, this,
                [this, watchedViewport]() {
            if (m_viewport == watchedViewport) {
                m_viewport = nullptr;
            }
        });
    }

    if (!m_document) {
        // No document is a close, not a tab switch: the session is over, and the
        // remembered open cards go with it.
        m_openByDocument.clear();
        return;
    }

    const QSet<QString> open = m_openByDocument.value(m_document->sessionId());
    for (const QString& memoId : open) {
        openMemo(memoId);
    }
}

void MemoManager::updateHostGeometry(const QRect& viewportRectInHost)
{
    if (viewportRectInHost == m_viewportRect) {
        return;
    }
    // Card positions are remembered in viewport coordinates; when the viewport
    // moves inside the host (split view, pane focus) the cards move with it.
    const QPoint delta = viewportRectInHost.topLeft() - m_viewportRect.topLeft();
    m_viewportRect = viewportRectInHost;

    for (MemoFloatWidget* card : m_cards) {
        if (!card) {
            continue;
        }
        if (!delta.isNull()) {
            QRectF rect = card->cardRect();
            rect.translate(delta);
            card->applyStoredGeometry(rect, card->rotation());
        }
        clampCardIntoHost(card);
    }
}

bool MemoManager::openMemo(const QString& memoId)
{
    if (!m_document || !m_host) {
        return false;
    }
    const MemoEntry* entry = m_document->memoById(memoId);
    if (!entry) {
        return false;
    }

    MemoFloatWidget* card = m_cards.value(memoId);
    if (!card) {
        card = createCard(*entry);
        if (!card) {
            return false;
        }
        m_cards.insert(memoId, card);
    }

    card->show();
    card->raise();
    activateCard(card);
    m_openByDocument[m_document->sessionId()].insert(memoId);
    return true;
}

void MemoManager::closeMemo(const QString& memoId)
{
    MemoFloatWidget* card = m_cards.take(memoId);
    if (m_document) {
        m_openByDocument[m_document->sessionId()].remove(memoId);
    }
    if (!card) {
        return;
    }
    // The viewport may die before this deleteLater is processed; detach the
    // wheel-forwarding target now so the card cannot touch a freed widget.
    card->setScrollTarget(nullptr);
    card->hide();
    card->deleteLater();
}

void MemoManager::closeAll()
{
    const QStringList ids = m_cards.keys();
    for (const QString& id : ids) {
        closeMemo(id);
    }
}

bool MemoManager::isMemoOpen(const QString& memoId) const
{
    return m_cards.contains(memoId);
}

void MemoManager::handleMemoRemoved(const QString& memoId)
{
    closeMemo(memoId);
}

void MemoManager::handleMemoRenamed(const QString& memoId, const QString& title)
{
    if (MemoFloatWidget* card = m_cards.value(memoId)) {
        card->setTitle(title);
    }
}

void MemoManager::setDarkMode(bool darkMode)
{
    if (m_darkMode == darkMode) {
        return;
    }
    m_darkMode = darkMode;
    for (MemoFloatWidget* card : m_cards) {
        if (card) {
            card->setDarkMode(darkMode);
        }
    }
}

void MemoManager::setAspectRatioLocked(bool locked)
{
    if (m_aspectRatioLocked == locked) {
        return;
    }
    m_aspectRatioLocked = locked;
    QSettings settings(QStringLiteral("SpeedyNote"), QStringLiteral("App"));
    settings.setValue(QStringLiteral("memos/aspectRatioLocked"), locked);

    for (MemoFloatWidget* card : m_cards) {
        if (card) {
            card->setAspectLocked(locked);
        }
    }
}

bool MemoManager::eventFilter(QObject* watched, QEvent* event)
{
    // A press on the page itself deactivates the cards, so their handles stop
    // advertising a card that no longer has the reader's attention. Presses on
    // a card never reach the viewport (the card is a sibling above it).
    if (watched == m_viewport && event->type() == QEvent::MouseButtonPress) {
        deactivateAll();
    }
    return QObject::eventFilter(watched, event);
}

void MemoManager::clearCards()
{
    if (!m_host) {
        m_cards.clear();
        return;
    }
    for (MemoFloatWidget* card : m_cards) {
        if (!card) {
            continue;
        }
        card->setScrollTarget(nullptr);
        card->hide();
        card->deleteLater();
    }
    m_cards.clear();
}

MemoFloatWidget* MemoManager::createCard(const MemoEntry& entry)
{
    if (!m_host || !m_document) {
        return nullptr;
    }
    const QImage image = m_document->memoImage(entry.id);
    auto* card = new MemoFloatWidget(entry.id, QPixmap::fromImage(image), m_host);
    card->setTitle(entry.title);
    card->setDarkMode(m_darkMode);
    card->setScrollTarget(m_viewport);
    card->setAspectLocked(m_aspectRatioLocked);

    // naturalSize is the material's capture-time size, so a fresh card opens at
    // the captured proportions instead of a generic default; the aspect lock
    // then keeps the stored rect honest for memos saved by older builds.
    QRectF rect = entry.hasFloatRect() ? entry.floatRect
                                       : defaultCardRectFor(entry, image.size());
    rect.translate(viewportOffset());
    card->applyStoredGeometry(rect, entry.floatRotation);
    clampCardIntoHost(card);

    connect(card, &MemoFloatWidget::cardGeometryChanged, this,
            [this](const QString& memoId) {
        if (!m_document) {
            return;
        }
        MemoFloatWidget* c = m_cards.value(memoId);
        if (!c) {
            return;
        }
        // Back into viewport coordinates for persistence.
        QRectF rect = c->cardRect();
        rect.translate(-viewportOffset());
        if (m_document->updateMemoFloat(memoId, rect, c->rotation()) && m_viewport) {
            // Mark the tab modified so the "*" appears: the new position is a
            // document change that the next save must persist.
            emit m_viewport->documentModified();
        }
    });
    connect(card, &MemoFloatWidget::closeRequested,
            this, &MemoManager::closeMemo);
    connect(card, &MemoFloatWidget::aspectLockToggled,
            this, &MemoManager::setAspectRatioLocked);
    connect(card, &MemoFloatWidget::activated, this,
            [this](const QString& memoId) {
        activateCard(m_cards.value(memoId));
    });

    return card;
}

void MemoManager::activateCard(MemoFloatWidget* card)
{
    if (!card) {
        return;
    }
    for (MemoFloatWidget* other : m_cards) {
        if (other && other != card) {
            other->setActiveCard(false);
        }
    }
    card->setActiveCard(true);
    card->raise();
}

void MemoManager::deactivateAll()
{
    for (MemoFloatWidget* card : m_cards) {
        if (card) {
            card->setActiveCard(false);
        }
    }
}


QRectF MemoManager::defaultCardRectFor(const MemoEntry& entry,
                                       const QSize& imageSize) const
{
    // Open at the material's own size: the capture-time size when it is known
    // (screenshots/handwriting/imported images all record it), otherwise the
    // decoded image's pixel size. This is what makes the card "the same size"
    // as what was captured; only a card too large for the viewport is scaled
    // down, and that scaling is uniform so the proportions never change.
    QSizeF size;
    if (entry.hasNaturalSize()) {
        size = entry.naturalSize;
    } else if (imageSize.isValid() && !imageSize.isEmpty()) {
        size = QSizeF(imageSize);
    } else {
        size = QSizeF(260.0, 200.0);
    }

    const QRectF viewport(QPointF(0.0, 0.0),
                          m_viewportRect.isEmpty()
                              ? QSizeF(800.0, 600.0)
                              : QSizeF(m_viewportRect.size()));
    // Cascade new cards a little so several opened from the gallery do not
    // stack exactly on top of each other.
    const qreal cascade = 20.0 * (m_cards.size() % 6) - 50.0;
    const QPointF center = viewport.center() + QPointF(cascade, cascade);

    const qreal maxW = qMax(MemoFloatWidget::MIN_CARD_SIZE,
                            viewport.width() - 48.0);
    const qreal maxH = qMax(MemoFloatWidget::MIN_CARD_SIZE,
                            viewport.height() - 48.0);
    qreal scale = 1.0;
    if (size.width() > maxW || size.height() > maxH) {
        scale = qMin(maxW / size.width(), maxH / size.height());
    }
    // Never open smaller than the widget's minimum card, but keep the ratio.
    const qreal minScale = qMax(MemoFloatWidget::MIN_CARD_SIZE / size.width(),
                                MemoFloatWidget::MIN_CARD_SIZE / size.height());
    scale = qMax(scale, minScale);
    const qreal w = size.width() * scale;
    const qreal h = size.height() * scale;
    return QRectF(center.x() - w / 2.0, center.y() - h / 2.0, w, h);
}

void MemoManager::clampCardIntoHost(MemoFloatWidget* card)
{
    if (!card || m_viewportRect.isEmpty()) {
        return;
    }
    const QRectF viewport(m_viewportRect);
    const QRectF rect = card->cardRect();

    qreal x = rect.x();
    qreal y = rect.y();
    if (rect.width() <= viewport.width()) {
        x = qBound(viewport.left(), x, viewport.right() - rect.width());
    } else {
        x = viewport.center().x() - rect.width() / 2.0;
    }
    if (rect.height() <= viewport.height()) {
        y = qBound(viewport.top(), y, viewport.bottom() - rect.height());
    } else {
        y = viewport.center().y() - rect.height() / 2.0;
    }

    if (!qFuzzyCompare(x + 1.0, rect.x() + 1.0)
        || !qFuzzyCompare(y + 1.0, rect.y() + 1.0)) {
        card->applyStoredGeometry(QRectF(x, y, rect.width(), rect.height()),
                                  card->rotation());
    }
}
