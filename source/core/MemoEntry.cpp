#include "MemoEntry.h"

#include <QCoreApplication>
#include <QJsonValue>

QString MemoEntry::kindToString(Kind kind)
{
    switch (kind) {
    case Kind::Handwriting:
        return QStringLiteral("handwriting");
    case Kind::Imported:
        return QStringLiteral("imported");
    case Kind::Screenshot:
    default:
        return QStringLiteral("screenshot");
    }
}

MemoEntry::Kind MemoEntry::kindFromString(const QString& text)
{
    if (text == QLatin1String("handwriting"))
        return Kind::Handwriting;
    if (text == QLatin1String("imported"))
        return Kind::Imported;
    return Kind::Screenshot;
}

QJsonObject MemoEntry::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["image"] = imageFile;
    obj["title"] = title;
    obj["created"] = created.toString(Qt::ISODate);
    obj["kind"] = kindToString(kind);
    if (sourcePage >= 0)
        obj["source_page"] = sourcePage;

    // Written only once the card has actually been shown, so a memo that was
    // never opened keeps the file shape of a plain list entry.
    if (hasFloatRect()) {
        obj["float_x"] = floatRect.x();
        obj["float_y"] = floatRect.y();
        obj["float_w"] = floatRect.width();
        obj["float_h"] = floatRect.height();
        if (!qFuzzyIsNull(floatRotation))
            obj["float_rotation"] = floatRotation;
    }
    return obj;
}

MemoEntry MemoEntry::fromJson(const QJsonObject& obj)
{
    MemoEntry entry;
    entry.id = obj["id"].toString();
    entry.imageFile = obj["image"].toString();
    entry.title = obj["title"].toString();
    entry.created = QDateTime::fromString(obj["created"].toString(), Qt::ISODate);
    if (!entry.created.isValid())
        entry.created = QDateTime::currentDateTime();
    entry.kind = kindFromString(obj["kind"].toString());
    entry.sourcePage = obj.contains("source_page")
        ? obj["source_page"].toInt(-1)
        : -1;

    const qreal w = obj["float_w"].toDouble(0.0);
    const qreal h = obj["float_h"].toDouble(0.0);
    if (w > 1.0 && h > 1.0) {
        entry.floatRect = QRectF(obj["float_x"].toDouble(0.0),
                                 obj["float_y"].toDouble(0.0), w, h);
        entry.floatRotation = obj["float_rotation"].toDouble(0.0);
    }

    if (entry.title.isEmpty()) {
        // A missing/blank title should never leave the gallery with a nameless
        // row; the UI is allowed to overwrite it on rename anyway.
        entry.title = memoDefaultTitle(entry.kind, entry.created);
    }
    return entry;
}

QString memoDefaultTitle(MemoEntry::Kind kind, const QDateTime& created)
{
    // Document-level helper, so it has no QObject context of its own; the
    // translations live under the "MemoEntry" context in app_*.ts.
    switch (kind) {
    case MemoEntry::Kind::Handwriting:
        return QCoreApplication::translate(
            "MemoEntry", "Handwriting %1").arg(created.toString("MM-dd HH:mm"));
    case MemoEntry::Kind::Imported:
        return QCoreApplication::translate(
            "MemoEntry", "Image %1").arg(created.toString("MM-dd HH:mm"));
    case MemoEntry::Kind::Screenshot:
    default:
        return QCoreApplication::translate(
            "MemoEntry", "Screenshot %1").arg(created.toString("MM-dd HH:mm"));
    }
}
