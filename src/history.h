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
    // An outgoing message the other side confirmed (XEP-0184).
    bool delivered = false;
    // In a group chat, the nickname of whoever wrote it.
    QString sender;
    // A file shared through the server (XEP-0363/0066): where it can be fetched.
    QString url;
    // Corrected after it was sent (XEP-0308), and the id of the newest
    // correction, which later corrections may point to instead.
    bool edited = false;
    QString correctionId;
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
    bool markDelivered(const QString &peer, const QString &xmppId);
    // The line a correction points to, by its own id or its last
    // correction's; id 0 when there's none.
    HistoryEntry findCorrectable(const QString &peer, const QString &xmppId) const;
    void applyCorrection(qint64 id, const QString &body, const QString &correctionId);
    // Our newest text line in a conversation, to correct; id 0 when none.
    HistoryEntry lastOwnText(const QString &peer) const;
    // An uploaded file gets its link, and the id of the message carrying
    // it, once the upload is done.
    void setLink(qint64 id, const QString &url, const QString &xmppId);
    // When the newest line of a conversation was written, or invalid.
    QDateTime lastStamp(const QString &peer) const;

    // A transfer can't continue after the app quits, so anything still
    // offered or running from the last session is marked as failed, and a
    // download from the server goes back to waiting for a click.
    void failInterruptedTransfers();

private:
    QString m_connection;
    QString m_error;
};
