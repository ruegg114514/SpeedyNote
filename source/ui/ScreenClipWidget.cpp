#include "ScreenClipWidget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRegion>
#include <QShowEvent>

ScreenClipWidget::ScreenClipWidget(const QPixmap& screenShot, QWidget* parent)
    : QDialog(parent)
    , m_shot(screenShot)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                   | Qt::Tool | Qt::BypassWindowManagerHint);
    setModal(true);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);
    // The caller positions us over the screen the screenshot came from.
}

QRect ScreenClipWidget::normalizedRect(const QPoint& a, const QPoint& b) const
{
    return QRect(QPoint(qMin(a.x(), b.x()), qMin(a.y(), b.y())),
                 QPoint(qMax(a.x(), b.x()), qMax(a.y(), b.y())))
        .intersected(rect());
}

void ScreenClipWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);

    // The screenshot fills the widget; drawPixmap maps the pixmap's device
    // pixel rect onto the widget's logical rect, which handles DPR scaling.
    if (!m_shot.isNull()) {
        painter.drawPixmap(rect(), m_shot, m_shot.rect());
    } else {
        painter.fillRect(rect(), QColor(32, 32, 32));
    }

    // Dark mask over everything EXCEPT the selection, so the chosen region
    // stays bright and readable underneath.
    if (!m_selection.isEmpty()) {
        const QRegion full(rect());
        const QRegion hole(m_selection);
        painter.save();
        painter.setClipRegion(full.subtracted(hole));
        painter.fillRect(rect(), QColor(0, 0, 0, 120));
        painter.restore();

        // Selection border: white outline with a dark hairline inside so it
        // reads on both light and dark content.
        painter.setPen(QPen(QColor(0, 0, 0, 160), 1));
        painter.drawRect(m_selection.adjusted(-1, -1, 0, 0));
        painter.setPen(QPen(QColor(255, 255, 255), 2));
        painter.drawRect(m_selection);

        // Size readout under the selection.
        painter.setPen(Qt::white);
        QFont font = painter.font();
        font.setPointSizeF(10);
        painter.setFont(font);
        const QString sizeText = QStringLiteral("%1 × %2")
            .arg(m_selection.width()).arg(m_selection.height());
        const QPoint textPos(m_selection.left(),
                             m_selection.bottom() + 20);
        painter.drawText(textPos, sizeText);
    } else {
        painter.fillRect(rect(), QColor(0, 0, 0, 120));
    }

    // Instruction hint pinned to the top centre of the screen.
    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPointSizeF(12);
    font.setBold(true);
    painter.setFont(font);
    const QString hint = tr("Drag to select a region  ·  Enter / double-click inserts on the page  ·  Esc cancels");
    const QRect hintRect(0, 16, width(), 24);
    painter.setPen(QColor(0, 0, 0, 180));
    painter.drawText(hintRect.translated(1, 1), Qt::AlignHCenter, hint);
    painter.setPen(Qt::white);
    painter.drawText(hintRect, Qt::AlignHCenter, hint);

    // Tablet-friendly action buttons pinned to the bottom centre. Hit-tested
    // in mousePressEvent so they work with touch taps too.
    const bool canConfirm = hasValidSelection();

    QFont btnFont = painter.font();
    btnFont.setPointSizeF(12);
    btnFont.setBold(true);
    painter.setFont(btnFont);

    struct ButtonStyle {
        QString text;
        QColor fill;
        bool enabled;
    };
    const ButtonStyle buttons[ButtonCount] = {
        { tr("Cancel"),      QColor(80, 80, 80, 225),           true      },
        { tr("Confirm"),     QColor(0, 120, 215, canConfirm ? 235 : 110), canConfirm },
        { tr("Memo (keep)"), QColor(46, 125, 50, canConfirm ? 235 : 110), canConfirm },
        { tr("Memo (cut)"),  QColor(179, 89, 0, canConfirm ? 235 : 110),  canConfirm },
    };

    for (int i = 0; i < ButtonCount; ++i) {
        const QRect rect = actionButtonRect(i);
        painter.setPen(Qt::NoPen);
        painter.setBrush(buttons[i].fill);
        painter.drawRoundedRect(rect, 10, 10);
        painter.setPen(buttons[i].enabled ? Qt::white : QColor(255, 255, 255, 150));
        painter.drawText(rect, Qt::AlignCenter, buttons[i].text);
    }
}

