#pragma once

#include <QString>
#include <QList>
#include <QStringList>
#include <optional>

class History;

// Brings the conversations of one account over from Gajim's logs.db (the
// Python Omajim keeps the same file). The original is never opened: a copy
// is read, together with its write-ahead log, since Gajim may be running.
namespace GajimImport {

// Where Gajim and the Python Omajim keep their history, if there.
QStringList candidates();
// Where they keep their settings (settings.sqlite), if there.
QStringList settingsCandidates();

// What Omajim can take over from Gajim's settings: its accounts and the
// preferences Omajim has too. A preference Gajim left at its default is
// left out, so Omajim's own default stays.
struct Account {
    QString jid;
    QString host;  // "host:port", or empty to find the server by DNS
    bool active = true;
};
struct Settings {
    QList<Account> accounts;
    std::optional<int> awayMinutes;  // 0 when automatic away is off
    std::optional<int> xaMinutes;
    std::optional<bool> notifications;
    std::optional<QString> closeAction;  // "tray", "minimize" or "quit"
    QString error;
};
Settings readSettings(const QString &path);

struct Result {
    int found = 0;     // messages of the account in the file
    int imported = 0;  // new here, after skipping those already in the history
    QString error;
};

// Counts the account's messages without importing anything.
Result count(const QString &path, const QString &account);

// Imports the account's messages that the history doesn't have yet;
// running it again adds nothing twice.
Result import(const QString &path, const QString &account, History &history);

}
