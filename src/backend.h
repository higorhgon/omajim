#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QSettings>
#include <QSslCertificate>
#include <QSslError>
#include <QUrl>
#include <QTimer>
#include <QStringList>
#include <QVariantList>
#include <memory>

#include "avatarstore.h"
#include "contactmodel.h"
#include "history.h"
#include "messagemodel.h"

class FilePicker;
class Notifier;
class QFileInfo;
class QNetworkAccessManager;
class QNetworkReply;
class QXmppClient;
class QXmppHttpUpload;
class QXmppHttpUploadManager;
class QXmppMamManager;
class QXmppMucManagerV2;
class QXmppPepBookmarkManager;
class QXmppMessage;
class QXmppPresence;
class QXmppRosterManager;
class QXmppSocksServer;
class QXmppTransferJob;
class QXmppTransferManager;
class QXmppVCardManager;
class SecretStore;

// One XMPP account: its connection, roster, conversations, and file
// transfers, handed to QML as `backend`.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool starting READ starting NOTIFY signedInChanged)
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY signedInChanged)
    Q_PROPERTY(QString jid READ jid NOTIFY signedInChanged)
    Q_PROPERTY(QString host READ host NOTIFY signedInChanged)
    Q_PROPERTY(QString connectionState READ connectionState NOTIFY connectionStateChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)
    Q_PROPERTY(QString certificateFingerprint READ certificateFingerprint NOTIFY certificateChanged)
    Q_PROPERTY(QStringList subscriptionRequests READ subscriptionRequests NOTIFY subscriptionRequestsChanged)
    Q_PROPERTY(QString currentChat READ currentChat NOTIFY currentChatChanged)
    Q_PROPERTY(QString currentChatName READ currentChatName NOTIFY currentChatChanged)
    Q_PROPERTY(QString currentChatPresence READ currentChatPresence NOTIFY currentChatChanged)
    Q_PROPERTY(QString currentChatStatusText READ currentChatStatusText NOTIFY currentChatChanged)
    Q_PROPERTY(QString currentChatAvatar READ currentChatAvatar NOTIFY currentChatChanged)
    Q_PROPERTY(bool currentChatTyping READ currentChatTyping NOTIFY typingChanged)
    Q_PROPERTY(bool currentChatIsRoom READ currentChatIsRoom NOTIFY currentChatChanged)
    Q_PROPERTY(QStringList currentRoomParticipants READ currentRoomParticipants NOTIFY participantsChanged)
    Q_PROPERTY(QVariantList invitations READ invitations NOTIFY invitationsChanged)
    Q_PROPERTY(bool serverUploadAvailable READ serverUploadAvailable NOTIFY uploadSupportChanged)
    Q_PROPERTY(bool editing READ editing NOTIFY editingChanged)
    Q_PROPERTY(bool windowActive READ windowActive WRITE setWindowActive NOTIFY windowActiveChanged)
    Q_PROPERTY(ContactModel *contacts READ contacts CONSTANT)
    Q_PROPERTY(MessageModel *messages READ messages CONSTANT)

