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
    case NameRole: return nameOf(contact.jid);
    case AliasRole: return contact.alias;
    case RosterNameRole: return displayName(contact.jid, contact.name);
    case PresenceRole: return contact.presence;
    case StatusTextRole: return contact.statusText;
    case UnreadRole: return contact.unread;
    case AvatarRole: return contact.avatar;
    case IsRoomRole: return contact.isRoom;
    case LastActivityRole: return contact.lastActivity.toLocalTime();
    case LastMessageRole: return contact.lastMessage;
    case LastOutgoingRole: return contact.lastOutgoing;
    }
    return {};
}

QHash<int, QByteArray> ContactModel::roleNames() const {
    return {{JidRole, "jid"}, {NameRole, "name"}, {PresenceRole, "presence"},
            {StatusTextRole, "statusText"}, {UnreadRole, "unread"}, {AvatarRole, "avatar"}, {IsRoomRole, "isRoom"}, {AliasRole, "alias"}, {RosterNameRole, "rosterName"},
            {LastActivityRole, "lastActivity"}, {LastMessageRole, "lastMessage"}, {LastOutgoingRole, "lastOutgoing"}};
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

void ContactModel::upsertRoom(const QString &jid, const QString &name) {
    Contact &room = m_contacts[jid];
    room.jid = jid;
    room.name = name;
    room.isRoom = true;
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

void ContactModel::setActivity(const QString &jid, const QDateTime &stamp, const QString &text, bool outgoing) {
    Contact &contact = m_contacts[jid];
    contact.jid = jid;
    contact.open = true;
    if (!contact.lastActivity.isValid() || stamp >= contact.lastActivity) {
        contact.lastActivity = stamp;
        contact.lastMessage = text;
        contact.lastOutgoing = outgoing;
    }
    rebuild();
    emit contactChanged(jid);
}

void ContactModel::ensure(const QString &jid) {
    if (m_contacts.contains(jid))
        return;
    m_contacts[jid].jid = jid;
    rebuild();
}

void ContactModel::setAlias(const QString &jid, const QString &alias) {
    Contact &contact = m_contacts[jid];
    contact.jid = jid;
    contact.alias = alias.trimmed();
    rebuild();
    emit contactChanged(jid);
}

QString ContactModel::nameOf(const QString &jid) const {
    const Contact contact = m_contacts.value(jid);
    return contact.alias.isEmpty() ? displayName(jid, contact.name) : contact.alias;
}

void ContactModel::setOpen(const QString &jid, bool open) {
    auto it = m_contacts.find(jid);
    if (it == m_contacts.end() || it->open == open)
        return;
    it->open = open;
    rebuild();
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

// Lowercase, without accents, so "joao" finds "João".
static QString folded(const QString &text) {
    QString plain;
    const QString decomposed = text.normalized(QString::NormalizationForm_D);
    for (const QChar c : decomposed)
        if (c.category() != QChar::Mark_NonSpacing)
            plain += c.toLower();
    return plain;
}

int ContactModel::fuzzyScore(const QString &needle, const QString &haystack) {
    const QString n = folded(needle), h = folded(haystack);
    if (n.isEmpty())
        return 0;
    auto startsWord = [&h](qsizetype at) { return at == 0 || !h.at(at - 1).isLetterOrNumber(); };

    // Typed as it appears in the name: the best kind of match, better still
    // at the start of a word.
    const qsizetype at = h.indexOf(n);
    if (at >= 0)
        return 50 + 6 * int(n.size()) + (startsWord(at) ? 20 : 0);

    // Otherwise each typed letter in order, not necessarily together: "jcp"
    // finds "Jean Carlos Pereira". Letters starting a word and letters in a
    // row count more.
    int score = 0, previous = -2;
    qsizetype from = 0;
    for (const QChar c : n) {
        if (c.isSpace())
            continue;
        const qsizetype found = h.indexOf(c, from);
        if (found < 0)
            return -1;
        score += 1 + (startsWord(found) ? 8 : 0) + (found == previous + 1 ? 5 : 0);
        previous = int(found);
        from = found + 1;
    }
    return score;
}

void ContactModel::rebuild() {
    QStringList rows;
    QHash<QString, int> scores;
    const QString needle = m_filter.trimmed();
    for (const Contact &contact : std::as_const(m_contacts)) {
        if (needle.isEmpty()) {
            if (contact.open)
                rows << contact.jid;
            continue;
        }
        const int score = std::max({fuzzyScore(needle, nameOf(contact.jid)),
                                    fuzzyScore(needle, displayName(contact.jid, contact.name)),
                                    fuzzyScore(needle, contact.jid)});
        if (score >= 0) {
            rows << contact.jid;
            scores.insert(contact.jid, score);
        }
    }

    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    if (needle.isEmpty()) {
        // Open conversations: the latest activity first, as in Gajim.
        std::sort(rows.begin(), rows.end(), [&](const QString &a, const QString &b) {
            const Contact &ca = m_contacts[a], &cb = m_contacts[b];
            if (ca.lastActivity != cb.lastActivity)
                return ca.lastActivity > cb.lastActivity;
            return collator.compare(nameOf(ca.jid), nameOf(cb.jid)) < 0;
        });
    } else std::sort(rows.begin(), rows.end(), [&](const QString &a, const QString &b) {
        const Contact &ca = m_contacts[a], &cb = m_contacts[b];
        // The closest match first; among equals, people who are around.
        if (scores.value(a) != scores.value(b))
            return scores.value(a) > scores.value(b);
        const bool onlineA = ca.presence != "offline", onlineB = cb.presence != "offline";
        if (onlineA != onlineB)
            return onlineA;
        const int order = collator.compare(nameOf(ca.jid), nameOf(cb.jid));
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
