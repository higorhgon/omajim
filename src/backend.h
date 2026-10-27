#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QSettings>
#include <QSslCertificate>
#include <QStringList>
#include <memory>

#include "avatarstore.h"
#include "contactmodel.h"
#include "history.h"
#include "messagemodel.h"

class FilePicker;
class Notifier;
class QXmppClient;
class QXmppMamManager;
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
    bool windowActive() const { return m_windowActive; }
    void setWindowActive(bool active);
    ContactModel *contacts() { return &m_contacts; }
    quint16 directTransferPort() const;
    MessageModel *messages() { return &m_messages; }

    Q_INVOKABLE void signIn(const QString &jid, const QString &password, const QString &host, bool remember);
    Q_INVOKABLE void signOut();
    Q_INVOKABLE void setStatus(const QString &status);
    Q_INVOKABLE void dismissError();
    Q_INVOKABLE void trustCertificate();
    Q_INVOKABLE void rejectCertificate();

    Q_INVOKABLE void openChat(const QString &jid);
    Q_INVOKABLE void closeChat();
    Q_INVOKABLE bool sendMessage(const QString &text);

    Q_INVOKABLE void chooseFileToSend();
    Q_INVOKABLE void sendFile(const QString &path);
    Q_INVOKABLE void acceptFile(qint64 messageId);
    Q_INVOKABLE void refuseFile(qint64 messageId);
    Q_INVOKABLE void cancelTransfer(qint64 messageId);
    Q_INVOKABLE void openFile(const QString &path);

    Q_INVOKABLE bool addContact(const QString &jid, const QString &name);
    Q_INVOKABLE void removeContact(const QString &jid);
    Q_INVOKABLE void acceptRequest(const QString &jid);
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
    // A notification was clicked; the window should come forward.
    void raiseRequested();

private:
    struct Transfer {
        QPointer<QXmppTransferJob> job;
        QString peer;
        QString finalPath;
        QString partPath;
        bool canceledHere = false;
    };

    void connectToServer();
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
    void refreshPresence(const QString &bareJid);
    void syncRoster();
    void showContact(const QString &jid);
    void handleAvatarHash(const QXmppPresence &presence);
    void fetchAvatar(const QString &jid);
    QString bestResource(const QString &bareJid) const;
    qint64 addEntry(HistoryEntry &entry, Delivery delivery = Delivery::Live);
    void updateTransfer(qint64 id, const QString &state, const QString &path);
    void watchTransfer(qint64 id, QXmppTransferJob *job);
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
    bool m_syncing = false;
    int m_archivedUnread = 0;
    bool m_reloadCurrentChat = false;

    History m_history;
    ContactModel m_contacts;
    MessageModel m_messages;
    QHash<qint64, Transfer> m_jobs;

    bool m_starting = false;
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
