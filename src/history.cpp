#include "history.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

// Every column a HistoryEntry carries, in the order read() takes them.
static const QString Columns = QStringLiteral(
    "id, outgoing, kind, body, stamp, file_path, file_size, state, xmpp_id, archive_id, delivered, sender, url,"
    " edited, correction_id, reply_id, smileys, forwarded, xhtml, send_state, peer");

static QString smileysColumn(const QHash<QString, QString> &smileys) {
    if (smileys.isEmpty())
        return {};
    QJsonObject object;
    for (auto it = smileys.cbegin(); it != smileys.cend(); ++it)
        object.insert(it.key(), it.value());
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

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
    entry.replyId = query.value(15).toString();
    const QJsonObject smileys = QJsonDocument::fromJson(query.value(16).toString().toUtf8()).object();
    for (auto it = smileys.constBegin(); it != smileys.constEnd(); ++it)
        entry.smileys.insert(it.key(), it.value().toString());
    entry.forwarded = query.value(17).toInt() != 0;
    entry.xhtml = query.value(18).toString();
    // From before the states, delivered meant received; the rest went out.
    entry.sendState = query.value(19).isNull() ? (entry.delivered ? HistoryEntry::Delivered : HistoryEntry::Sent)
                                               : query.value(19).toInt();
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
                               " correction_id TEXT,"
                               " reply_id TEXT,"
                               " smileys TEXT,"
                               " forwarded INTEGER NOT NULL DEFAULT 0,"
                               " xhtml TEXT,"
                               " send_state INTEGER)") &&
                    query.exec("CREATE INDEX IF NOT EXISTS messages_peer ON messages (peer, stamp)") &&
                    query.exec("CREATE TABLE IF NOT EXISTS roster ("
                               " jid TEXT PRIMARY KEY,"
                               " name TEXT,"
                               " groups TEXT,"
                               " subscription TEXT,"
                               " approved INTEGER NOT NULL DEFAULT 0)") &&
                    query.exec("CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT)") &&
                    query.exec("CREATE TABLE IF NOT EXISTS chats (peer TEXT PRIMARY KEY, open INTEGER NOT NULL)") &&
                    query.exec("CREATE TABLE IF NOT EXISTS aliases (jid TEXT PRIMARY KEY, alias TEXT NOT NULL)") &&
                    query.exec("CREATE TABLE IF NOT EXISTS reactions (peer TEXT NOT NULL, message_id TEXT NOT NULL,"
                               " sender TEXT NOT NULL, emojis TEXT NOT NULL, PRIMARY KEY (peer, message_id, sender))");
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
                                                   {"correction_id", "TEXT"},
                                                   {"reply_id", "TEXT"},
                                                   {"smileys", "TEXT"},
                                                   {"forwarded", "INTEGER NOT NULL DEFAULT 0"},
                                                   {"xhtml", "TEXT"},
                                                   {"send_state", "INTEGER"}};
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
                  " archive_id, delivered, sender, url, reply_id, smileys, forwarded, xhtml, send_state)"
                  " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
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
    query.addBindValue(entry.replyId);
    query.addBindValue(smileysColumn(entry.smileys));
    query.addBindValue(entry.forwarded ? 1 : 0);
    query.addBindValue(entry.xhtml);
    query.addBindValue(entry.outgoing ? entry.sendState : int(HistoryEntry::Sent));
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

bool History::contains(const QString &peer, const QString &xmppId, const QString &archiveId,
                       const QString &body) const {
    if (!isOpen() || (xmppId.isEmpty() && archiveId.isEmpty()))
        return false;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    // Without a text, the id alone (Gajim's import: its ids don't repeat,
    // and a corrected line has the original's id and another text).
    query.prepare(QStringLiteral("SELECT 1 FROM messages WHERE peer = ? AND ((xmpp_id = ? AND xmpp_id <> ''%1)"
                                 " OR (archive_id = ? AND archive_id <> '')) LIMIT 1")
                      .arg(body.isEmpty() ? QString() : QStringLiteral(" AND body = ?")));
    query.addBindValue(peer);
    query.addBindValue(xmppId);
    if (!body.isEmpty())
        query.addBindValue(body);
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
    // The newest with that id: an old id can come back after a restart.
    query.prepare("UPDATE messages SET delivered = 1, send_state = MAX(COALESCE(send_state, 1), 2)"
                  " WHERE id = (SELECT id FROM messages WHERE peer = ? AND xmpp_id = ? AND outgoing = 1"
                  " ORDER BY stamp DESC, id DESC LIMIT 1)");
    query.addBindValue(peer);
    query.addBindValue(xmppId);
    return query.exec() && query.numRowsAffected() > 0;
}

void History::setSendState(qint64 id, int state) {
    if (!isOpen())
        return;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare("UPDATE messages SET send_state = ? WHERE id = ?");
    query.addBindValue(state);
    query.addBindValue(id);
    query.exec();
}

