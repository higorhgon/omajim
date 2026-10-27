#include "contactmodel.h"

#include <QCollator>
#include <QUrl>
#include <algorithm>

ContactModel::ContactModel(QObject *parent) : QAbstractListModel(parent) {}

int ContactModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant ContactModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Contact &contact = m_contacts[m_rows.at(index.row())];
    switch (role) {
    case JidRole: return contact.jid;
    case NameRole: return displayName(contact.jid, contact.name);
    case PresenceRole: return contact.presence;
    case StatusTextRole: return contact.statusText;
    case UnreadRole: return contact.unread;
    case AvatarRole: return contact.avatar;
    }
    return {};
}

QHash<int, QByteArray> ContactModel::roleNames() const {
    return {{JidRole, "jid"}, {NameRole, "name"}, {PresenceRole, "presence"},
            {StatusTextRole, "statusText"}, {UnreadRole, "unread"}, {AvatarRole, "avatar"}};
}

QString ContactModel::displayName(const QString &jid, const QString &name) {
    if (!name.trimmed().isEmpty())
        return name.trimmed();
    const qsizetype at = jid.indexOf('@');
    return at > 0 ? jid.left(at) : jid;
}

void ContactModel::setFilter(const QString &filter) {
    if (filter == m_filter)
        return;
    m_filter = filter;
    emit filterChanged();
    rebuild();
}

int ContactModel::totalUnread() const {
    int total = 0;
    for (const Contact &contact : m_contacts)
        total += contact.unread;
    return total;
}

void ContactModel::upsert(const QString &jid, const QString &name) {
    Contact &contact = m_contacts[jid];
    contact.jid = jid;
    contact.name = name;
    rebuild();
    emit contactChanged(jid);
}

void ContactModel::remove(const QString &jid) {
    if (m_contacts.remove(jid)) {
        rebuild();
        emit totalUnreadChanged();
    }
}

void ContactModel::clear() {
    m_contacts.clear();
    rebuild();
    emit totalUnreadChanged();
}

void ContactModel::setPresence(const QString &jid, const QString &presence, const QString &statusText) {
    auto it = m_contacts.find(jid);
    if (it == m_contacts.end())
        return;
    const bool wasOnline = it->presence != "offline";
    it->presence = presence;
    it->statusText = statusText;
    // Going on or offline moves the contact between the two groups; any
    // other change only repaints its row.
    if (wasOnline != (presence != "offline"))
        rebuild();
    else
        refreshRow(jid);
    emit contactChanged(jid);
}

void ContactModel::setAllOffline() {
    for (Contact &contact : m_contacts) {
        contact.presence = "offline";
        contact.statusText.clear();
    }
    rebuild();
}

void ContactModel::setAvatar(const QString &jid, const QString &path) {
    auto it = m_contacts.find(jid);
    const QString url = path.isEmpty() ? QString() : QUrl::fromLocalFile(path).toString();
    if (it == m_contacts.end() || it->avatar == url)
        return;
    it->avatar = url;
    refreshRow(jid);
    emit contactChanged(jid);
}

void ContactModel::addUnread(const QString &jid) {
    auto it = m_contacts.find(jid);
    if (it == m_contacts.end())
        return;
    ++it->unread;
    refreshRow(jid);
    emit totalUnreadChanged();
}

void ContactModel::clearUnread(const QString &jid) {
    auto it = m_contacts.find(jid);
    if (it == m_contacts.end() || it->unread == 0)
        return;
    it->unread = 0;
    refreshRow(jid);
    emit totalUnreadChanged();
}

QString ContactModel::jidAt(int row) const {
    return row >= 0 && row < m_rows.size() ? m_rows.at(row) : QString();
}

int ContactModel::rowOf(const QString &jid) const {
    return m_rows.indexOf(jid);
}

void ContactModel::rebuild() {
    QStringList rows;
    const QString needle = m_filter.trimmed();
    for (const Contact &contact : std::as_const(m_contacts)) {
        if (needle.isEmpty() || contact.jid.contains(needle, Qt::CaseInsensitive) ||
            displayName(contact.jid, contact.name).contains(needle, Qt::CaseInsensitive))
            rows << contact.jid;
    }

    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    std::sort(rows.begin(), rows.end(), [&](const QString &a, const QString &b) {
        const Contact &ca = m_contacts[a], &cb = m_contacts[b];
        const bool onlineA = ca.presence != "offline", onlineB = cb.presence != "offline";
        if (onlineA != onlineB)
            return onlineA;
        const int order = collator.compare(displayName(ca.jid, ca.name), displayName(cb.jid, cb.name));
        return order != 0 ? order < 0 : a < b;
    });

    if (rows == m_rows) {
        for (const QString &jid : rows)
            refreshRow(jid);
        return;
    }
    const bool countChanges = rows.size() != m_rows.size();
    beginResetModel();
    m_rows = rows;
    endResetModel();
    if (countChanges)
        emit countChanged();
}

void ContactModel::refreshRow(const QString &jid) {
    const int row = m_rows.indexOf(jid);
    if (row >= 0)
        emit dataChanged(index(row), index(row));
}
