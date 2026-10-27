#include "history.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

// Every column a HistoryEntry carries, in the order read() takes them.
static const QString Columns = QStringLiteral(
    "id, outgoing, kind, body, stamp, file_path, file_size, state, xmpp_id, archive_id, delivered, sender, url,"
    " edited, correction_id");

static HistoryEntry read(const QSqlQuery &query, const QString &peer) {
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
    entry.delivered = query.value(10).toInt() != 0;
    entry.sender = query.value(11).toString();
    entry.url = query.value(12).toString();
    entry.edited = query.value(13).toInt() != 0;
    entry.correctionId = query.value(14).toString();
    return entry;
}

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
                               " archive_id TEXT,"
                               " delivered INTEGER NOT NULL DEFAULT 0,"
                               " sender TEXT,"
                               " url TEXT,"
                               " edited INTEGER NOT NULL DEFAULT 0,"
                               " correction_id TEXT)") &&
                    query.exec("CREATE INDEX IF NOT EXISTS messages_peer ON messages (peer, stamp)") &&
                    query.exec("CREATE TABLE IF NOT EXISTS roster ("
                               " jid TEXT PRIMARY KEY,"
                               " name TEXT,"
                               " groups TEXT,"
                               " subscription TEXT,"
                               " approved INTEGER NOT NULL DEFAULT 0)") &&
                    query.exec("CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT)") &&
                    query.exec("CREATE TABLE IF NOT EXISTS chats (peer TEXT PRIMARY KEY, open INTEGER NOT NULL)");
    if (!ok) {
        m_error = query.lastError().text();
        db.close();
        return false;
    }

    // Databases from earlier versions lack the newer columns.
    QStringList columns;
    query.exec("PRAGMA table_info(messages)");
    while (query.next())
        columns << query.value(1).toString();
    const QList<std::pair<QString, QString>> added{{"archive_id", "TEXT"},
                                                   {"delivered", "INTEGER NOT NULL DEFAULT 0"},
                                                   {"sender", "TEXT"},
                                                   {"url", "TEXT"},
                                                   {"edited", "INTEGER NOT NULL DEFAULT 0"},
                                                   {"correction_id", "TEXT"}};
    for (const auto &[name, type] : added) {
        if (!columns.contains(name) && !query.exec(QStringLiteral("ALTER TABLE messages ADD COLUMN %1 %2").arg(name, type))) {
            m_error = query.lastError().text();
            db.close();
            return false;
        }
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
                  " archive_id, delivered, sender, url) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
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
    query.addBindValue(entry.delivered ? 1 : 0);
    query.addBindValue(entry.sender);
    query.addBindValue(entry.url);
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
    query.prepare(QStringLiteral("SELECT %1 FROM messages WHERE peer = ? ORDER BY stamp DESC, id DESC LIMIT ?")
                      .arg(Columns));
    query.addBindValue(peer);
    query.addBindValue(limit);
    if (!query.exec())
        return entries;
    while (query.next()) {
        HistoryEntry entry = read(query, peer);
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

bool History::markDelivered(const QString &peer, const QString &xmppId) {
    if (!isOpen() || xmppId.isEmpty())
        return false;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare("UPDATE messages SET delivered = 1 WHERE peer = ? AND xmpp_id = ? AND outgoing = 1");
    query.addBindValue(peer);
    query.addBindValue(xmppId);
    return query.exec() && query.numRowsAffected() > 0;
}

void History::setLink(qint64 id, const QString &url, const QString &xmppId) {
    if (!isOpen())
        return;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare("UPDATE messages SET url = ?, xmpp_id = ? WHERE id = ?");
    query.addBindValue(url);
    query.addBindValue(xmppId);
    query.addBindValue(id);
    query.exec();
}

HistoryEntry History::findCorrectable(const QString &peer, const QString &xmppId) const {
    if (!isOpen() || xmppId.isEmpty())
        return {};
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("SELECT %1 FROM messages WHERE peer = ? AND kind = 'text'"
                                 " AND (xmpp_id = ? OR correction_id = ?) ORDER BY stamp DESC LIMIT 1").arg(Columns));
    query.addBindValue(peer);
    query.addBindValue(xmppId);
    query.addBindValue(xmppId);
    return query.exec() && query.next() ? read(query, peer) : HistoryEntry{};
}

void History::applyCorrection(qint64 id, const QString &body, const QString &correctionId) {
    if (!isOpen())
        return;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare("UPDATE messages SET body = ?, edited = 1, correction_id = ? WHERE id = ?");
    query.addBindValue(body);
    query.addBindValue(correctionId);
    query.addBindValue(id);
    query.exec();
}

HistoryEntry History::lastOwnText(const QString &peer) const {
    if (!isOpen())
        return {};
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("SELECT %1 FROM messages WHERE peer = ? ORDER BY stamp DESC, id DESC LIMIT 1").arg(Columns));
    query.addBindValue(peer);
    if (!query.exec() || !query.next())
        return {};
    // Only the very last line of the conversation, and only if it's ours:
    // correcting something further up would rewrite the past.
    HistoryEntry entry = read(query, peer);
    return entry.outgoing && entry.kind == "text" && !entry.xmppId.isEmpty() ? entry : HistoryEntry{};
}

QDateTime History::lastStamp(const QString &peer) const {
    if (!isOpen())
        return {};
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare("SELECT MAX(stamp) FROM messages WHERE peer = ?");
    query.addBindValue(peer);
    if (!query.exec() || !query.next() || query.value(0).isNull())
        return {};
    return QDateTime::fromMSecsSinceEpoch(query.value(0).toLongLong(), QTimeZone::UTC);
}