public:
    Backend(const QString &configDirectory, const QString &dataDirectory, FilePicker *filePicker,
            SecretStore *secrets, Notifier *notifier = nullptr, QObject *parent = nullptr);
    ~Backend() override;

    // Signs in with the saved account, if there is one.
    void start();

    bool starting() const { return m_starting; }
    bool signedIn() const { return m_signedIn; }
    QString jid() const { return m_jid; }
    QString host() const;
    QString connectionState() const;
    QString status() const { return m_status; }
    QString errorText() const { return m_errorText; }
    QString certificateFingerprint() const;
    QStringList subscriptionRequests() const { return m_requests; }
    QString currentChat() const { return m_currentChat; }
    QString currentChatName() const;
    QString currentChatPresence() const;
    QString currentChatStatusText() const;
    QString currentChatAvatar() const;
    bool currentChatTyping() const { return m_typing.contains(m_currentChat); }
    bool currentChatIsRoom() const { return m_rooms.contains(m_currentChat); }
    QStringList currentRoomParticipants() const;
    QVariantList invitations() const { return m_invitations; }
    bool serverUploadAvailable() const;
    bool editing() const { return m_editingId != 0; }
    bool windowActive() const { return m_windowActive; }
    void setWindowActive(bool active);
    ContactModel *contacts() { return &m_contacts; }
    quint16 directTransferPort() const;
    QXmppClient *client() const { return m_client.get(); }
    MessageModel *messages() { return &m_messages; }

    Q_INVOKABLE void signIn(const QString &jid, const QString &password, const QString &host, bool remember);
    Q_INVOKABLE void signOut();
    Q_INVOKABLE void setStatus(const QString &status);
    Q_INVOKABLE void dismissError();
    Q_INVOKABLE void trustCertificate();
    Q_INVOKABLE void rejectCertificate();

    Q_INVOKABLE void openChat(const QString &jid);
    Q_INVOKABLE void closeChat();
    // Takes a conversation off the sidebar; its history stays, and a new
    // message opens it again.
    Q_INVOKABLE void closeConversation(const QString &jid);
    Q_INVOKABLE bool sendMessage(const QString &text);
    // Up in an empty message field: our last line comes back to be
    // corrected (XEP-0308), and the next send replaces it.
    Q_INVOKABLE QString startEditingLast();
    Q_INVOKABLE void cancelEditing();
    // The message field has text (or was just cleared); the contact sees
    // "typing" while it does.
    Q_INVOKABLE void setComposing(bool composing);

    // How a file travels: "server" uploads it (XEP-0363) and sends the link,
    // which works for rooms and offline contacts; "direct" streams it to the
    // contact's device over SI (XEP-0096), as Pidgin expects.
    Q_INVOKABLE void chooseFileToSend(const QString &method = QStringLiteral("server"));
    Q_INVOKABLE void sendFile(const QString &path, const QString &method = QStringLiteral("server"));
    Q_INVOKABLE void downloadFile(qint64 messageId);
    Q_INVOKABLE void acceptFile(qint64 messageId);
    Q_INVOKABLE void refuseFile(qint64 messageId);
    Q_INVOKABLE void cancelTransfer(qint64 messageId);
    Q_INVOKABLE void openFile(const QString &path);
    // A message as rich text with clickable links, and opening one of them.
    Q_INVOKABLE QString formatMessage(const QString &body, const QString &linkColor) const;
    Q_INVOKABLE void openLink(const QString &url);
    // The selected messages as "[date time] Name: text" lines, to the clipboard.
    Q_INVOKABLE void copySelectedMessages();
    QString selectedMessagesText() const;
    // Gajim's history for this account: where it is and how many messages
    // it holds ({path, found, error}), and bringing them over.
    Q_INVOKABLE QVariantMap findGajimHistory() const;
    Q_INVOKABLE QVariantMap importGajimHistory(const QString &path);

    Q_INVOKABLE bool addContact(const QString &jid, const QString &name);
    Q_INVOKABLE void removeContact(const QString &jid);
    // A name for a contact on this computer only; empty goes back to the
    // roster's name.
    Q_INVOKABLE void setAlias(const QString &jid, const QString &alias);
    Q_INVOKABLE void acceptRequest(const QString &jid);
    // Joins a group chat and keeps it as a bookmark on the server, so it's
    // there on the next sign-in too.
    Q_INVOKABLE bool joinRoom(const QString &jid, const QString &nickname, const QString &password = {});
    Q_INVOKABLE void leaveRoom(const QString &jid);
    Q_INVOKABLE void acceptInvitation(int index);
    Q_INVOKABLE void declineInvitation(int index);
    Q_INVOKABLE void refuseRequest(const QString &jid);

    static QString presenceName(const QXmppPresence &presence);
    static QString normalizedJid(const QString &jid);

signals:
    void signedInChanged();
    void connectionStateChanged();
    void statusChanged();
    void errorTextChanged();
    void certificateChanged();
    void subscriptionRequestsChanged();
    void currentChatChanged();
    void windowActiveChanged();
    void typingChanged();
    void participantsChanged();
    void invitationsChanged();
    void uploadSupportChanged();
    void editingChanged();
    // A notification was clicked; the window should come forward.
    void raiseRequested();

