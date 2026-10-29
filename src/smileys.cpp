#include "smileys.h"

#include "textformat.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>
#include <QXmlStreamReader>
#include <QXmppBitsOfBinaryContentId.h>
#include <QXmppBitsOfBinaryIq.h>
#include <QXmppClient.h>
#include <QXmppTask.h>
#include <QDomElement>
#include <algorithm>

// A picture goes as it is up to this, so an animated GIF or WebP stays one.
// The other side fetches it in a single stanza, base64 and all, and servers
// cap those: Prosody, ours, at 256 KB from a client (c2s_stanza_size_limit).
static const qint64 KeptAsIs = 150 * 1024;
// A still picture over that is shrunk to the size a sticker shows at.
static const int ShrunkSide = 160;

SmileyStore::SmileyStore(const QString &directory, QObject *parent) : QObject(parent), m_directory(directory) {
    QFile file(QDir(directory).filePath("own.json"));
    if (!file.open(QIODevice::ReadOnly))
        return;
    for (const QJsonValue &value : QJsonDocument::fromJson(file.readAll()).array()) {
        const QJsonObject object = value.toObject();
        const Own own{object.value("shortcut").toString(), object.value("cid").toString()};
        if (!own.shortcut.isEmpty() && !pathOf(own.cid).isEmpty())
            m_own << own;
    }
}

SmileyStore *SmileyStore::shared(const QString &directory) {
    static QHash<QString, SmileyStore *> stores;
    SmileyStore *&store = stores[QDir(directory).absolutePath()];
    if (!store)
        store = new SmileyStore(directory, qApp);
    return store;
}

QVariantList SmileyStore::own() const {
    QVariantList list;
    for (const Own &own : m_own)
        list << QVariantMap{{"shortcut", own.shortcut}, {"source", QUrl::fromLocalFile(pathOf(own.cid)).toString()}};
    return list;
}

QString SmileyStore::add(const QString &shortcut, const QString &path) {
    const QString trimmed = shortcut.trimmed();
    if (trimmed.isEmpty())
        return tr("Write the text that becomes the smiley.");
    if (trimmed.contains(QRegularExpression("\\s")))
        return tr("The text can't have spaces.");
    for (const Own &own : m_own)
        if (own.shortcut == trimmed)
            return tr("You already have a smiley for %1.").arg(trimmed);

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return tr("%1 couldn't be read.").arg(QFileInfo(path).fileName());
    QByteArray data = file.readAll();
    QBuffer buffer(&data);
    QImageReader reader(&buffer);
    const QImage image = reader.read();
    if (image.isNull())
        return tr("%1 isn't a picture.").arg(QFileInfo(path).fileName());
    if (data.size() > KeptAsIs) {
        // Qt reads every frame of an animation but writes only one: shrunk,
        // it would stop moving, so it's turned down instead.
        if (reader.supportsAnimation() && reader.imageCount() > 1)
            return tr("This animated smiley is over %1 KB, too big to send; pick a smaller one.").arg(KeptAsIs / 1024);
        QByteArray shrunk;
        QBuffer out(&shrunk);
        out.open(QIODevice::WriteOnly);
        image.scaled(ShrunkSide, ShrunkSide, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(&out, "PNG");
        data = shrunk;
    }

    const QString cid = cidOf(data);
    if (!store(cid, data))
        return tr("The smiley couldn't be saved.");
    m_own << Own{trimmed, cid};
    save();
    emit ownChanged();
    return {};
}

void SmileyStore::remove(const QString &shortcut) {
    const auto removed = std::remove_if(m_own.begin(), m_own.end(), [&](const Own &own) { return own.shortcut == shortcut; });
    if (removed == m_own.end())
        return;
    // The picture stays: messages sent with it still show it.
    m_own.erase(removed, m_own.end());
    save();
    emit ownChanged();
}

// Whether a shortcut at a place of a text stands for a smiley: anywhere,
// as Pidgin does, but not inside a link, where ";)" or ":x:" can be part
// of the address and a picture would break it.
static bool outsideLinks(const QList<std::pair<qsizetype, qsizetype>> &links, qsizetype start, qsizetype end) {
    return std::none_of(links.cbegin(), links.cend(),
                        [&](const auto &link) { return start < link.second && end > link.first; });
}

QHash<QString, QString> SmileyStore::findOwn(const QString &text) const {
    const auto links = TextFormat::links(text);
    QHash<QString, QString> found;
    for (const Own &own : m_own)
        for (qsizetype at = text.indexOf(own.shortcut); at >= 0; at = text.indexOf(own.shortcut, at + 1))
            if (outsideLinks(links, at, at + own.shortcut.size())) {
                found.insert(own.shortcut, own.cid);
                break;
            }
    return found;
}

bool SmileyStore::isOwn(const QString &cid) const {
    return std::any_of(m_own.cbegin(), m_own.cend(), [&](const Own &own) { return own.cid == cid; });
}

QString SmileyStore::fileFor(const QString &cid) const {
    // Named by a hash, so no cid a contact sends can point outside.
    return QDir(m_directory).filePath(
        QString::fromLatin1(QCryptographicHash::hash(cid.toUtf8(), QCryptographicHash::Sha1).toHex()));
}

QString SmileyStore::pathOf(const QString &cid) const {
    if (cid.isEmpty())
        return {};
    const QString path = fileFor(cid);
    return QFile::exists(path) ? path : QString();
}

QByteArray SmileyStore::dataOf(const QString &cid, QString *type) const {
    QFile file(pathOf(cid));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = file.readAll();
    if (type)
        *type = QMimeDatabase().mimeTypeForData(data).name();
    return data;
}

QSize SmileyStore::sizeOf(const QString &cid) const {
    if (m_sizes.contains(cid))
        return m_sizes.value(cid);
    // Not kept until the picture is here.
    const QSize size = QImageReader(pathOf(cid)).size();
    if (size.isValid())
        m_sizes.insert(cid, size);
    return size;
}

bool SmileyStore::store(const QString &cid, const QByteArray &data) {
    if (cid.isEmpty() || data.isEmpty())
        return false;
    // "sha1+<hex>@bob.xmpp.org" says what the picture must hash to; Pidgin
    // checks it too.
    if (cid.startsWith("sha1+") && cid.endsWith("@bob.xmpp.org") && cidOf(data) != cid.toLower())
        return false;
    if (!pathOf(cid).isEmpty())
        return true;
    QDir().mkpath(m_directory);
    QSaveFile file(fileFor(cid));
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        return false;
    ++m_revision;
    emit arrived();
    return true;
}

void SmileyStore::save() const {
    QJsonArray array;
    for (const Own &own : m_own)
        array << QJsonObject{{"shortcut", own.shortcut}, {"cid", own.cid}};
    QDir().mkpath(m_directory);
    QSaveFile file(QDir(m_directory).filePath("own.json"));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(array).toJson());
        file.commit();
    }
}

