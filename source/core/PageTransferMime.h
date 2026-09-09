#ifndef PAGETRANSFERMIME_H
#define PAGETRANSFERMIME_H

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <QString>
#include <QStringList>

/**
 * @brief Shared MIME format for cross-document multi-page drag-and-drop (Plan D2).
 *
 * Distinct from the panel's single-index reorder MIME
 * (application/x-speedynote-page-index). Carries a source-document identity
 * token (Document::sessionId()) plus the list of source page UUIDs so the drop
 * target can resolve the live source document and copy the pages.
 */
namespace PageTransfer {

inline const char* mimeType()
{
    return "application/x-speedynote-page-transfer";
}

/// Encode a source token + page UUID list into a MIME payload.
inline QByteArray encode(const QString& srcToken, const QStringList& uuids)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream << srcToken;
    stream << static_cast<qint32>(uuids.size());
    for (const QString& uuid : uuids) {
        stream << uuid;
    }
    return data;
}

/// Decode a MIME payload. Returns false if malformed or empty.
inline bool decode(const QByteArray& data, QString& srcToken, QStringList& uuids)
{
    srcToken.clear();
    uuids.clear();
    if (data.isEmpty()) {
        return false;
    }
    QDataStream stream(data);
    stream >> srcToken;
    qint32 count = 0;
    stream >> count;
    if (count < 0) {
        return false;
    }
    uuids.reserve(count);
    for (qint32 i = 0; i < count; ++i) {
        QString uuid;
        stream >> uuid;
        if (stream.status() != QDataStream::Ok) {
            return false;
        }
        uuids.append(uuid);
    }
    return !srcToken.isEmpty() && !uuids.isEmpty();
}

} // namespace PageTransfer

#endif // PAGETRANSFERMIME_H