QDateTime History::markReadUpTo(const QString &peer, const QString &xmppId) {
    if (!isOpen() || xmppId.isEmpty())
        return {};
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare("SELECT stamp FROM messages WHERE peer = ? AND outgoing = 1 AND (xmpp_id = ? OR correction_id = ?)"
                  " ORDER BY stamp DESC LIMIT 1");
    query.addBindValue(peer);
    query.addBindValue(xmppId);
    query.addBindValue(xmppId);
    if (!query.exec() || !query.next())
        return {};
    const qint64 stamp = query.value(0).toLongLong();
    query.prepare("UPDATE messages SET send_state = 3, delivered = 1 WHERE peer = ? AND outgoing = 1 AND stamp <= ?"
                  " AND COALESCE(send_state, 1) >= 1");
    query.addBindValue(peer);
    query.addBindValue(stamp);
    query.exec();
    return QDateTime::fromMSecsSinceEpoch(stamp, QTimeZone::UTC);
}

QList<HistoryEntry> History::pending() const {
    QList<HistoryEntry> entries;
    if (!isOpen())
        return entries;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    if (!query.exec(QStringLiteral("SELECT %1 FROM messages WHERE outgoing = 1 AND send_state = 0 ORDER BY stamp, id")
                        .arg(Columns)))
        return entries;
    while (query.next())
        entries << read(query, query.value(20).toString());
    return entries;
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

void History::applyCorrection(qint64 id, const QString &body, const QString &correctionId, const QString &xhtml) {
    if (!isOpen())
        return;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare("UPDATE messages SET body = ?, edited = 1, correction_id = ?, xhtml = ? WHERE id = ?");
    query.addBindValue(body);
    query.addBindValue(correctionId);
    query.addBindValue(xhtml);
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

QHash<QString, QString> History::aliases() const {
    QHash<QString, QString> aliases;
    if (!isOpen())
        return aliases;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    if (query.exec("SELECT jid, alias FROM aliases"))
        while (query.next())
            aliases.insert(query.value(0).toString(), query.value(1).toString());
    return aliases;
}

void History::setAlias(const QString &jid, const QString &alias) {
    if (!isOpen())
        return;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    if (alias.trimmed().isEmpty()) {
        query.prepare("DELETE FROM aliases WHERE jid = ?");
        query.addBindValue(jid);
    } else {
        query.prepare("INSERT OR REPLACE INTO aliases (jid, alias) VALUES (?, ?)");
        query.addBindValue(jid);
        query.addBindValue(alias.trimmed());
    }
    query.exec();
}

HistoryEntry History::findByAnyId(const QString &peer, const QString &id) const {
    if (!isOpen() || id.isEmpty())
        return {};
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("SELECT %1 FROM messages WHERE peer = ? AND (xmpp_id = ? OR archive_id = ?)"
                                 " ORDER BY stamp LIMIT 1").arg(Columns));
    query.addBindValue(peer);
    query.addBindValue(id);
    query.addBindValue(id);
    return query.exec() && query.next() ? read(query, peer) : HistoryEntry{};
}

QList<Reaction> History::reactions(const QString &peer, const QString &messageId) const {
    QList<Reaction> reactions;
    if (!isOpen() || messageId.isEmpty())
        return reactions;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare("SELECT sender, emojis FROM reactions WHERE peer = ? AND message_id = ? ORDER BY rowid");
    query.addBindValue(peer);
    query.addBindValue(messageId);
    if (query.exec())
        while (query.next())
            reactions << Reaction{query.value(0).toString(), query.value(1).toString().split('\n', Qt::SkipEmptyParts)};
    return reactions;
}

void History::setReaction(const QString &peer, const QString &messageId, const QString &sender, const QStringList &emojis) {
    if (!isOpen())
        return;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    if (emojis.isEmpty()) {
        query.prepare("DELETE FROM reactions WHERE peer = ? AND message_id = ? AND sender = ?");
        query.addBindValue(peer);
        query.addBindValue(messageId);
        query.addBindValue(sender);
    } else {
        query.prepare("INSERT OR REPLACE INTO reactions (peer, message_id, sender, emojis) VALUES (?, ?, ?, ?)");
        query.addBindValue(peer);
        query.addBindValue(messageId);
        query.addBindValue(sender);
        query.addBindValue(emojis.join('\n'));
    }
    query.exec();
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

QList<qint64> History::search(const QString &peer, const QString &needle) const {
    QList<qint64> ids;
    const QString wanted = folded(needle.trimmed());
    if (!isOpen() || wanted.isEmpty())
        return ids;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare("SELECT id, body FROM messages WHERE peer = ? AND kind = 'text' ORDER BY stamp, id");
    query.addBindValue(peer);
    if (query.exec())
        while (query.next())
            if (folded(query.value(1).toString()).contains(wanted))
                ids << query.value(0).toLongLong();
    return ids;
}

QList<HistoryEntry> History::since(const QString &peer, const QDateTime &stamp) const {
    QList<HistoryEntry> entries;
    if (!isOpen())
        return entries;
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("SELECT %1 FROM messages WHERE peer = ? AND stamp >= ? ORDER BY stamp, id").arg(Columns));
    query.addBindValue(peer);
    query.addBindValue(stamp.toMSecsSinceEpoch());
    if (query.exec())
        while (query.next())
            entries << read(query, peer);
    return entries;
}

HistoryEntry History::byId(qint64 id) const {
    if (!isOpen())
        return {};
    QSqlQuery query(QSqlDatabase::database(m_connection));
    query.prepare(QStringLiteral("SELECT %1, peer FROM messages WHERE id = ?").arg(Columns));
    query.addBindValue(id);
    if (!query.exec() || !query.next())
        return {};
    // The peer comes after the usual columns.
    return read(query, query.value(20).toString());
}
