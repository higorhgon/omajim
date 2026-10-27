#include "history.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

History::History() : m_connection("history-" + QUuid::createUuid().toString(QUuid::WithoutBraces)) {}

History::~History() {
    if (QSqlDatabase::contains(m_connection)) {
        QSqlDatabase::database(m_connection, false).close();
        QSqlDatabase::removeDatabase(m_connection);
    }
}

bool History::open(const QString &path) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSqlDatabase db = QSqlDatabase::contains(m_connection) ? QSqlDatabase::database(m_connection, false)
                                                          : QSqlDatabase::addDatabase("QSQLITE", m_connection);
    db.close();
    db.setDatabaseName(path);
    if (!db.open()) {
        m_error = db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    const bool ok = query.exec("PRAGMA journal_mode=WAL") &&
                    query.exec("CREATE TABLE IF NOT EXISTS messages ("
                               " id INTEGER PRIMARY KEY,"
                               " peer TEXT NOT NULL,"
                               " outgoing INTEGER NOT NULL,"
                               " kind TEXT NOT NULL,"
                               " body TEXT NOT NULL,"
                               " stamp INTEGER NOT NULL,"
                               " file_path TEXT,"
                               " file_size INTEGER,"
                               " state TEXT,"
                               " xmpp_id TEXT,"
                               " archive_id TEXT)") &&
                    query.exec("CREATE INDEX IF NOT EXISTS messages_peer ON messages (peer, stamp)");
    if (!ok) {
        m_error = query.lastError().text();
        db.close();
        return false;
    }

    // Databases from before the archive sync lack its column.
    bool hasArchiveId = false;
    query.exec("PRAGMA table_info(messages)");
    while (query.next())
        hasArchiveId = hasArchiveId || query.value(1).toString() == "archive_id";
    if (!hasArchiveId && !query.exec("ALTER TABLE messages ADD COLUMN archive_id TEXT")) {
        m_error = query.lastError().text();
        db.close();
        return false;
    }
    m_error.clear();
    return true;
}

bool History::isOpen() const {
    return QSqlDatabase::contains(m_connection) && QSqlDatabase::database(m_connection, false).isOpen();
}

qint64 History::add(HistoryEntry &entry) {
    if (!entry.stamp.isValid())
        entry.stamp = QDateTime::currentDateTimeUtc();
    if (!isOpen())
        return 0;

    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare("INSERT INTO messages (peer, outgoing, kind, body, stamp, file_path, file_size, state, xmpp_id,"
                  " archive_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    query.addBindValue(entry.peer);
    query.addBindValue(entry.outgoing ? 1 : 0);
    query.addBindValue(entry.kind);
    query.addBindValue(entry.body);
    query.addBindValue(entry.stamp.toMSecsSinceEpoch());
    query.addBindValue(entry.filePath);
    query.addBindValue(entry.fileSize);
    query.addBindValue(entry.state);
    query.addBindValue(entry.xmppId);
    query.addBindValue(entry.archiveId);
    if (!query.exec()) {
        m_error = query.lastError().text();
        return 0;
    }
    entry.id = query.lastInsertId().toLongLong();
    return entry.id;
}

QList<HistoryEntry> History::recent(const QString &peer, int limit) const {
    QList<HistoryEntry> entries;
    if (!isOpen())
        return entries;

    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare("SELECT id, outgoing, kind, body, stamp, file_path, file_size, state, xmpp_id, archive_id"
                  " FROM messages WHERE peer = ? ORDER BY stamp DESC, id DESC LIMIT ?");
    query.addBindValue(peer);
    query.addBindValue(limit);
    if (!query.exec())
        return entries;
    while (query.next()) {
        HistoryEntry entry;
        entry.id = query.value(0).toLongLong();
        entry.peer = peer;
        entry.outgoing = query.value(1).toInt() != 0;
        entry.kind = query.value(2).toString();
        entry.body = query.value(3).toString();
        entry.stamp = QDateTime::fromMSecsSinceEpoch(query.value(4).toLongLong(), QTimeZone::UTC);
        entry.filePath = query.value(5).toString();
        entry.fileSize = query.value(6).toLongLong();
        entry.state = query.value(7).toString();
        entry.xmppId = query.value(8).toString();
        entry.archiveId = query.value(9).toString();
        entries.prepend(entry);
    }
    return entries;
}

bool History::contains(const QString &peer, const QString &xmppId, const QString &archiveId) const {
    if (!isOpen() || (xmppId.isEmpty() && archiveId.isEmpty()))
        return false;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare("SELECT 1 FROM messages WHERE peer = ? AND ((xmpp_id = ? AND xmpp_id <> '')"
                  " OR (archive_id = ? AND archive_id <> '')) LIMIT 1");
    query.addBindValue(peer);
    query.addBindValue(xmppId);
    query.addBindValue(archiveId);
    return query.exec() && query.next();
}

void History::updateFile(qint64 id, const QString &state, const QString &filePath) {
    if (!isOpen())
        return;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare("UPDATE messages SET state = ?, file_path = ? WHERE id = ?");
    query.addBindValue(state);
    query.addBindValue(filePath);
    query.addBindValue(id);
    query.exec();
}

void History::failInterruptedTransfers() {
    if (!isOpen())
        return;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.exec("UPDATE messages SET state = 'failed' WHERE kind = 'file' AND state IN ('offered', 'active')");
}
