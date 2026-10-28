#include "gajimimport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QUuid>

#include "files.h"
#include "history.h"

namespace GajimImport {

// Gajim's own codes (gajim/common/storage/archive/const.py).
enum { Chat = 1, GroupChat = 2, PrivateMessage = 3 };
enum { Outgoing = 2 };

QStringList candidates() {
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QStringList found;
    for (const QString &name : {QStringLiteral("gajim"), QStringLiteral("omajim")}) {
        const QString path = QDir(data).filePath(name + "/logs.db");
        if (QFileInfo::exists(path))
            found << path;
    }
    return found;
}

QStringList settingsCandidates() {
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    QStringList found;
    for (const QString &name : {QStringLiteral("gajim"), QStringLiteral("omajim")}) {
        const QString path = QDir(config).filePath(name + "/settings.sqlite");
        if (QFileInfo::exists(path))
            found << path;
    }
    return found;
}

// A private copy of the file and its write-ahead log, opened read-only, so
// a running Gajim and its data are never touched.
class Copy {
public:
    explicit Copy(const QString &path) : m_connection("gajim-" + QUuid::createUuid().toString(QUuid::WithoutBraces)) {
        if (!m_dir.isValid()) {
            m_error = QObject::tr("No temporary folder to read %1 from.").arg(path);
            return;
        }
        const QString copy = m_dir.filePath(QFileInfo(path).fileName());
        for (const QString &suffix : {QString(), QStringLiteral("-wal"), QStringLiteral("-shm")}) {
            if (QFileInfo::exists(path + suffix) && !QFile::copy(path + suffix, copy + suffix)) {
                m_error = QObject::tr("%1 couldn't be copied for reading.").arg(path + suffix);
                return;
            }
        }
        m_db = QSqlDatabase::addDatabase("QSQLITE", m_connection);
        m_db.setDatabaseName(copy);
        if (!m_db.open())
            m_error = m_db.lastError().text();
    }
    ~Copy() {
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connection);
    }

    QString error() const { return m_error; }
    QSqlDatabase &db() { return m_db; }

private:
    QTemporaryDir m_dir;
    QString m_connection;
    QSqlDatabase m_db;
    QString m_error;
};

static const char *Select =
    "SELECT m.pk, m.type, m.direction, m.timestamp, m.id, m.stanza_id, m.text, m.resource, m.correction_id,"
    " r.jid,"
    " (SELECT o.url FROM oob o WHERE o.fk_message_pk = m.pk LIMIT 1),"
    " (SELECT f.name FROM filetransfer f WHERE f.fk_message_pk = m.pk LIMIT 1),"
    " (SELECT f.size FROM filetransfer f WHERE f.fk_message_pk = m.pk LIMIT 1)"
    " FROM message m JOIN remote r ON r.pk = m.fk_remote_pk JOIN account a ON a.pk = m.fk_account_pk"
    " WHERE lower(a.jid) = lower(?) ORDER BY m.timestamp, m.pk";

Result count(const QString &path, const QString &account) {
    Result result;
    Copy copy(path);
    if (!copy.error().isEmpty()) {
        result.error = copy.error();
        return result;
    }
    QSqlQuery query(copy.db());
    query.prepare("SELECT COUNT(*) FROM message m JOIN account a ON a.pk = m.fk_account_pk"
                  " WHERE lower(a.jid) = lower(?) AND m.correction_id IS NULL");
    query.addBindValue(account);
    if (!query.exec() || !query.next()) {
        result.error = QObject::tr("%1 isn't a Gajim history this version can read: %2").arg(path, query.lastError().text());
        return result;
    }
    result.found = query.value(0).toInt();
    return result;
}

