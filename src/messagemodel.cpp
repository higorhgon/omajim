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
            {PreviewRole, "preview"}};
}

void MessageModel::reset(const QList<HistoryEntry> &entries) {
    const bool countChanges = entries.size() != m_entries.size();
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
