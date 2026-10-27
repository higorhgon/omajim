#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QStringList>

struct Contact {
    QString jid;
    QString name;
    // online, chat, away, xa, dnd, or offline.
    QString presence = QStringLiteral("offline");
    QString statusText;
    int unread = 0;
};

// The roster as the contact list shows it: people who are around first,
// then alphabetically, narrowed by the search field.
class ContactModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int totalUnread READ totalUnread NOTIFY totalUnreadChanged)

public:
    enum Role { JidRole = Qt::UserRole + 1, NameRole, PresenceRole, StatusTextRole, UnreadRole };

    explicit ContactModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    int totalUnread() const;

    void upsert(const QString &jid, const QString &name);
    void remove(const QString &jid);
    void clear();
    void setPresence(const QString &jid, const QString &presence, const QString &statusText);
    void setAllOffline();
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