private:
    struct Transfer {
        QPointer<QXmppTransferJob> job;
        QString peer;
        QString finalPath;
        QString partPath;
        bool canceledHere = false;
        // How the bytes travel, shown on the file's card: direct, proxy,
        // ibb, or socks for incoming, with -ufw when ufw forced the choice.
        QString route;
    };

    void connectToServer();
    // An empty server follows the domain's SRV records.
    void connectTo(const QString &server, int port);
    void openHistory();
    void setError(const QString &text);
    void setSignedIn(bool signedIn);
    // How a message reached us: as it was sent, or later from the server's
    // archive. Archived ones count as unread but don't notify one by one,
    // and the first sync of an account fills the history without either.
    enum class Delivery { Live, Archived, ArchivedSilently };

    bool ingest(const QXmppMessage &message, Delivery delivery);
    void syncArchive(bool firstPage);
    void finishArchiveSync();
    void handleChatState(const QString &peer, int state);
    void sendChatState(int state);

    struct Room {
        QString name;
        QString nickname;
        QString password;
        bool autojoin = true;
    };
    void addRoom(const QString &jid, const Room &room);
    void enterRoom(const QString &jid);
    void refreshRoom(const QString &jid);
    void ingestRoomMessage(const QString &roomJid, const QXmppMessage &message, Delivery delivery);
    QString roomNickname(const QString &jid) const;
    void saveBookmark(const QString &jid);
    void addInvitation(const QString &room, const QString &from, const QString &reason, const QString &password,
                       bool mediated);
    void refreshPresence(const QString &bareJid);
    void syncRoster();
    void showContact(const QString &jid);
    // Puts a conversation in the sidebar with its newest line, or at the
    // top when it has none yet.
    void openConversation(const QString &jid);
    QString previewOf(const HistoryEntry &entry) const;
    void handleAvatarHash(const QXmppPresence &presence);
    void fetchAvatar(const QString &jid);
    QString bestResource(const QString &bareJid) const;
    qint64 addEntry(HistoryEntry &entry, Delivery delivery = Delivery::Live);
    void updateTransfer(qint64 id, const QString &state, const QString &path);
    void watchTransfer(qint64 id, QXmppTransferJob *job);
    // Finds the server's SOCKS5 proxy (XEP-0065) for direct transfers.
    void discoverProxy();
    void sendFileDirect(const QFileInfo &info);
    void uploadFile(const QFileInfo &info);
    // Sends the link of an uploaded file and returns the message's id.
    QString sendFileLink(const QString &peer, const QUrl &url);
    void applyOutOfBand(HistoryEntry &entry, const QXmppMessage &message) const;
    // Applies a correction to the line it replaces, if its author wrote it.
    bool applyCorrection(const HistoryEntry &correction, const QString &replaceId);
    bool sendCorrection(const QString &body);
    bool trustsCertificateOf(const QList<QSslError> &errors) const;
    // Shows a picture for an image file line: the file itself when it's on
    // disk, or a copy fetched into the preview cache from a known contact.
    void ensurePreview(const HistoryEntry &entry);
    QString previewPath(const QString &url) const;
    void finishTransfer(qint64 id);
    QList<QSslCertificate> trustedCertificates() const;

    QSettings m_settings;
    QString m_dataDirectory;
    FilePicker *m_filePicker;
    SecretStore *m_secrets;
    Notifier *m_notifier;

    std::unique_ptr<QXmppClient> m_client;
    QXmppRosterManager *m_roster = nullptr;
    QXmppTransferManager *m_transfers = nullptr;
    QXmppSocksServer *m_socks = nullptr;
    QXmppMamManager *m_mam = nullptr;
    QXmppVCardManager *m_vcards = nullptr;
    std::unique_ptr<AvatarStore> m_avatars;
    QSet<QString> m_fetchingAvatars;
    QXmppMucManagerV2 *m_muc = nullptr;
    QXmppPepBookmarkManager *m_bookmarks = nullptr;
    QHash<QString, Room> m_rooms;
    QSet<QString> m_joiningRooms;
    QVariantList m_invitations;

    // Files through the server: uploads and downloads by history line.
    QNetworkAccessManager *m_network = nullptr;
    QXmppHttpUploadManager *m_uploads = nullptr;
    QHash<qint64, std::shared_ptr<QXmppHttpUpload>> m_uploadJobs;
    QHash<qint64, QNetworkReply *> m_downloads;
    QHash<QString, QNetworkReply *> m_previewDownloads;
    // Contacts in the roster, cached or from the server: whose links are
    // fetched for previews.
    QSet<QString> m_knownContacts;
    QString m_pickMethod = QStringLiteral("server");
    QString m_proxy;
    qint64 m_editingId = 0;
    bool m_syncing = false;
    int m_archivedUnread = 0;
    bool m_reloadCurrentChat = false;

    // Chat states (XEP-0085): who is typing to us, until when, and who sends
    // states at all, since the spec asks to send them only to those.
    QHash<QString, QDateTime> m_typing;
    QSet<QString> m_chatStatePeers;
    QTimer *m_typingExpiry = nullptr;
    QTimer *m_pauseTimer = nullptr;
    int m_sentState = 0;

    History m_history;
    ContactModel m_contacts;
    MessageModel m_messages;
    QHash<qint64, Transfer> m_jobs;

    bool m_starting = false;
    bool m_resolving = false;
    bool m_signedIn = false;
    QString m_jid;
    QString m_password;
    QString m_status = QStringLiteral("online");
    QString m_errorText;
    QSslCertificate m_pendingCertificate;
    QStringList m_requests;
    QString m_currentChat;
    bool m_windowActive = true;
};
