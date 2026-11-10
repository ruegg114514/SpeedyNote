#pragma once

// ============================================================================
// MemoEntry - one free-form memo (随记) of a notebook
// ============================================================================
// A memo is a piece of the page kept aside: a screenshot region, a lasso
// selection of handwriting rendered to a transparent PNG, or an imported
// image. Unlike the markdown notes (which hang off a LinkObject slot), memos
// belong to the notebook as a whole: the gallery in the right sidebar lists
// every one of them and each can be opened as a floating card over the page.
//
// Storage:
//   image        bundle/assets/memos/<id>.png   (imageFile, relative to assets/)
//   metadata     bundle/document.json["memos"]  (this struct minus the image)
//
// The float geometry is stored in the same viewport-space pixels the card is
// positioned in, so reopening a notebook puts a card back where the reader
// left it. Geometry is a suggestion, not a contract: the card is clamped into
// the viewport when it is opened again.
// ============================================================================

#include <QDateTime>
#include <QImage>
#include <QJsonObject>
#include <QRectF>
#include <QString>

struct MemoEntry {
    enum class Kind {
        Screenshot,   ///< Captured with the screenshot tool
        Handwriting,  ///< Lasso selection rendered to a transparent PNG
        Imported      ///< Loaded from an image file
    };

    QString id;           ///< UUID; also the image file stem
    QString imageFile;    ///< Path relative to assets/, e.g. "memos/<uuid>.png"
    QString title;        ///< Display title (user-renamable)
    QDateTime created;    ///< Creation time (local)
    Kind kind = Kind::Screenshot;
    int sourcePage = -1;  ///< 0-based notebook page the material came from, -1 = unknown

    /// Last floating-card geometry: the UNROTATED card rect in viewport
    /// logical pixels. Empty until the card has been shown once.
    QRectF floatRect;

    /// Card rotation in degrees, clockwise-positive (same convention as
    /// InsertedObject::rotation).
    qreal floatRotation = 0.0;

    /// Transient only: image bytes waiting for a bundle path on disk (a
    /// document that has never been saved has nowhere to write them yet).
    /// Never serialized; flushed by Document::saveBundle().
    QImage pendingImage;

    bool isValid() const { return !id.isEmpty(); }

    bool hasFloatRect() const
    {
        return floatRect.width() > 1.0 && floatRect.height() > 1.0;
    }

    QJsonObject toJson() const;
    static MemoEntry fromJson(const QJsonObject& obj);

    static QString kindToString(Kind kind);
    static Kind kindFromString(const QString& text);
};

/**
 * @brief Default display title for a memo of @p kind created at @p created.
 *
 * Shared by Document (when a memo is added) and MemoEntry::fromJson() (when an
 * entry on disk carries no title), so both give the same name.
 */
QString memoDefaultTitle(MemoEntry::Kind kind, const QDateTime& created);
