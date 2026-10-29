#pragma once

#include <QHash>
#include <QObject>
#include <QSize>
#include <QVariantList>
#include <QXmppClientExtension.h>

// Custom smileys the way Pidgin sends them over XMPP: the text keeps the
// shortcut, and the message's XHTML-IM swaps it for an <img> whose src is a
// cid: of Bits of Binary (XEP-0231), which the other side asks the sender
// for. The images, ours and the ones received, are kept by their cid.
class SmileyStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList own READ own NOTIFY ownChanged)
    Q_PROPERTY(int revision READ revision NOTIFY arrived)

public:
    explicit SmileyStore(const QString &directory, QObject *parent = nullptr);
    // The one store of a directory, shared by every account, so a smiley
    // added in one goes out from all.
    static SmileyStore *shared(const QString &directory);

    // Ours, in the order added: {shortcut, source} with source a file URL.
    QVariantList own() const;
    int revision() const { return m_revision; }

    // Adds a picture of ours under a shortcut; the error, or empty.
    Q_INVOKABLE QString add(const QString &shortcut, const QString &path);
    Q_INVOKABLE void remove(const QString &shortcut);

    // Our shortcuts in a text, to their cid.
    QHash<QString, QString> findOwn(const QString &text) const;
    Q_INVOKABLE bool isOwn(const QString &cid) const;
    // The picture of a cid, if it's here.
    Q_INVOKABLE QString pathOf(const QString &cid) const;
    QByteArray dataOf(const QString &cid, QString *type) const;
    QSize sizeOf(const QString &cid) const;
    // Keeps a picture that arrived; false when its cid's hash doesn't match.
    bool store(const QString &cid, const QByteArray &data);

    // The cid Pidgin gives a picture: its SHA-1, at bob.xmpp.org.
    static QString cidOf(const QByteArray &data);
    // The XHTML-IM body of a text with the shortcuts as pictures.
    static QString xhtmlFor(const QString &text, const QHash<QString, QString> &smileys);
    // The pictures in a received XHTML-IM body, by the text they stand for.
    static QHash<QString, QString> imagesIn(const QString &xhtml);

signals:
    void ownChanged();
    void arrived();

private:
    struct Own {
        QString shortcut;
        QString cid;
    };
    QString fileFor(const QString &cid) const;
    void save() const;

    QString m_directory;
    QList<Own> m_own;
    int m_revision = 0;
    mutable QHash<QString, QSize> m_sizes;
};

// Hands our smileys to whoever got a message with them and asks for the
// picture (XEP-0231), and tells Pidgin we take its custom smileys: it only
// sends them to clients with Bits of Binary and XHTML-IM (the latter QXmpp
// announces itself).
class SmileyResponder : public QXmppClientExtension {
public:
    explicit SmileyResponder(SmileyStore *store) : m_store(store) {}
    QStringList discoveryFeatures() const override;
    bool handleStanza(const QDomElement &stanza) override;

private:
    SmileyStore *m_store;
};