QRect ScreenClipWidget::actionButtonRect(int index) const
{
    // Four buttons share the available width; narrow windows (phones) shrink
    // them rather than letting the row run off the overlay.
    const int gap = 12;
    const int margin = 20;
    const int btnH = 46;
    const int available = qMax(240, width() - margin * 2 - gap * (ButtonCount - 1));
    const int btnW = qBound(64, available / ButtonCount, 150);
    const int totalW = btnW * ButtonCount + gap * (ButtonCount - 1);
    const int left = (width() - totalW) / 2;
    const int top = height() - btnH - 28;
    return QRect(left + index * (btnW + gap), top, btnW, btnH);
}

void ScreenClipWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        // Buttons take priority: a tap on them confirms/cancels instead of
        // starting a new drag, which is what tablet users expect. The memo
        // buttons share the same "needs a real region" rule as Confirm.
        for (int i = 0; i < ButtonCount; ++i) {
            if (!actionButtonRect(i).contains(event->pos())) {
                continue;
            }
            if (i == CancelButton) {
                reject();
                return;
            }
            if (!hasValidSelection()) {
                return;  // Dimmed button: ignore the tap, keep the overlay open
            }
            switch (i) {
            case ConfirmButton:  m_action = Action::InsertOnly; break;
            case MemoKeepButton: m_action = Action::MemoKeep;   break;
            case MemoCutButton:  m_action = Action::MemoCut;     break;
            default: break;
            }
            accept();
            return;
        }
        m_anchor = event->pos();
        m_selection = QRect(m_anchor, m_anchor);
        m_selecting = true;
        update();
    }
    QDialog::mousePressEvent(event);
}

void ScreenClipWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_selecting) {
        m_selection = normalizedRect(m_anchor, event->pos());
        update();
    }
    QDialog::mouseMoveEvent(event);
}

void ScreenClipWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_selecting) {
        m_selecting = false;
        m_selection = normalizedRect(m_anchor, event->pos());
        // A near-zero drag is a stray click, not a region.
        if (m_selection.width() < 2 || m_selection.height() < 2) {
            m_selection = QRect();
        }
        update();
    }
    QDialog::mouseReleaseEvent(event);
}

void ScreenClipWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    Q_UNUSED(event);
    // Guard with a minimum size: a stray double-click after a failed tiny
    // drag leaves a 1x1 residue selection that must not confirm a capture.
    if (m_selection.width() >= 4 && m_selection.height() >= 4) {
        m_action = Action::InsertOnly;
        accept();
    }
}

void ScreenClipWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        reject();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (!m_selection.isEmpty()) {
            m_action = Action::InsertOnly;
            accept();
        }
        return;
    }
    QDialog::keyPressEvent(event);
}

void ScreenClipWidget::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    // A frameless Qt::Tool window must actively take focus, otherwise the
    // key handling (Enter / Esc) can fall through to nothing on some platforms.
    activateWindow();
    raise();
    setFocus();
}

QPixmap ScreenClipWidget::capturedRegion() const
{
    if (m_selection.isEmpty() || m_shot.isNull()) {
        return QPixmap();
    }
    const qreal dpr = m_shot.devicePixelRatio() > 0.0
        ? m_shot.devicePixelRatio() : 1.0;
    const QRectF src(m_selection.x() * dpr,
                     m_selection.y() * dpr,
                     m_selection.width() * dpr,
                     m_selection.height() * dpr);
    QPixmap region = m_shot.copy(src.toAlignedRect());
    region.setDevicePixelRatio(1.0);
    return region;
}
