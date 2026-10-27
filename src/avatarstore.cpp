#include "avatarstore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSaveFile>

static const int Size = 128;

// QSettings treats "/" in keys as groups and "@" fine, but a JID can hold
// other characters too, so keys are the hex of the JID.
static QString keyFor(const QString &jid) {
    return "avatars/" + QString::fromLatin1(jid.toUtf8().toHex());
}

AvatarStore::AvatarStore(const QString &directory)
    : m_directory(directory), m_index(QDir(directory).filePath("index.ini"), QSettings::IniFormat) {
    QDir().mkpath(directory);
}

QString AvatarStore::normalizedHash(const QByteArray &hash) {
    static const QRegularExpression hex("^[0-9a-fA-F]{40}$");
    const QString text = QString::fromLatin1(hash);
    if (hex.match(text).hasMatch())
        return text.toLower();
    return hash.size() == 20 ? QString::fromLatin1(hash.toHex()) : QString();
}

QString AvatarStore::fileFor(const QString &hash) const {
    return QDir(m_directory).filePath(hash + ".png");
}

bool AvatarStore::has(const QString &hash) const {
    return !hash.isEmpty() && QFileInfo::exists(fileFor(hash));
}

QString AvatarStore::hashFor(const QString &jid) const {
    return m_index.value(keyFor(jid)).toString();
}

QString AvatarStore::pathFor(const QString &jid) const {
    const QString hash = hashFor(jid);
    return has(hash) ? fileFor(hash) : QString();
}

QString AvatarStore::save(const QByteArray &photo) {
    QImage image;
    if (photo.isEmpty() || !image.loadFromData(photo))
        return {};
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(photo, QCryptographicHash::Sha1).toHex());
    if (has(hash))
        return hash;

    // Cropped to the middle square and clipped round once here, so the
    // window draws a plain image.
    const int side = qMin(image.width(), image.height());
    const QImage square = image.copy((image.width() - side) / 2, (image.height() - side) / 2, side, side)
                              .scaled(Size, Size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QImage round(Size, Size, QImage::Format_ARGB32_Premultiplied);
    round.fill(Qt::transparent);
    {
        QPainter painter(&round);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath circle;
        circle.addEllipse(0, 0, Size, Size);
        painter.setClipPath(circle);
        painter.drawImage(0, 0, square);
    }

    QSaveFile file(fileFor(hash));
    if (!file.open(QIODevice::WriteOnly) || !round.save(&file, "PNG") || !file.commit())
        return {};
    return hash;
}

void AvatarStore::assign(const QString &jid, const QString &hash) {
    m_index.setValue(keyFor(jid), hash);
}

void AvatarStore::clear(const QString &jid) {
    m_index.remove(keyFor(jid));
}