void History::failInterruptedTransfers() {
    if (!isOpen())
        return;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.exec("UPDATE messages SET state = 'failed' WHERE kind = 'file' AND state IN ('offered', 'active', 'uploading')");
    // A download from the server can simply start over.
    query.exec("UPDATE messages SET state = 'remote', file_path = '' WHERE kind = 'file' AND state = 'downloading'");
}

QString History::rosterVersion() const {
    if (!isOpen())
        return {};
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare("SELECT value FROM meta WHERE key = 'roster_version'");
    return query.exec() && query.next() ? query.value(0).toString() : QString();
}

QList<RosterRow> History::roster() const {
    QList<RosterRow> rows;
    if (!isOpen())
        return rows;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    if (!query.exec("SELECT jid, name, groups, subscription, approved FROM roster"))
        return rows;
    while (query.next()) {
        const QString groups = query.value(2).toString();
        rows << RosterRow{query.value(0).toString(), query.value(1).toString(),
                          groups.isEmpty() ? QStringList() : groups.split('\n'),
                          query.value(3).toString(), query.value(4).toInt() != 0};
    }
    return rows;
}

// Each change and the version it brings are written together, so a crash
// can't leave a version that claims changes the table doesn't have.
static bool setRosterVersion(QSqlQuery &query, const QString &version) {
    query.prepare("INSERT OR REPLACE INTO meta (key, value) VALUES ('roster_version', ?)");
    query.addBindValue(version);
    return query.exec();
}

static bool writeRosterRow(QSqlQuery &query, const RosterRow &row) {
    query.prepare("INSERT OR REPLACE INTO roster (jid, name, groups, subscription, approved) VALUES (?, ?, ?, ?, ?)");
    query.addBindValue(row.jid);
    query.addBindValue(row.name);
    query.addBindValue(row.groups.join('\n'));
    query.addBindValue(row.subscription);
    query.addBindValue(row.approved ? 1 : 0);
    return query.exec();
}

void History::replaceRoster(const QString &version, const QList<RosterRow> &rows) {
    if (!isOpen())
        return;
    QSqlDatabase db = QSqlDatabase::database(m_connection);
    db.transaction();
    QSqlQuery query(db);
    bool ok = query.exec("DELETE FROM roster");
    for (const RosterRow &row : rows)
        ok = ok && writeRosterRow(query, row);
    ok = ok && setRosterVersion(query, version);
    ok ? db.commit() : db.rollback();
}

void History::upsertRosterRow(const QString &version, const RosterRow &row) {
    if (!isOpen())
        return;
    QSqlDatabase db = QSqlDatabase::database(m_connection);
    db.transaction();
    QSqlQuery query(db);
    (writeRosterRow(query, row) && setRosterVersion(query, version)) ? db.commit() : db.rollback();
}

void History::removeRosterRow(const QString &version, const QString &jid) {
    if (!isOpen())
        return;
    QSqlDatabase db = QSqlDatabase::database(m_connection);
    db.transaction();
    QSqlQuery query(db);
    query.prepare("DELETE FROM roster WHERE jid = ?");
    query.addBindValue(jid);
    (query.exec() && setRosterVersion(query, version)) ? db.commit() : db.rollback();
}

void History::clearRoster() {
    if (!isOpen())
        return;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.exec("DELETE FROM roster");
    query.exec("DELETE FROM meta WHERE key = 'roster_version'");
}

bool History::containsExact(const QString &peer, const QDateTime &stamp, const QString &body) const {
    if (!isOpen())
        return false;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare("SELECT 1 FROM messages WHERE peer = ? AND stamp = ? AND body = ? LIMIT 1");
    query.addBindValue(peer);
    query.addBindValue(stamp.toMSecsSinceEpoch());
    query.addBindValue(body);
    return query.exec() && query.next();
}

void History::beginBatch() {
    if (isOpen())
        QSqlDatabase::database(m_connection).transaction();
}

void History::commitBatch() {
    if (isOpen())
        QSqlDatabase::database(m_connection).commit();
}

QStringList History::openChats() {
    QStringList peers;
    if (!isOpen())
        return peers;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.exec("SELECT value FROM meta WHERE key = 'chats_seeded'");
    if (!query.next()) {
        query.exec("INSERT OR IGNORE INTO chats (peer, open) SELECT DISTINCT peer, 1 FROM messages");
        query.exec("INSERT OR REPLACE INTO meta (key, value) VALUES ('chats_seeded', '1')");
    }
    if (query.exec("SELECT peer FROM chats WHERE open = 1"))
        while (query.next())
            peers << query.value(0).toString();
    return peers;
}

bool History::isKnownChat(const QString &peer) const {
    if (!isOpen())
        return false;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare("SELECT 1 FROM chats WHERE peer = ?");
    query.addBindValue(peer);
    return query.exec() && query.next();
}

void History::setChatOpen(const QString &peer, bool open) {
    if (!isOpen())
        return;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare("INSERT OR REPLACE INTO chats (peer, open) VALUES (?, ?)");
    query.addBindValue(peer);
    query.addBindValue(open ? 1 : 0);
    query.exec();
}

HistoryEntry History::lastLine(const QString &peer) const {
    const auto lines = recent(peer, 1);
    return lines.isEmpty() ? HistoryEntry{} : lines.first();
}

QStringList History::knownChats() const {
    QStringList peers;
    if (!isOpen())
        return peers;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    if (query.exec("SELECT peer FROM chats UNION SELECT DISTINCT peer FROM messages"))
        while (query.next())
            peers << query.value(0).toString();
    return peers;
}
