#include "messagemodel.h"

#include "files.h"

#include <QUrl>

MessageModel::MessageModel(QObject *parent) : QAbstractListModel(parent) {}

int MessageModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant MessageModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const HistoryEntry &entry = m_entries.at(index.row());
    switch (role) {
    case IdRole: return entry.id;
    case BodyRole: return entry.body;
    case OutgoingRole: return entry.outgoing;
    case StampRole: return entry.stamp.toLocalTime();
    case KindRole: return entry.kind;
    case FilePathRole: return entry.filePath;
    case FileSizeRole: return entry.fileSize > 0 ? Files::formatSize(entry.fileSize) : QString();
    case StateRole: return entry.state;
    case ProgressRole: return m_progress.value(entry.id, entry.state == "done" ? 1.0 : 0.0);
    case DeliveredRole: return entry.delivered;
    case SenderRole: return entry.sender;
    case UrlRole: return entry.url;
    case EditedRole: return entry.edited;
    case SelectedRole: return m_selected.contains(entry.id);
    case ReplyRole: return m_replies.value(entry.id);
    case ReactionsRole: return m_reactions.value(entry.id);
    case RouteRole: return m_routes.value(entry.id);
    case PreviewRole: return m_previews.contains(entry.id) ? QUrl::fromLocalFile(m_previews.value(entry.id)).toString() : QString();
    }
    return {};
}

QHash<int, QByteArray> MessageModel::roleNames() const {
    return {{IdRole, "messageId"}, {BodyRole, "body"}, {OutgoingRole, "outgoing"},
            {StampRole, "stamp"}, {KindRole, "kind"}, {FilePathRole, "filePath"},
            {FileSizeRole, "fileSize"}, {StateRole, "transferState"}, {ProgressRole, "progress"},
            {DeliveredRole, "delivered"}, {SenderRole, "sender"},
            {UrlRole, "url"}, {EditedRole, "edited"},
            {PreviewRole, "preview"}, {RouteRole, "route"}, {SelectedRole, "selected"},
            {ReplyRole, "reply"}, {ReactionsRole, "reactions"}};
}

void MessageModel::reset(const QList<HistoryEntry> &entries) {
    const bool countChanges = entries.size() != m_entries.size();
    // Another conversation: nothing of it is selected.
    if (!m_selected.isEmpty()) {
        m_selected.clear();
        emit selectionChanged();
    }
    beginResetModel();
    m_entries = entries;
    endResetModel();
    if (countChanges)
        emit countChanged();
}

void MessageModel::append(const HistoryEntry &entry) {
    beginInsertRows({}, m_entries.size(), m_entries.size());
    m_entries.append(entry);
    endInsertRows();
    emit countChanged();
}

void MessageModel::updateFile(qint64 id, const QString &state, const QString &filePath) {
    const int row = rowOf(id);
    if (row < 0)
        return;
    m_entries[row].state = state;
    m_entries[row].filePath = filePath;
    emit dataChanged(index(row), index(row), {StateRole, FilePathRole, ProgressRole});
}

void MessageModel::setProgress(qint64 id, double progress) {
    m_progress.insert(id, progress);
    const int row = rowOf(id);
    if (row >= 0)
        emit dataChanged(index(row), index(row), {ProgressRole});
}

void MessageModel::markDelivered(const QString &xmppId) {
    for (int row = m_entries.size() - 1; row >= 0; --row) {
        if (m_entries.at(row).outgoing && m_entries.at(row).xmppId == xmppId) {
            m_entries[row].delivered = true;
            emit dataChanged(index(row), index(row), {DeliveredRole});
            return;
        }
    }
}

void MessageModel::setLink(qint64 id, const QString &url, const QString &xmppId) {
    const int row = rowOf(id);
    if (row < 0)
        return;
    m_entries[row].url = url;
    m_entries[row].xmppId = xmppId;
    emit dataChanged(index(row), index(row), {UrlRole});
}

void MessageModel::applyCorrection(qint64 id, const QString &body) {
    const int row = rowOf(id);
    if (row < 0)
        return;
    m_entries[row].body = body;
    m_entries[row].edited = true;
    emit dataChanged(index(row), index(row), {BodyRole, EditedRole});
}

void MessageModel::setPreview(qint64 id, const QString &path) {
    m_previews.insert(id, path);
    const int row = rowOf(id);
    if (row >= 0)
        emit dataChanged(index(row), index(row), {PreviewRole});
}

void MessageModel::setRoute(qint64 id, const QString &route) {
    m_routes.insert(id, route);
    const int row = rowOf(id);
    if (row >= 0)
        emit dataChanged(index(row), index(row), {RouteRole});
}

QString MessageModel::url(qint64 id) const {
    const int row = rowOf(id);
    return row < 0 ? QString() : m_entries.at(row).url;
}

QString MessageModel::filePath(qint64 id) const {
    const int row = rowOf(id);
    return row < 0 ? QString() : m_entries.at(row).filePath;
}

int MessageModel::rowOf(qint64 id) const {
    // Transfers are nearly always among the newest lines.
    for (int row = m_entries.size() - 1; row >= 0; --row)
        if (m_entries.at(row).id == id)
            return row;
    return -1;
}

void MessageModel::selectRange(qint64 from, qint64 to) {
    int first = rowOf(from), last = rowOf(to);
    if (first < 0 || last < 0)
        return;
    if (first > last)
        std::swap(first, last);
    for (int row = first; row <= last; ++row)
        m_selected.insert(m_entries.at(row).id);
    emit dataChanged(index(first), index(last), {SelectedRole});
    emit selectionChanged();
}

void MessageModel::toggleSelected(qint64 id) {
    const int row = rowOf(id);
    if (row < 0)
        return;
    if (!m_selected.remove(id))
        m_selected.insert(id);
    emit dataChanged(index(row), index(row), {SelectedRole});
    emit selectionChanged();
}

void MessageModel::clearSelection() {
    if (m_selected.isEmpty())
        return;
    m_selected.clear();
    emit dataChanged(index(0), index(m_entries.size() - 1), {SelectedRole});
    emit selectionChanged();
}

QList<HistoryEntry> MessageModel::selectedEntries() const {
    QList<HistoryEntry> entries;
    for (const HistoryEntry &entry : m_entries)
        if (m_selected.contains(entry.id))
            entries << entry;
    return entries;
}

void MessageModel::setReply(qint64 id, const QVariantMap &reply) {
    m_replies.insert(id, reply);
    const int row = rowOf(id);
    if (row >= 0)
        emit dataChanged(index(row), index(row), {ReplyRole});
}

void MessageModel::setReactions(qint64 id, const QVariantList &reactions) {
    m_reactions.insert(id, reactions);
    const int row = rowOf(id);
    if (row >= 0)
        emit dataChanged(index(row), index(row), {ReactionsRole});
}

HistoryEntry MessageModel::entry(qint64 id) const {
    const int row = rowOf(id);
    return row < 0 ? HistoryEntry{} : m_entries.at(row);
}

int MessageModel::rowOfMessage(const QString &messageId) const {
    if (messageId.isEmpty())
        return -1;
    for (int row = m_entries.size() - 1; row >= 0; --row)
        if (m_entries.at(row).xmppId == messageId || m_entries.at(row).archiveId == messageId)
            return row;
    return -1;
}
