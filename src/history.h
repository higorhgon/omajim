#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

// One line of a conversation: a text message or a file sent either way.
struct HistoryEntry {
    qint64 id = 0;
    QString peer;
    bool outgoing = false;
    QString kind = QStringLiteral("text");
    QString body;
    QDateTime stamp;
    // Only for kind "file": where it is on disk, its size, and how the
    // transfer went (offered, active, done, failed, refused, canceled).
    QString filePath;
    qint64 fileSize = 0;
    QString state;
    QString xmppId;
    // The id the server's archive gave it (XEP-0359), to recognize the same
    // message when it comes back from the archive.
    QString archiveId;
};

// Conversations kept in SQLite, one database per account.
class History {
public:
    History();
    ~History();
    History(const History &) = delete;
    History &operator=(const History &) = delete;

    bool open(const QString &path);
    bool isOpen() const;
    QString errorString() const { return m_error; }

    qint64 add(HistoryEntry &entry);
    QList<HistoryEntry> recent(const QString &peer, int limit) const;
    bool contains(const QString &peer, const QString &xmppId, const QString &archiveId = {}) const;
    void updateFile(qint64 id, const QString &state, const QString &filePath);

    // A transfer can't continue after the app quits, so anything still
    // offered or running from the last session is marked as failed.
    void failInterruptedTransfers();

private:
    QString m_connection;
    QString m_error;
};
