#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QStringList>

struct Contact {
    QString jid;
    QString name;
    // A name given here only, which wins over the roster's.
    QString alias;
    // online, chat, away, xa, dnd, or offline.
    QString presence = QStringLiteral("offline");
    QString statusText;
    // A round picture on disk, as a file URL for QML, or empty.
    QString avatar;
    int unread = 0;
    // A group chat (XEP-0045) rather than a person.
    bool isRoom = false;
    // In the list of open conversations, and its newest line for the
    // preview under the name.
    bool open = false;
    QDateTime lastActivity;
    QString lastMessage;
    bool lastOutgoing = false;
};

// The sidebar. Without a search it lists the open conversations, newest
// first, like Gajim's chat list; a search goes through every contact and
// room, people who are around first, then alphabetically.
class ContactModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int totalUnread READ totalUnread NOTIFY totalUnreadChanged)

public:
    enum Role { JidRole = Qt::UserRole + 1, NameRole, PresenceRole, StatusTextRole, UnreadRole, AvatarRole, IsRoomRole, AliasRole, RosterNameRole,
                LastActivityRole, LastMessageRole, LastOutgoingRole };

    explicit ContactModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    int totalUnread() const;

    void upsert(const QString &jid, const QString &name);
    void upsertRoom(const QString &jid, const QString &name);
    void remove(const QString &jid);
    void clear();
    void setPresence(const QString &jid, const QString &presence, const QString &statusText);
    void setAllOffline();
    void setAvatar(const QString &jid, const QString &path);
    // Opens the conversation and moves it up if the line is its newest.
    void setActivity(const QString &jid, const QDateTime &stamp, const QString &text, bool outgoing);
    void setOpen(const QString &jid, bool open);
    // Known for searching, without touching a name already there.
    void ensure(const QString &jid);
    void setAlias(const QString &jid, const QString &alias);
    // How a contact is called everywhere: the alias, the roster name, or
    // the address's user part.
    QString nameOf(const QString &jid) const;
    bool isOpen(const QString &jid) const { return m_contacts.value(jid).open; }
    void addUnread(const QString &jid);
    void clearUnread(const QString &jid);

    bool contains(const QString &jid) const { return m_contacts.contains(jid); }
    Contact contact(const QString &jid) const { return m_contacts.value(jid); }

    Q_INVOKABLE QString jidAt(int row) const;
    Q_INVOKABLE int rowOf(const QString &jid) const;

    static QString displayName(const QString &jid, const QString &name);

signals:
    void filterChanged();
    void countChanged();
    void totalUnreadChanged();
    void contactChanged(const QString &jid);

private:
    void rebuild();
    void refreshRow(const QString &jid);

    QHash<QString, Contact> m_contacts;
    QStringList m_rows;
    QString m_filter;
};