QString SmileyStore::cidOf(const QByteArray &data) {
    return "sha1+" + QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex()) +
           "@bob.xmpp.org";
}

QString SmileyStore::xhtmlFor(const QString &text, const QHash<QString, QString> &smileys) {
    // The longest first, so ":bigsmile:" isn't taken for ":big".
    QStringList shortcuts = smileys.keys();
    std::sort(shortcuts.begin(), shortcuts.end(), [](const QString &a, const QString &b) { return a.size() > b.size(); });

    const auto links = TextFormat::links(text);
    QString xhtml = QStringLiteral("<p>");
    for (qsizetype i = 0; i < text.size();) {
        bool matched = false;
        for (const QString &shortcut : shortcuts) {
            if (QStringView(text).mid(i).startsWith(shortcut) && outsideLinks(links, i, i + shortcut.size())) {
                xhtml += QStringLiteral("<img alt=\"%1\" src=\"cid:%2\"/>")
                             .arg(shortcut.toHtmlEscaped(), smileys.value(shortcut).toHtmlEscaped());
                i += shortcut.size();
                matched = true;
                break;
            }
        }
        if (matched)
            continue;
        const QChar c = text.at(i++);
        xhtml += c == '\n' ? QStringLiteral("<br/>") : QString(c).toHtmlEscaped();
    }
    return xhtml + QStringLiteral("</p>");
}

QHash<QString, QString> SmileyStore::imagesIn(const QString &xhtml) {
    QHash<QString, QString> images;
    if (!xhtml.contains("cid:"))
        return images;
    QXmlStreamReader reader("<body>" + xhtml + "</body>");
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement || reader.name() != QLatin1String("img"))
            continue;
        const QString src = reader.attributes().value("src").toString();
        const QString alt = reader.attributes().value("alt").toString();
        if (src.startsWith("cid:") && !alt.isEmpty())
            images.insert(alt, src.mid(4));
    }
    return images;
}

QStringList SmileyResponder::discoveryFeatures() const {
    // XHTML-IM QXmpp announces already; twice, Pidgin's check of our
    // capabilities' hash fails (it counts both, QXmpp one) and it takes us
    // for a client without smileys.
    return {QStringLiteral("urn:xmpp:bob")};
}

bool SmileyResponder::handleStanza(const QDomElement &stanza) {
    const QDomElement request = stanza.firstChildElement("data");
    if (stanza.tagName() != "iq" || stanza.attribute("type") != "get" || request.namespaceURI() != "urn:xmpp:bob")
        return false;
    const QString cid = request.attribute("cid");
    QString type;
    // Only our own: what others sent us isn't ours to hand out.
    const QByteArray data = m_store->isOwn(cid) ? m_store->dataOf(cid, &type) : QByteArray();
    if (data.isEmpty()) {
        QXmppIq error(QXmppIq::Error);
        error.setId(stanza.attribute("id"));
        error.setTo(stanza.attribute("from"));
        error.setError(QXmppStanza::Error(QXmppStanza::Error::Cancel, QXmppStanza::Error::ItemNotFound));
        client()->send(std::move(error));
        return true;
    }
    QXmppBitsOfBinaryIq reply;
    reply.setType(QXmppIq::Result);
    reply.setId(stanza.attribute("id"));
    reply.setTo(stanza.attribute("from"));
    reply.setCid(QXmppBitsOfBinaryContentId::fromContentId(cid));
    reply.setContentType(QMimeDatabase().mimeTypeForName(type));
    reply.setData(data);
    // A picture under a hash never changes.
    reply.setMaxAge(86400);
    client()->send(std::move(reply));
    return true;
}
