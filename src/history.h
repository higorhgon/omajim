#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

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
    // How far one of ours got, as ZapFast shows it: waiting to go out
    // (Pending, kept to send once connected), taken by the server (Sent,
    // XEP-0198), on the other side (Delivered, XEP-0184), read there (Read,
    // XEP-0333).
    enum SendState { Pending = 0, Sent = 1, Delivered = 2, Read = 3 };
    int sendState = Sent;
    // In a group chat, the nickname of whoever wrote it.
    QString sender;
    // A file shared through the server (XEP-0363/0066): where it can be fetched.
    QString url;
    // Corrected after it was sent (XEP-0308), and the id of the newest
    // correction, which later corrections may point to instead.
    bool edited = false;
    QString correctionId;
    // The message this one answers (XEP-0461), by its id.
    QString replyId;
    // Forwarded with Omajim, whose "[when] who:" header starts the text.
    bool forwarded = false;
    // The formatted text (XHTML-IM, as Pidgin sends colors and the like),
    // as it came; shown, filtered, instead of the body.
    QString xhtml;
    // Custom smileys in the text, Pidgin's way: the text each one stands
    // for, to its picture's cid (XEP-0231).
    QHash<QString, QString> smileys;
};

// One person's reactions to a message (XEP-0444): they always come as the
// whole set, which replaces the one before.
struct Reaction {
    QString sender;
    QStringList emojis;
};

// A contact as the server's roster lists it, kept so the list shows at once
// on the next start and the server only sends what changed since (RFC 6121
// roster versioning).
struct RosterRow {
    QString jid;
    QString name;
    QStringList groups;
    QString subscription;
    bool approved = false;
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
    // The same text at the same moment, for messages that came without ids.
    bool containsExact(const QString &peer, const QDateTime &stamp, const QString &body) const;
    // Many writes as one transaction, for imports.
    void beginBatch();
    void commitBatch();
    void updateFile(qint64 id, const QString &state, const QString &filePath);
    bool markDelivered(const QString &peer, const QString &xmppId);
    void setSendState(qint64 id, int state);
    // The other side read one of ours: it and every one before it are read.
    // The stamp of that line, or invalid when it isn't here.
    QDateTime markReadUpTo(const QString &peer, const QString &xmppId);
    // Ours still waiting to go out, oldest first.
    QList<HistoryEntry> pending() const;
    // The line a correction points to, by its own id or its last
    // correction's; id 0 when there's none.
    HistoryEntry findCorrectable(const QString &peer, const QString &xmppId) const;
    void applyCorrection(qint64 id, const QString &body, const QString &correctionId, const QString &xhtml = {});
    // Our newest text line in a conversation, to correct; id 0 when none.
    HistoryEntry lastOwnText(const QString &peer) const;
    // An uploaded file gets its link, and the id of the message carrying
    // it, once the upload is done.
    void setLink(qint64 id, const QString &url, const QString &xmppId);
    // When the newest line of a conversation was written, or invalid.
    QDateTime lastStamp(const QString &peer) const;

    // Conversations shown in the sidebar, kept open between runs like
    // Gajim's chat list. A first run opens every conversation with history.
    QStringList openChats();
    // Whether the conversation was ever opened or closed, so a room joined
    // again stays closed if the user closed it.
    bool isKnownChat(const QString &peer) const;
    // Everyone with a conversation here, open or closed, for searching.
    QStringList knownChats() const;
    void setChatOpen(const QString &peer, bool open);
    // The newest line of a conversation; id 0 when there's none.
    HistoryEntry lastLine(const QString &peer) const;
    // The text lines of a conversation that contain a search, oldest first,
    // ignoring case and accents.
    QList<qint64> search(const QString &peer, const QString &needle) const;
    // Every line from a moment on, oldest first, to show an older match.
    QList<HistoryEntry> since(const QString &peer, const QDateTime &stamp) const;
    HistoryEntry byId(qint64 id) const;
    // A line by its own id or the server's (rooms answer and react to the
    // latter); id 0 when it isn't here.
    HistoryEntry findByAnyId(const QString &peer, const QString &id) const;
    QList<Reaction> reactions(const QString &peer, const QString &messageId) const;
    void setReaction(const QString &peer, const QString &messageId, const QString &sender, const QStringList &emojis);

    // Names the user gave contacts here, never sent to the server.
    QHash<QString, QString> aliases() const;
    void setAlias(const QString &jid, const QString &alias);

    QString rosterVersion() const;
    QList<RosterRow> roster() const;
    void replaceRoster(const QString &version, const QList<RosterRow> &rows);
    void upsertRosterRow(const QString &version, const RosterRow &row);
    void removeRosterRow(const QString &version, const QString &jid);
    void clearRoster();

    // A transfer can't continue after the app quits, so anything still
    // offered or running from the last session is marked as failed, and a
    // download from the server goes back to waiting for a click.
    void failInterruptedTransfers();

private:
    QString m_connection;
    QString m_error;
};
