#pragma once

#include <QByteArray>
#include <QImage>
#include <QSettings>
#include <QString>

// Contact pictures on disk, one account per directory. Each picture is kept
// once, as a round PNG named after the SHA-1 of the original image (the hash
// contacts announce in their presence, XEP-0153), and an index says whose
// picture is whose.
class AvatarStore {
public:
    explicit AvatarStore(const QString &directory);

    // The SHA-1 a contact announces, as lowercase hex, whether QXmpp handed
    // over raw bytes or hex.
    static QString normalizedHash(const QByteArray &hash);

    bool has(const QString &hash) const;
    QString hashFor(const QString &jid) const;
    // The picture file for a contact, or empty when there's none.
    QString pathFor(const QString &jid) const;

    // A picture made ready for our own vCard: its middle square, at most
    // 256 px a side, as JPEG, or PNG when it has see-through parts; empty
    // when there's no picture. The MIME type goes in type.
    static QByteArray photoFrom(const QImage &image, QString *type);

    // Stores an image from a vCard and returns its hash, or an empty string
    // when the data isn't an image.
    QString save(const QByteArray &photo);
    void assign(const QString &jid, const QString &hash);
    void clear(const QString &jid);

private:
    QString fileFor(const QString &hash) const;

    QString m_directory;
    QSettings m_index;
};