Result import(const QString &path, const QString &account, History &history) {
    Result result;
    Copy copy(path);
    if (!copy.error().isEmpty()) {
        result.error = copy.error();
        return result;
    }
    QSqlQuery query(copy.db());
    query.prepare(Select);
    query.addBindValue(account);
    if (!query.exec()) {
        result.error = QObject::tr("%1 isn't a Gajim history this version can read: %2").arg(path, query.lastError().text());
        return result;
    }

    struct Correction {
        QString peer, replaces, body, id;
        bool outgoing;
        QString sender;
    };
    QList<Correction> corrections;

    history.beginBatch();
    while (query.next()) {
        const int type = query.value(1).toInt();
        const QString text = query.value(6).toString();
        const QString nick = query.value(7).toString();
        HistoryEntry entry;
        entry.peer = query.value(9).toString().toLower();
        entry.outgoing = query.value(2).toInt() == Outgoing;
        entry.stamp = QDateTime::fromMSecsSinceEpoch(qint64(query.value(3).toDouble() * 1000), QTimeZone::UTC);
        entry.xmppId = query.value(4).toString();
        entry.archiveId = query.value(5).toString();
        entry.body = text;
        if (type == GroupChat)
            entry.sender = nick;
        else if (type == PrivateMessage)
            entry.sender = entry.outgoing ? nick : QObject::tr("%1 (private)").arg(nick);

        // Corrections go on top of their line once everything is in.
        const QString replaces = query.value(8).toString();
        if (!replaces.isEmpty()) {
            corrections << Correction{entry.peer, replaces, text, entry.xmppId, entry.outgoing, entry.sender};
            continue;
        }
        ++result.found;

        const QString url = query.value(10).toString();
        const QString fileName = query.value(11).toString();
        if (!url.isEmpty() && text.trimmed() == url) {
            entry.kind = "file";
            entry.url = url;
            entry.body = Files::safeFileName(QUrl(url).fileName(QUrl::FullyDecoded));
            entry.state = entry.outgoing ? "done" : "remote";
        } else if (!fileName.isEmpty()) {
            // A direct transfer: where the file went isn't known here.
            entry.kind = "file";
            entry.body = Files::safeFileName(fileName);
            entry.fileSize = query.value(12).toLongLong();
            entry.state = "done";
        } else if (text.isEmpty()) {
            continue;
        }

        const bool known = !entry.xmppId.isEmpty() || !entry.archiveId.isEmpty()
                               ? history.contains(entry.peer, entry.xmppId, entry.archiveId)
                               : history.containsExact(entry.peer, entry.stamp, entry.body);
        if (known)
            continue;
        if (history.add(entry) > 0)
            ++result.imported;
    }
    for (const Correction &correction : std::as_const(corrections)) {
        const HistoryEntry original = history.findCorrectable(correction.peer, correction.replaces);
        if (original.id != 0 && original.outgoing == correction.outgoing)
            history.applyCorrection(original.id, correction.body, correction.id);
    }
    history.commitBatch();
    return result;
}

// Gajim keeps each group of settings as a JSON object, only with what
// differs from its defaults (gajim/common/settings.py).
Settings readSettings(const QString &path) {
    Settings settings;
    Copy copy(path);
    if (!copy.error().isEmpty()) {
        settings.error = copy.error();
        return settings;
    }
    QSqlQuery query(copy.db());
    if (query.exec("SELECT settings FROM settings WHERE name = 'app'") && query.next()) {
        const QJsonObject app = QJsonDocument::fromJson(query.value(0).toString().toUtf8()).object();
        // Gajim's defaults (gajim/common/setting_values.py): away after 5
        // minutes, not available after 15, both on.
        if (app.contains("autoawaytime") || app.contains("autoaway"))
            settings.awayMinutes = app.value("autoaway").toBool(true) ? app.value("autoawaytime").toInt(5) : 0;
        if (app.contains("autoxatime") || app.contains("autoxa"))
            settings.xaMinutes = app.value("autoxa").toBool(true) ? app.value("autoxatime").toInt(15) : 0;
        if (app.contains("show_notifications"))
            settings.notifications = app.value("show_notifications").toBool();
        // Gajim's "hide" is to the tray.
        const QString close = app.value("action_on_close").toString();
        if (close == "hide")
            settings.closeAction = QStringLiteral("tray");
        else if (close == "minimize" || close == "quit")
            settings.closeAction = close;
    }
    if (query.exec("SELECT settings FROM account_settings ORDER BY rowid")) {
        while (query.next()) {
            const QJsonObject account = QJsonDocument::fromJson(query.value(0).toString().toUtf8())
                                            .object().value("account").toObject();
            const QString jid = account.value("address").toString().trimmed().toLower();
            if (!jid.contains('@'))
                continue;
            Account found{jid, {}, account.value("active").toBool(true)};
            if (account.value("use_custom_host").toBool() && !account.value("custom_host").toString().isEmpty())
                found.host = QStringLiteral("%1:%2").arg(account.value("custom_host").toString())
                                 .arg(account.value("custom_port").toInt(5222));
            settings.accounts << found;
        }
    }
    return settings;
}

}
