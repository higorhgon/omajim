#include "messagemodel.h"

#include "files.h"

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
    case FileSizeRole: return Files::formatSize(entry.fileSize);
    case StateRole: return entry.state;
    case ProgressRole: return m_progress.value(entry.id, entry.state == "done" ? 1.0 : 0.0);
    case DeliveredRole: return entry.delivered;
    }
    return {};
}

QHash<int, QByteArray> MessageModel::roleNames() const {
    return {{IdRole, "messageId"}, {BodyRole, "body"}, {OutgoingRole, "outgoing"},
            {StampRole, "stamp"}, {KindRole, "kind"}, {FilePathRole, "filePath"},
            {FileSizeRole, "fileSize"}, {StateRole, "transferState"}, {ProgressRole, "progress"},
            {DeliveredRole, "delivered"}};
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

int MessageModel::rowOf(qint64 id) const {
    // Transfers are nearly always among the newest lines.
    for (int row = m_entries.size() - 1; row >= 0; --row)
        if (m_entries.at(row).id == id)
            return row;
    return -1;
}
