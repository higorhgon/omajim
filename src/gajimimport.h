#pragma once

#include <QString>
#include <QStringList>

class History;

// Brings the conversations of one account over from Gajim's logs.db (the
// Python Omajim keeps the same file). The original is never opened: a copy
// is read, together with its write-ahead log, since Gajim may be running.
namespace GajimImport {

// Where Gajim and the Python Omajim keep their history, if there.
QStringList candidates();

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
