#include "backend.h"

#include <QCollator>
#include <QDnsLookup>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>
#include <QClipboard>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QImage>
#include <QMimeData>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSslConfiguration>
#include <QStandardPaths>
#include <QUrl>

#include <QXmppAuthenticationError.h>
#include <QXmppCarbonManagerV2.h>
#include <QXmppClient.h>
#include <QXmppConfiguration.h>
#include <QXmppMamManager.h>
#include <QXmppDiscoveryIq.h>
#include <QXmppDiscoveryManager.h>
#include <QXmppHttpUploadManager.h>
#include <QXmppLogger.h>
#include <QXmppFallback.h>
#include <QXmppMessage.h>
#include <QXmppMessageReaction.h>
#include <QXmppMessageReceiptManager.h>
#include <QXmppMucData.h>
#include <QXmppMucManagerV2.h>
#include <QXmppPepBookmarkManager.h>
#include <QXmppPubSubManager.h>
#include <QXmppPresence.h>
#include <QXmppRosterManager.h>
#include <QXmppSocks.h>
#include <QXmppTask.h>
#include <QXmppTransferManager.h>
#include <QXmppStanzaId.h>
#include <QXmppUtils.h>
#include <QXmppVCardIq.h>
#include <QXmppVersionManager.h>
#include <QXmppVCardManager.h>

#include "filepicker.h"
#include "files.h"
#include "firewall.h"
#include "gajimimport.h"
#include "notifier.h"
#include "rosterstore.h"
#include "textformat.h"
#include "secretstore.h"

static const int HistoryLimit = 300;

// For diagnosing a slow sign-in: OMAJIM_DEBUG=1 prints when each step
// happens, counted from startup. Never the stanzas themselves, since
// authentication carries the password.
static bool tracing() {
    static const bool enabled = qEnvironmentVariableIsSet("OMAJIM_DEBUG");
    return enabled;
}

static void trace(const QString &step) {
    static QElapsedTimer clock = [] {
        QElapsedTimer timer;
        timer.start();
        return timer;
    }();
    if (tracing())
        fprintf(stderr, "[omajim %6lld ms] %s\n", clock.elapsed(), qPrintable(step));
}

// Contacts connect to this port to fetch a file sent directly. QXmpp picks a
// random one, which no firewall rule can allow, so use a fixed port, the one
// the Python Omajim used, and an existing ufw rule keeps working.
static const quint16 DirectTransferPort = 28011;

// Messages per archive request. The first sync of an account fetches one page
// only, so history from years back doesn't download on the first sign-in.
static const int ArchivePageSize = 100;

// Omajim's mark on a forwarded message, so another Omajim shows it as one;
// other clients ignore it and show the text, header and all.
static const QString ForwardedNamespace = QStringLiteral("https://github.com/higorhgon/omajim/forwarded");

static bool isForwarded(const QXmppMessage &message) {
    for (const auto &element : message.extensions().getAll<QXmpp::Xml::Element>())
        if (element.tag() == "forwarded" && element.xmlns() == ForwardedNamespace)
            return true;
    return false;
}

static void markForwarded(QXmppMessage &message) {
    message.extensions().add(QXmpp::Xml::Element(QStringLiteral("forwarded"), ForwardedNamespace));
}

// Lines of history to ask a room for the first time we join it.
static const int RoomHistoryStanzas = 50;

// Pictures larger than this aren't fetched just to preview them.
static const qint64 PreviewLimit = 10 * 1024 * 1024;

Backend::Backend(const QString &configDirectory, const QString &dataDirectory, FilePicker *filePicker,
                 SecretStore *secrets, Notifier *notifier, QObject *parent)
    : QObject(parent),
      m_settings(QDir(configDirectory).filePath("omajim.conf"), QSettings::IniFormat),
      m_dataDirectory(dataDirectory),
      m_filePicker(filePicker),
      m_secrets(secrets),
      m_notifier(notifier),
      m_client(std::make_unique<QXmppClient>()) {
    trace("start");
    if (tracing()) {
        auto *logger = new QXmppLogger(this);
        logger->setLoggingType(QXmppLogger::SignalLogging);
        logger->setMessageTypes(QXmppLogger::InformationMessage | QXmppLogger::WarningMessage);
        connect(logger, &QXmppLogger::message, this, [](QXmppLogger::MessageType, const QString &text) {
            trace("qxmpp: " + text);
        });
        m_client->setLogger(logger);
    }
    m_jid = m_settings.value("account/jid").toString();
    m_contacts.setBrowsing(m_settings.value("ui/showAllContacts", false).toBool());
    m_status = m_settings.value("account/status", "online").toString();

    // Contacts' clients show who they talk to: Omajim, not QXmpp's defaults.
    if (auto *version = m_client->findExtension<QXmppVersionManager>()) {
        version->setClientName("Omajim");
        version->setClientVersion(QCoreApplication::applicationVersion());
    }
    if (auto *disco = m_client->findExtension<QXmppDiscoveryManager>()) {
        disco->setClientName("Omajim");
        disco->setClientCategory("client");
        disco->setClientType("pc");
        disco->setClientCapabilitiesNode("https://github.com/higorhgon/omajim");
    }
    m_roster = m_client->findExtension<QXmppRosterManager>();
    // The roster lives in the account's database between runs.
    m_roster->setStorage(std::make_unique<RosterStore>(&m_history));
    m_client->addNewExtension<QXmppCarbonManagerV2>();
    m_mam = m_client->addNewExtension<QXmppMamManager>();
    m_vcards = m_client->findExtension<QXmppVCardManager>();
    if (!m_vcards)
        m_vcards = m_client->addNewExtension<QXmppVCardManager>();
    connect(m_client.get(), &QXmppClient::presenceReceived, this, &Backend::handleAvatarHash);

    auto *receipts = m_client->addNewExtension<QXmppMessageReceiptManager>();
    connect(receipts, &QXmppMessageReceiptManager::messageDelivered, this, [this](const QString &jid, const QString &id) {
        const QString peer = QXmppUtils::jidToBareJid(jid).toLower();
        if (m_history.markDelivered(peer, id) && peer == m_currentChat)
            m_messages.markDelivered(id);
    });

    // Group chats (XEP-0045), and the rooms kept as bookmarks on the server
    // (XEP-0402), which needs PubSub underneath.
    m_client->addNewExtension<QXmppPubSubManager>();
    m_bookmarks = m_client->addNewExtension<QXmppPepBookmarkManager>();
    m_muc = m_client->addNewExtension<QXmppMucManagerV2>();
    auto loadBookmarks = [this](const QList<QXmppMucBookmark> &bookmarks) {
        for (const QXmppMucBookmark &bookmark : bookmarks) {
            const QString jid = bookmark.jid().toLower();
            addRoom(jid, {bookmark.name(), bookmark.nick(), bookmark.password(), bookmark.autojoin()});
            if (bookmark.autojoin())
                enterRoom(jid);
        }
    };
    connect(m_bookmarks, &QXmppPepBookmarkManager::bookmarksReset, this, [this, loadBookmarks] {
        trace("bookmarks: loaded");
        if (const auto &bookmarks = m_bookmarks->bookmarks())
            loadBookmarks(*bookmarks);
    });
    connect(m_bookmarks, &QXmppPepBookmarkManager::bookmarksAdded, this, loadBookmarks);
    connect(m_bookmarks, &QXmppPepBookmarkManager::bookmarksChanged, this,
            [loadBookmarks](const QList<QXmppPepBookmarkManager::BookmarkChange> &changes) {
                QList<QXmppMucBookmark> updated;
                for (const auto &change : changes)
                    updated << change.newBookmark;
                loadBookmarks(updated);
            });
    // Removed from another device: leave here too.
    connect(m_bookmarks, &QXmppPepBookmarkManager::bookmarksRemoved, this, [this](const QList<QString> &jids) {
        for (const QString &jid : jids)
            if (m_rooms.contains(jid.toLower()))
                leaveRoom(jid.toLower());
    });

    connect(m_muc, &QXmppMucManagerV2::messageReceived, this, [this](const QString &room, const QXmppMessage &message) {
        ingestRoomMessage(room.toLower(), message, Delivery::Live);
    });
    connect(m_muc, &QXmppMucManagerV2::roomHistoryReceived, this,
            [this](const QString &room, const QList<QXmppMessage> &messages) {
                for (const QXmppMessage &message : messages)
                    ingestRoomMessage(room.toLower(), message, Delivery::ArchivedSilently);
                if (m_reloadCurrentChat && !m_currentChat.isEmpty())
                    m_messages.reset(m_history.recent(m_currentChat, HistoryLimit));
                m_reloadCurrentChat = false;
            });
    connect(m_muc, &QXmppMucManagerV2::participantJoined, this,
            [this](const QString &room, const QXmppMucParticipant &) { refreshRoom(room.toLower()); });
    connect(m_muc, &QXmppMucManagerV2::participantLeft, this,
            [this](const QString &room, const QXmppMucParticipant &, QXmpp::Muc::LeaveReason) { refreshRoom(room.toLower()); });
    connect(m_muc, &QXmppMucManagerV2::removedFromRoom, this,
            [this](const QString &roomJid, QXmpp::Muc::LeaveReason reason, const std::optional<QXmpp::Muc::Destroy> &) {
                const QString room = roomJid.toLower();
                m_contacts.setPresence(room, "offline", {});
                const QString name = m_contacts.nameOf(room);
                switch (reason) {
                case QXmpp::Muc::LeaveReason::Kicked: setError(tr("You were removed from %1.").arg(name)); break;
                case QXmpp::Muc::LeaveReason::Banned: setError(tr("You were banned from %1.").arg(name)); break;
                case QXmpp::Muc::LeaveReason::RoomDestroyed: setError(tr("%1 was closed.").arg(name)); break;
                case QXmpp::Muc::LeaveReason::AffiliationChanged:
                case QXmpp::Muc::LeaveReason::MembersOnly:
                    setError(tr("%1 is now for members only, and you aren't one.").arg(name));
                    break;
                case QXmpp::Muc::LeaveReason::ServiceShutdown:
                    setError(tr("The group chat service of %1 shut down.").arg(name));
                    break;
                case QXmpp::Muc::LeaveReason::Error: setError(tr("You left %1 because of an error.").arg(name)); break;
                default: break;
                }
                if (room == m_currentChat)
                    emit participantsChanged();
            });
    connect(m_muc, &QXmppMucManagerV2::invitationReceived, this,
            [this](const QString &room, const QXmpp::Muc::Invite &invite, const QString &password) {
                addInvitation(room, invite.from(), invite.reason(), password, true);
            });
    connect(m_muc, &QXmppMucManagerV2::directInvitationReceived, this,
            [this](const QXmpp::Muc::DirectInvitation &invitation, const QString &from, const QXmppMessage &) {
                addInvitation(invitation.jid(), from, invitation.reason(), invitation.password(), false);
            });

    // Files through the server share one network manager, which trusts the
    // certificates the user accepted for the XMPP server too.
    m_network = new QNetworkAccessManager(this);
    connect(m_network, &QNetworkAccessManager::sslErrors, this, [this](QNetworkReply *reply, const QList<QSslError> &errors) {
        if (trustsCertificateOf(errors))
            reply->ignoreSslErrors(errors);
    });
    m_uploads = m_client->addNewExtension<QXmppHttpUploadManager>(m_network);
    connect(m_uploads, &QXmppHttpUploadManager::supportChanged, this, &Backend::uploadSupportChanged);

    // Some clients never send "paused", so a typing notice fades on its own.
    m_typingExpiry = new QTimer(this);
    m_typingExpiry->setInterval(5000);
    connect(m_typingExpiry, &QTimer::timeout, this, [this] {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        bool changed = false;
        for (auto it = m_typing.begin(); it != m_typing.end();) {
            if (it.value() < now) {
                it = m_typing.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
        if (m_typing.isEmpty())
            m_typingExpiry->stop();
        if (changed)
            emit typingChanged();
    });
    m_pauseTimer = new QTimer(this);
    m_pauseTimer->setSingleShot(true);
    m_pauseTimer->setInterval(5000);
    connect(m_pauseTimer, &QTimer::timeout, this, [this] { sendChatState(QXmppMessage::Paused); });
    m_transfers = m_client->addNewExtension<QXmppTransferManager>();
    if (auto *socks = m_transfers->findChild<QXmppSocksServer *>()) {
        socks->close();
        // Another program on the port (the Python Omajim, say) shouldn't
        // stop direct transfers; they work on any port the firewall allows.
        if (!socks->listen(DirectTransferPort))
            socks->listen();
        m_socks = socks;
    }

    connect(m_client.get(), &QXmppClient::stateChanged, this, [this](QXmppClient::State state) {
        trace(QStringLiteral("state: %1").arg(state == QXmppClient::ConnectedState ? "connected"
                                              : state == QXmppClient::ConnectingState ? "connecting" : "disconnected"));
        if (state == QXmppClient::DisconnectedState) {
            m_contacts.setAllOffline();
            emit currentChatChanged();
        }
        if (state == QXmppClient::ConnectedState) {
            dismissError();
            // Whatever arrived while we were away, or went through another
            // of the account's devices.
            syncArchive(m_settings.value("archive/" + m_jid + "/last").toString().isEmpty());
            discoverProxy();
            // The bookmarks bring their rooms back too, but a server without
            // bookmarks still has the rooms joined in this session.
            for (auto it = m_rooms.cbegin(); it != m_rooms.cend(); ++it)
                if (it->autojoin)
                    enterRoom(it.key());
        }
        if (state == QXmppClient::DisconnectedState)
            m_joiningRooms.clear();
        emit connectionStateChanged();
    });
    connect(m_client.get(), &QXmppClient::errorOccurred, this, [this](const QXmppError &error) {
        if (error.holdsType<QXmpp::AuthenticationError>()) {
            // A rejected password won't start working on a retry, so stop
            // reconnecting and ask for it again.
            m_client->disconnectFromServer();
            m_password.clear();
            setSignedIn(false);
            setError(tr("The server didn't accept the password for %1.").arg(m_jid));
            return;
        }
        if (m_pendingCertificate.isNull())
            setError(tr("Connection problem: %1").arg(error.description));
    });
    connect(m_client.get(), &QXmppClient::sslErrors, this, [this](const QList<QSslError> &errors) {
        for (const QSslError &error : errors) {
            if (error.certificate().isNull())
                continue;
            // Stop before authenticating: the password must never go to a
            // server whose certificate the user hasn't accepted.
            m_client->disconnectFromServer();
            m_pendingCertificate = error.certificate();
            setError(tr("The server's certificate isn't trusted: %1").arg(error.errorString()));
            emit certificateChanged();
            return;
        }
    });
    connect(m_client.get(), &QXmppClient::messageReceived, this,
            [this](const QXmppMessage &message) { ingest(message, Delivery::Live); });

    connect(m_roster, &QXmppRosterManager::rosterReceived, this, &Backend::syncRoster);
    connect(m_roster, &QXmppRosterManager::itemAdded, this, &Backend::showContact);
    connect(m_roster, &QXmppRosterManager::itemChanged, this, &Backend::showContact);
    connect(m_roster, &QXmppRosterManager::itemRemoved, this, [this](const QString &jid) {
        m_knownContacts.remove(jid);
        m_contacts.remove(jid);
        if (jid == m_currentChat)
            closeChat();
    });
    connect(m_roster, &QXmppRosterManager::presenceChanged, this,
            [this](const QString &jid, const QString &) { refreshPresence(jid); });
    connect(m_roster, &QXmppRosterManager::subscriptionRequestReceived, this,
            [this](const QString &jid, const QXmppPresence &) {
                if (m_requests.contains(jid))
                    return;
                m_requests << jid;
                emit subscriptionRequestsChanged();
            });

    connect(m_transfers, &QXmppTransferManager::fileReceived, this, [this](QXmppTransferJob *job) {
        HistoryEntry entry;
        entry.peer = QXmppUtils::jidToBareJid(job->jid());
        entry.kind = "file";
        entry.body = Files::safeFileName(job->fileName());
        entry.fileSize = job->fileSize();
        entry.state = "offered";
        if (!addEntry(entry)) {
            job->abort();
            return;
        }
        m_jobs.insert(entry.id, {job, entry.peer, {}, {}, false});
        watchTransfer(entry.id, job);
    });

    connect(&m_contacts, &ContactModel::contactChanged, this, [this](const QString &jid) {
        if (jid == m_currentChat)
            emit currentChatChanged();
    });

    if (m_filePicker) {
        connect(m_filePicker, &FilePicker::picked, this, [this](const QString &path) {
            if (!path.isEmpty())
                sendFile(path, m_pickMethod);
        });
        connect(m_filePicker, &FilePicker::failed, this, &Backend::setError);
        connect(m_filePicker, &FilePicker::savePicked, this, [this](const QString &target) {
            const QString source = m_saveSource;
            m_saveSource.clear();
            if (target.isEmpty() || source.isEmpty() || target == source)
                return;
            // Written beside the target and renamed, so a failed copy never
            // leaves half a file.
            QFile in(source);
            QSaveFile out(target);
            if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly)) {
                setError(tr("%1 couldn't be saved to %2.").arg(QFileInfo(source).fileName(), target));
                return;
            }
            while (!in.atEnd())
                if (out.write(in.read(1 << 20)) < 0)
                    break;
            if (!out.commit())
                setError(tr("%1 couldn't be saved to %2.").arg(QFileInfo(source).fileName(), target));
        });
    }
    if (m_notifier) {
        connect(m_notifier, &Notifier::activated, this, [this](const QString &jid) {
            openChat(jid);
            emit raiseRequested();
        });
    }
}

Backend::~Backend() {
    // Leave cleanly so the server sends "offline" to contacts right away.
    if (m_client->isConnected())
        m_client->disconnectFromServer();
}

void Backend::start() {
    if (m_jid.isEmpty() || !m_secrets)
        return;
    m_starting = true;
    emit signedInChanged();
    trace("keyring: looking up the password");
    m_secrets->lookup(m_jid, [this](const QString &password, const QString &error) {
        trace("keyring: done");
        m_starting = false;
        if (!error.isEmpty())
            setError(error);
        if (password.isEmpty()) {
            emit signedInChanged();
            return;
        }
        m_password = password;
        openHistory();
        setSignedIn(true);
        connectToServer();
    });
}

quint16 Backend::directTransferPort() const {
    return m_socks ? m_socks->serverPort() : 0;
}

QString Backend::host() const {
    return m_settings.value("account/host").toString();
}

QString Backend::connectionState() const {
    switch (m_client->state()) {
    case QXmppClient::ConnectedState: return "online";
    case QXmppClient::ConnectingState: return "connecting";
    default: return "offline";
    }
}

QString Backend::certificateFingerprint() const {
    if (m_pendingCertificate.isNull())
        return {};
    return QString::fromLatin1(m_pendingCertificate.digest(QCryptographicHash::Sha256).toHex(':').toUpper());
}

QString Backend::currentChatName() const {
    if (m_currentChat.isEmpty())
        return {};
    return m_contacts.nameOf(m_currentChat);
}

QString Backend::currentChatPresence() const {
    return m_contacts.contains(m_currentChat) ? m_contacts.contact(m_currentChat).presence : QString("offline");
}

QString Backend::currentChatAvatar() const {
    return m_contacts.contact(m_currentChat).avatar;
}

QString Backend::currentChatStatusText() const {
    return m_contacts.contact(m_currentChat).statusText;
}

void Backend::setWindowActive(bool active) {
    if (active == m_windowActive)
        return;
    m_windowActive = active;
    if (active && !m_currentChat.isEmpty())
        m_contacts.clearUnread(m_currentChat);
    emit windowActiveChanged();
}

QString Backend::normalizedJid(const QString &jid) {
    return QXmppUtils::jidToBareJid(jid.trimmed()).toLower();
}

QString Backend::presenceName(const QXmppPresence &presence) {
    if (presence.type() != QXmppPresence::Available)
        return "offline";
    switch (presence.availableStatusType()) {
    case QXmppPresence::Chat: return "chat";
    case QXmppPresence::Away: return "away";
    case QXmppPresence::XA: return "xa";
    case QXmppPresence::DND: return "dnd";
    default: return "online";
    }
}

static QXmppPresence presenceFor(const QString &status) {
    QXmppPresence presence(status == "offline" ? QXmppPresence::Unavailable : QXmppPresence::Available);
    if (status == "away")
        presence.setAvailableStatusType(QXmppPresence::Away);
    else if (status == "xa")
        presence.setAvailableStatusType(QXmppPresence::XA);
    else if (status == "dnd")
        presence.setAvailableStatusType(QXmppPresence::DND);
    else if (status == "chat")
        presence.setAvailableStatusType(QXmppPresence::Chat);
    return presence;
}

void Backend::signIn(const QString &jid, const QString &password, const QString &host, bool remember) {
    const QString bare = normalizedJid(jid);
    if (!bare.contains('@') || bare.startsWith('@') || bare.endsWith('@')) {
        setError(tr("Write the address as name@server."));
        return;
    }
    if (password.isEmpty()) {
        setError(tr("Write the password."));
        return;
    }

    if (bare != m_jid) {
        m_contacts.clear();
        m_rooms.clear();
        m_invitations.clear();
        emit invitationsChanged();
        closeChat();
    }
    m_jid = bare;
    m_password = password;
    m_settings.setValue("account/jid", bare);
    m_settings.setValue("account/host", host.trimmed());
    if (m_status == "offline")
        setStatus("online");

    if (m_secrets) {
        auto done = [this](const QString &error) {
            if (!error.isEmpty())
                setError(error);
        };
        if (remember)
            m_secrets->store(bare, password, done);
        else
            m_secrets->clear(bare, done);
    }

    dismissError();
    openHistory();
    setSignedIn(true);
    connectToServer();
}

void Backend::signOut() {
    m_client->disconnectFromServer();
    if (m_secrets)
        m_secrets->clear(m_jid, [this](const QString &error) {
            if (!error.isEmpty())
                setError(error);
        });
    m_password.clear();
    m_contacts.clear();
    m_rooms.clear();
    m_requests.clear();
    m_invitations.clear();
    emit subscriptionRequestsChanged();
    emit invitationsChanged();
    closeChat();
    setSignedIn(false);
}

void Backend::setStatus(const QString &status) {
    // A status picked by hand ends any automatic away.
    m_statusBeforeIdle.clear();
    applyStatus(status, true);
}

void Backend::applyStatus(const QString &status, bool remember) {
    static const QStringList known{"online", "chat", "away", "xa", "dnd", "offline"};
    if (!known.contains(status))
        return;
    if (status != m_status) {
        m_status = status;
        if (remember)
            m_settings.setValue("account/status", status);
        emit statusChanged();
    }
    if (status == "offline") {
        m_client->disconnectFromServer();
    } else if (m_client->isConnected()) {
        m_client->setClientPresence(presenceFor(status));
    } else if (m_signedIn && m_client->state() == QXmppClient::DisconnectedState) {
        connectToServer();
    }
}

void Backend::dismissError() {
    if (m_errorText.isEmpty() && m_pendingCertificate.isNull())
        return;
    m_errorText.clear();
    m_pendingCertificate.clear();
    emit errorTextChanged();
    emit certificateChanged();
}

void Backend::trustCertificate() {
    if (m_pendingCertificate.isNull())
        return;
    QStringList pems = m_settings.value("account/certificates").toStringList();
    pems << QString::fromLatin1(m_pendingCertificate.toPem());
    m_settings.setValue("account/certificates", pems);
    dismissError();
    connectToServer();
}

void Backend::rejectCertificate() {
    dismissError();
    if (m_status != "offline")
        setStatus("offline");
}

QList<QSslCertificate> Backend::trustedCertificates() const {
    QList<QSslCertificate> certificates = QSslConfiguration::systemCaCertificates();
    for (const QString &pem : m_settings.value("account/certificates").toStringList())
        certificates += QSslCertificate::fromData(pem.toLatin1());
    return certificates;
}

void Backend::connectToServer() {
    if (m_jid.isEmpty() || m_password.isEmpty() || m_status == "offline" || m_resolving)
        return;
    const QString server = host();
    if (!server.isEmpty()) {
        // "host" or "host:port", typed in by the user.
        const qsizetype colon = server.lastIndexOf(':');
        bool numeric = false;
        const int port = colon > 0 ? server.mid(colon + 1).toInt(&numeric) : 0;
        connectTo(numeric ? server.left(colon) : server, numeric ? port : 5222);
        return;
    }

    // Without SRV records QXmpp first tries direct TLS on port 5223, and a
    // firewall that drops packets there (srv005 does) holds the sign-in for
    // minutes before it falls back to 5222. So look the records up here:
    // with any, QXmpp follows them; without, go straight to 5222 with
    // STARTTLS, as Gajim does.
    const QString domain = QXmppUtils::jidToDomain(m_jid);
    trace("dns: looking up SRV records of " + domain);
    m_resolving = true;
    auto pending = std::make_shared<int>(2);
    auto found = std::make_shared<bool>(false);
    for (const QString &service : {QStringLiteral("_xmpp-client._tcp."), QStringLiteral("_xmpps-client._tcp.")}) {
        auto *lookup = new QDnsLookup(QDnsLookup::SRV, service + domain, this);
        connect(lookup, &QDnsLookup::finished, this, [this, lookup, pending, found, domain] {
            *found = *found || (lookup->error() == QDnsLookup::NoError && !lookup->serviceRecords().isEmpty());
            lookup->deleteLater();
            if (--*pending > 0)
                return;
            m_resolving = false;
            trace(*found ? "dns: SRV records found" : "dns: no SRV records, using port 5222");
            // Signed out or switched accounts while looking up.
            if (QXmppUtils::jidToDomain(m_jid) != domain || m_password.isEmpty() || m_status == "offline")
                return;
            if (*found)
                connectTo({}, 0);
            else
                connectTo(domain, 5222);
        });
        lookup->lookup();
    }
}

void Backend::connectTo(const QString &server, int port) {
    QXmppConfiguration configuration;
    configuration.setJid(m_jid);
    configuration.setPassword(m_password);
    configuration.setResource("omajim");
    configuration.setAutoReconnectionEnabled(true);
    configuration.setCaCertificates(trustedCertificates());
    // Never fall back to an unencrypted stream: the password and every
    // message would go in the clear.
    configuration.setStreamSecurityMode(QXmppConfiguration::TLSRequired);
    if (!server.isEmpty()) {
        configuration.setHost(server);
        configuration.setPort(port);
    }
    trace(server.isEmpty() ? QStringLiteral("connecting through the SRV records")
                           : QStringLiteral("connecting to %1:%2").arg(server).arg(port));
    m_client->connectToServer(configuration, presenceFor(m_status));
}

void Backend::openHistory() {
    const QString path = QDir(m_dataDirectory).filePath("accounts/" + m_jid + "/history.sqlite");
    if (!m_history.open(path)) {
        setError(tr("The message history couldn't be opened (%1): %2").arg(path, m_history.errorString()));
        return;
    }
    m_history.failInterruptedTransfers();
    m_avatars = std::make_unique<AvatarStore>(QDir(m_dataDirectory).filePath("accounts/" + m_jid + "/avatars"));

    // Last session's contacts show right away, offline until the server
    // says otherwise, instead of an empty list while connecting.
    const auto cached = m_history.roster();
    m_knownContacts.clear();
    for (const RosterRow &row : cached) {
        m_knownContacts.insert(row.jid);
        m_contacts.upsert(row.jid, row.name);
        m_contacts.setGroups(row.jid, row.groups);
        m_contacts.setAvatar(row.jid, m_avatars->pathFor(row.jid));
    }
    trace(QStringLiteral("roster: %1 contacts from the cache").arg(cached.size()));

    const auto aliases = m_history.aliases();
    for (auto it = aliases.cbegin(); it != aliases.cend(); ++it)
        m_contacts.setAlias(it.key(), it.value());

    const QStringList open = m_history.openChats();
    for (const QString &jid : open)
        openConversation(jid);
    // Closed conversations stay findable, even with people not in the roster.
    const QStringList known = m_history.knownChats();
    for (const QString &jid : known)
        m_contacts.ensure(jid);
}

void Backend::setError(const QString &text) {
    if (text == m_errorText)
        return;
    m_errorText = text;
    emit errorTextChanged();
}

void Backend::setSignedIn(bool signedIn) {
    if (signedIn == m_signedIn)
        return;
    m_signedIn = signedIn;
    emit signedInChanged();
}

void Backend::syncRoster() {
    trace(QStringLiteral("roster: %1 contacts").arg(m_roster->getRosterBareJids().size()));
    const QStringList jids = m_roster->getRosterBareJids();
    for (const QString &jid : jids)
        showContact(jid);
    for (const QString &jid : jids)
        refreshPresence(jid);
}

void Backend::showContact(const QString &jid) {
    m_knownContacts.insert(jid);
    const auto entry = m_roster->getRosterEntry(jid);
    m_contacts.upsert(jid, entry.name());
    m_contacts.setGroups(jid, entry.groups().values());
    if (m_avatars)
        m_contacts.setAvatar(jid, m_avatars->pathFor(jid));
}

void Backend::handleAvatarHash(const QXmppPresence &presence) {
    const QString jid = QXmppUtils::jidToBareJid(presence.from()).toLower();
    if (!m_avatars || jid.isEmpty() || jid == m_jid || !m_contacts.contains(jid))
        return;
    switch (presence.vCardUpdateType()) {
    case QXmppPresence::VCardUpdateNoPhoto:
        m_avatars->clear(jid);
        m_contacts.setAvatar(jid, {});
        break;
    case QXmppPresence::VCardUpdateValidPhoto: {
        const QString hash = AvatarStore::normalizedHash(presence.photoHash());
        if (hash.isEmpty() || hash == m_avatars->hashFor(jid))
            break;
        // Someone else may already have sent us this very picture.
        if (m_avatars->has(hash)) {
            m_avatars->assign(jid, hash);
            m_contacts.setAvatar(jid, m_avatars->pathFor(jid));
        } else {
            fetchAvatar(jid);
        }
        break;
    }
    default:
        // No word on the picture (or not yet): keep what we have.
        break;
    }
}

void Backend::fetchAvatar(const QString &jid) {
    if (m_fetchingAvatars.contains(jid))
        return;
    m_fetchingAvatars.insert(jid);
    const QString account = m_jid;
    m_vcards->fetchVCard(jid).then(this, [this, jid, account](QXmppVCardManager::VCardIqResult &&result) {
        m_fetchingAvatars.remove(jid);
        // A missing picture isn't worth an error in the window.
        if (account != m_jid || !m_avatars || !std::holds_alternative<QXmppVCardIq>(result))
            return;
        const QString hash = m_avatars->save(std::get<QXmppVCardIq>(result).photo());
        if (hash.isEmpty())
            return;
        m_avatars->assign(jid, hash);
        m_contacts.setAvatar(jid, m_avatars->pathFor(jid));
    });
}

void Backend::refreshPresence(const QString &bareJid) {
    // Several devices can be signed in at once; show the most available one.
    static const QStringList rank{"chat", "online", "dnd", "away", "xa", "offline"};
    QString best = "offline", statusText;
    const auto presences = m_roster->getAllPresencesForBareJid(bareJid);
    for (const QXmppPresence &presence : presences) {
        const QString name = presenceName(presence);
        if (rank.indexOf(name) < rank.indexOf(best)) {
            best = name;
            statusText = presence.statusText();
        }
    }
    m_contacts.setPresence(bareJid, best, statusText);
}

QString Backend::bestResource(const QString &bareJid) const {
    QString best;
    int priority = std::numeric_limits<int>::min();
    const auto presences = m_roster->getAllPresencesForBareJid(bareJid);
    for (auto it = presences.begin(); it != presences.end(); ++it) {
        if (it->type() == QXmppPresence::Available && it->priority() > priority) {
            priority = it->priority();
            best = it.key();
        }
    }
    return best;
}

qint64 Backend::addEntry(HistoryEntry &entry, Delivery delivery) {
    if (!m_contacts.contains(entry.peer))
        m_contacts.upsert(entry.peer, {});
    if (!m_history.add(entry)) {
        setError(tr("The message couldn't be saved to the history: %1").arg(m_history.errorString()));
        return 0;
    }
    m_history.setChatOpen(entry.peer, true);
    m_contacts.setActivity(entry.peer, entry.stamp, previewOf(entry), entry.outgoing);
    if (entry.peer == m_currentChat) {
        // Archived lines can be older than what's on screen, so the chat
        // reloads in order once the sync is over.
        if (delivery == Delivery::Live) {
            m_messages.append(entry);
            ensurePreview(entry);
            decorate(entry);
        }
        else
            m_reloadCurrentChat = true;
    }

    if (entry.outgoing || delivery == Delivery::ArchivedSilently)
        return entry.id;
    if (entry.peer != m_currentChat || !m_windowActive) {
        m_contacts.addUnread(entry.peer);
        if (delivery == Delivery::Archived) {
            ++m_archivedUnread;
        } else if (m_notifier && m_status != "dnd") {
            const QString name = m_contacts.nameOf(entry.peer);
            const QString body = entry.kind == "file" ? tr("Wants to send you %1").arg(entry.body)
                                                      : entry.body.left(200);
            // A busy group chat would notify all day; only being named, or
            // a private message, is worth it.
            if (!m_rooms.contains(entry.peer))
                m_notifier->notify(entry.peer, name, body);
            else if (entry.body.contains(roomNickname(entry.peer), Qt::CaseInsensitive) ||
                     entry.sender.endsWith(tr("(private)")))
                m_notifier->notify(entry.peer, tr("%1 in %2").arg(entry.sender, name), body);
        }
    }
    return entry.id;
}

bool Backend::ingest(const QXmppMessage &message, Delivery delivery) {
    if (message.type() == QXmppMessage::Error) {
        if (delivery == Delivery::Live)
            setError(tr("A message to %1 didn't arrive.").arg(QXmppUtils::jidToBareJid(message.from())));
        return false;
    }
    if (message.type() == QXmppMessage::GroupChat)
        return false;

    const QString from = QXmppUtils::jidToBareJid(message.from()).toLower();
    if (delivery == Delivery::Live && from != m_jid && !m_rooms.contains(from) && message.state() != QXmppMessage::None)
        handleChatState(from, message.state());
    if (const auto reaction = message.reaction()) {
        const QString peer = from == m_jid ? QXmppUtils::jidToBareJid(message.to()).toLower() : from;
        applyReaction(peer, from, *reaction);
        return true;
    }
    if (message.body().isEmpty())
        return false;

    HistoryEntry entry;
    // A carbon of a message sent from another of our devices comes "from"
    // us; it belongs to the conversation with whoever it went to.
    entry.outgoing = from == m_jid;
    entry.peer = entry.outgoing ? QXmppUtils::jidToBareJid(message.to()).toLower() : from;
    if (entry.peer.isEmpty() || entry.peer == m_jid)
        return false;
    // A private message from someone in a group chat goes with that chat.
    if (m_rooms.contains(entry.peer))
        entry.sender = entry.outgoing ? roomNickname(entry.peer)
                                      : tr("%1 (private)").arg(QXmppUtils::jidToResource(message.from()));
    entry.body = message.body();
    if (const auto reply = message.reply()) {
        entry.replyId = reply->id;
        const QString stripped = message.readFallbackRemovedText(QXmppFallback::Body, {QStringLiteral("urn:xmpp:reply:0")});
        if (!stripped.trimmed().isEmpty())
            entry.body = stripped;
    }
    applyOutOfBand(entry, message);
    entry.forwarded = isForwarded(message);
    entry.xmppId = message.id();
    // Our server's id for it; another entity's stanza-id says nothing.
    const auto stanzaIds = message.stanzaIds();
    for (const QXmppStanzaId &stanzaId : stanzaIds)
        if (stanzaId.by.isEmpty() || QXmppUtils::jidToBareJid(stanzaId.by).toLower() == m_jid)
            entry.archiveId = stanzaId.id;
    entry.stamp = message.stamp().isValid() ? message.stamp().toUTC() : QDateTime::currentDateTimeUtc();
    if (!message.replaceId().isEmpty() && applyCorrection(entry, message.replaceId()))
        return true;
    if (m_history.contains(entry.peer, entry.xmppId, entry.archiveId))
        return false;
    return addEntry(entry, delivery) > 0;
}

void Backend::syncArchive(bool firstPage) {
    if (m_syncing || !m_history.isOpen())
        return;
    m_syncing = true;

    const QString key = "archive/" + m_jid + "/last";
    const QString last = m_settings.value(key).toString();
    QXmppResultSetQuery page;
    page.setMax(ArchivePageSize);
    if (firstPage)
        page.setBefore(QStringLiteral(""));  // An empty <before/> asks for the newest page.
    else
        page.setAfter(last);

    const QString account = m_jid;
    m_mam->retrieveMessages({}, {}, {}, {}, {}, page)
        .then(this, [this, key, account, firstPage, last](QXmppMamManager::RetrieveResult &&result) {
            m_syncing = false;
            // Signed out or switched accounts meanwhile.
            if (account != m_jid)
                return;
            if (std::holds_alternative<QXmppError>(result)) {
                // The server forgot the message we continue from (archives
                // expire), so start over from the newest page. A server
                // without an archive just has nothing to add.
                if (!firstPage && !last.isEmpty()) {
                    m_settings.remove(key);
                    syncArchive(true);
                }
                return;
            }

            const auto &retrieved = std::get<QXmppMamManager::RetrievedMessages>(result);
            const Delivery delivery = firstPage ? Delivery::ArchivedSilently : Delivery::Archived;
            for (const QXmppMessage &message : retrieved.messages)
                ingest(message, delivery);

            const QString newLast = retrieved.result.resultSetReply().last();
            if (!newLast.isEmpty())
                m_settings.setValue(key, newLast);
            if (!firstPage && !retrieved.result.complete() && !newLast.isEmpty() && newLast != last) {
                syncArchive(false);
                return;
            }
            finishArchiveSync();
        });
}

void Backend::finishArchiveSync() {
    trace("archive: synced");
    if (m_reloadCurrentChat && !m_currentChat.isEmpty())
        m_messages.reset(m_history.recent(m_currentChat, HistoryLimit));
    m_reloadCurrentChat = false;
    if (m_archivedUnread > 0 && m_notifier && m_status != "dnd")
        m_notifier->notify(QStringLiteral("archive"), QStringLiteral("Omajim"),
                           tr("%n new messages while you were away", "", m_archivedUnread));
    m_archivedUnread = 0;
}

void Backend::openChat(const QString &jid) {
    const QString bare = normalizedJid(jid);
    if (bare.isEmpty())
        return;
    if (bare != m_currentChat && (m_sentState == QXmppMessage::Composing || m_sentState == QXmppMessage::Paused))
        sendChatState(QXmppMessage::Active);
    m_pauseTimer->stop();
    m_sentState = 0;
    if (bare != m_currentChat) {
        cancelEditing();
        cancelReply();
    }
    m_currentChat = bare;
    if (!m_contacts.isOpen(bare))
        openConversation(bare);
    m_messages.reset(m_history.recent(bare, HistoryLimit));
    for (const HistoryEntry &entry : m_messages.entries()) {
        ensurePreview(entry);
        decorate(entry);
    }
    if (m_windowActive)
        m_contacts.clearUnread(bare);
    if (m_rooms.contains(bare))
        enterRoom(bare);
    emit currentChatChanged();
    emit typingChanged();
    emit participantsChanged();
}

void Backend::closeChat() {
    if (m_currentChat.isEmpty())
        return;
    m_currentChat.clear();
    m_messages.reset({});
    emit currentChatChanged();
    emit typingChanged();
}

bool Backend::sendMessage(const QString &text) {
    const QString body = text.trimmed();
    if (body.isEmpty() || m_currentChat.isEmpty())
        return false;
    if (!m_client->isAuthenticated()) {
        setError(tr("You're offline, so the message wasn't sent."));
        return false;
    }
    if (editing())
        return sendCorrection(body, xhtml);
    if (m_rooms.contains(m_currentChat)) {
        auto room = m_muc->findRoom(m_currentChat);
        if (!room || !room->joined().value()) {
            setError(tr("You aren't in %1 right now, so the message wasn't sent.").arg(currentChatName()));
            return false;
        }
        // The room echoes our message back; the id tells it apart.
        QXmppMessage message;
        message.setBody(body);
        message.setId(QXmppUtils::generateStanzaUuid());
        message.setOriginId(message.id());
        HistoryEntry entry;
        entry.peer = m_currentChat;
        entry.outgoing = true;
        entry.body = body;
        entry.xmppId = message.id();
        entry.sender = roomNickname(m_currentChat);
        quoteReply(message, entry);
        if (m_forwarding) {
            entry.forwarded = true;
            markForwarded(message);
        }
        const QString name = currentChatName();
        room->sendMessage(message).then(this, [this, name](QXmpp::Result<> &&result) {
            if (auto *error = std::get_if<QXmppError>(&result))
                setError(tr("The message to %1 wasn't sent: %2").arg(name, error->description));
        });
        addEntry(entry);
        cancelReply();
        return true;
    }

    QXmppMessage message({}, m_currentChat, body);
    message.setType(QXmppMessage::Chat);
    message.setReceiptRequested(true);
    if (m_chatStatePeers.contains(m_currentChat))
        message.setState(QXmppMessage::Active);
    m_sentState = QXmppMessage::Active;
    m_pauseTimer->stop();
    HistoryEntry entry;
    entry.peer = m_currentChat;
    entry.outgoing = true;
    entry.body = body;
    entry.xmppId = message.id();
    quoteReply(message, entry);
    if (m_forwarding) {
        entry.forwarded = true;
        markForwarded(message);
    }
    const QString peer = m_currentChat;
    m_client->send(std::move(message)).then(this, [this, peer](QXmpp::SendResult &&result) {
        if (std::holds_alternative<QXmppError>(result))
            setError(tr("The message to %1 wasn't sent: %2").arg(peer, std::get<QXmppError>(result).description));
    });
    addEntry(entry);
    cancelReply();
    return true;
}

void Backend::handleChatState(const QString &peer, int state) {
    m_chatStatePeers.insert(peer);
    const bool typing = state == QXmppMessage::Composing;
    if (typing) {
        m_typing.insert(peer, QDateTime::currentDateTimeUtc().addSecs(30));
        if (!m_typingExpiry->isActive())
            m_typingExpiry->start();
    } else if (!m_typing.remove(peer)) {
        return;
    }
    if (peer == m_currentChat)
        emit typingChanged();
}

void Backend::setComposing(bool composing) {
    if (composing) {
        // One "composing" per burst of typing, then "paused" once it stops.
        if (m_sentState != QXmppMessage::Composing)
            sendChatState(QXmppMessage::Composing);
        m_pauseTimer->start();
    } else {
        m_pauseTimer->stop();
        if (m_sentState == QXmppMessage::Composing || m_sentState == QXmppMessage::Paused)
            sendChatState(QXmppMessage::Active);
    }
}

void Backend::sendChatState(int state) {
    m_sentState = state;
    if (m_currentChat.isEmpty() || !m_chatStatePeers.contains(m_currentChat) || !m_client->isAuthenticated())
        return;
    QXmppMessage message({}, m_currentChat);
    message.setType(QXmppMessage::Chat);
    message.setState(QXmppMessage::State(state));
    m_client->send(std::move(message));
}

bool Backend::serverUploadAvailable() const {
    return m_uploads->support() == QXmppHttpUploadManager::Support::Supported;
}

void Backend::chooseFileToSend(const QString &method) {
    if (m_currentChat.isEmpty() || !m_filePicker)
        return;
    m_pickMethod = method;
    m_filePicker->openFile(tr("Send a file to %1").arg(currentChatName()));
}

void Backend::sendFile(const QString &path, const QString &method) {
    if (m_currentChat.isEmpty())
        return;
    // A file dropped on the window arrives as a file:// URL.
    const QFileInfo info(path.startsWith("file:") ? QUrl(path).toLocalFile() : path);
    if (!info.isFile() || !info.isReadable()) {
        setError(tr("%1 can't be read.").arg(path));
        return;
    }
    if (method == "direct")
        sendFileDirect(info);
    else
        uploadFile(info);
}

void Backend::sendFileDirect(const QFileInfo &info) {
    // SI goes to one person's device; a room has no device to send to.
    if (m_rooms.contains(m_currentChat)) {
        setError(tr("Files can't be sent directly to a group chat. Send them through the server instead."));
        return;
    }
    // A file goes straight to one of the contact's devices, so it has to be
    // online; the server doesn't hold it for later.
    const QString resource = bestResource(m_currentChat);
    if (!m_client->isAuthenticated() || resource.isEmpty()) {
        setError(tr("%1 is offline. Files go directly to a connected device, so try again when they're online.")
                     .arg(currentChatName()));
        return;
    }
    // The contact connects to us on our port, unless ufw drops it: then the
    // server's proxy carries the file, or, without one, the XMPP stream
    // itself (IBB), slow but sure to pass.
    const auto blocked = Firewall().isIncomingPortBlocked(directTransferPort(), "tcp", Firewall::localAddresses());
    QString route = "direct";
    m_transfers->setProxy(m_proxy);
    m_transfers->setProxyOnly(false);
    m_transfers->setSupportedMethods(QXmppTransferJob::AnyMethod);
    if (blocked.value_or(false)) {
        if (!m_proxy.isEmpty()) {
            m_transfers->setProxyOnly(true);
            route = "proxy-ufw";
        } else {
            m_transfers->setSupportedMethods(QXmppTransferJob::InBandMethod);
            route = "ibb-ufw";
        }
    }
    trace(QStringLiteral("si: sending %1 (%2)").arg(info.fileName(), route));
    QXmppTransferJob *job = m_transfers->sendFile(m_currentChat + "/" + resource, info.absoluteFilePath());
    if (!job) {
        setError(tr("The transfer of %1 couldn't start.").arg(info.fileName()));
        return;
    }

    HistoryEntry entry;
    entry.peer = m_currentChat;
    entry.outgoing = true;
    entry.kind = "file";
    entry.body = info.fileName();
    entry.filePath = info.absoluteFilePath();
    entry.fileSize = info.size();
    entry.state = "offered";
    if (!addEntry(entry)) {
        job->abort();
        return;
    }
    m_jobs.insert(entry.id, {job, entry.peer, entry.filePath, {}, false, route});
    m_messages.setRoute(entry.id, route);
    watchTransfer(entry.id, job);
}

void Backend::uploadFile(const QFileInfo &info) {
    if (!m_client->isAuthenticated()) {
        setError(tr("You're offline, so the file wasn't sent."));
        return;
    }
    if (!serverUploadAvailable()) {
        setError(tr("Your server doesn't take files (HTTP Upload). Send it directly to the person instead."));
        return;
    }
    const auto services = m_uploads->services();
    if (!services.isEmpty() && services.first().sizeLimit() && quint64(info.size()) > *services.first().sizeLimit()) {
        setError(tr("%1 is %2, and your server takes files up to %3.")
                     .arg(info.fileName(), Files::formatSize(info.size()),
                          Files::formatSize(qint64(*services.first().sizeLimit()))));
        return;
    }
    if (m_rooms.contains(m_currentChat)) {
        auto room = m_muc->findRoom(m_currentChat);
        if (!room || !room->joined().value()) {
            setError(tr("You aren't in %1 right now, so the file wasn't sent.").arg(currentChatName()));
            return;
        }
    }

    HistoryEntry entry;
    entry.peer = m_currentChat;
    entry.outgoing = true;
    entry.kind = "file";
    entry.body = info.fileName();
    entry.filePath = info.absoluteFilePath();
    entry.fileSize = info.size();
    entry.state = "uploading";
    if (m_rooms.contains(m_currentChat))
        entry.sender = roomNickname(m_currentChat);
    if (!addEntry(entry))
        return;

    const qint64 id = entry.id;
    const QString peer = entry.peer;
    auto upload = m_uploads->uploadFile(info);
    m_uploadJobs.insert(id, upload);
    connect(upload.get(), &QXmppHttpUpload::progressChanged, this, [this, id] {
        if (auto job = m_uploadJobs.value(id))
            m_messages.setProgress(id, job->progress());
    });
    connect(upload.get(), &QXmppHttpUpload::finished, this,
            [this, id, peer, name = info.fileName(), path = info.absoluteFilePath()](const QXmppHttpUpload::Result &result) {
        m_uploadJobs.remove(id);
        if (const QUrl *url = std::get_if<QUrl>(&result)) {
            const QString messageId = sendFileLink(peer, *url);
            m_history.setLink(id, url->toString(), messageId);
            m_messages.setLink(id, url->toString(), messageId);
            updateTransfer(id, "done", path);
        } else if (std::holds_alternative<QXmpp::Cancelled>(result)) {
            updateTransfer(id, "canceled", path);
        } else {
            updateTransfer(id, "failed", path);
            setError(tr("%1 couldn't be uploaded: %2").arg(name, std::get<QXmppError>(result).description));
        }
    });
}

QString Backend::sendFileLink(const QString &peer, const QUrl &url) {
    // The body is the link itself, as Gajim and Conversations expect, so
    // clients without file support still show something useful.
    QXmppMessage message({}, peer, url.toString());
    message.setOutOfBandUrl(url.toString());
    const QString id = QXmppUtils::generateStanzaUuid();
    message.setId(id);
    if (m_rooms.contains(peer)) {
        if (auto room = m_muc->findRoom(peer)) {
            message.setOriginId(id);
            room->sendMessage(message);
        }
        return id;
    }
    message.setType(QXmppMessage::Chat);
    message.setReceiptRequested(true);
    m_client->send(std::move(message));
    return id;
}

void Backend::applyOutOfBand(HistoryEntry &entry, const QXmppMessage &message) const {
    // A message whose whole body is the shared link is a file, not text.
    const QString url = message.outOfBandUrl();
    if (url.isEmpty() || message.body().trimmed() != url)
        return;
    const QUrl parsed(url);
    if (parsed.scheme() != "https" && parsed.scheme() != "http")
        return;
    entry.kind = "file";
    entry.url = url;
    entry.body = Files::safeFileName(parsed.fileName(QUrl::FullyDecoded));
    entry.state = entry.outgoing ? "done" : "remote";
}

void Backend::downloadFile(qint64 messageId) {
    if (m_downloads.contains(messageId))
        return;
    const QString url = m_messages.url(messageId);
    if (url.isEmpty())
        return;
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (!QDir().mkpath(directory)) {
        setError(tr("The download folder %1 can't be created.").arg(directory));
        return;
    }
    const QString path = Files::uniquePath(directory, Files::safeFileName(QUrl(url).fileName(QUrl::FullyDecoded)));
    // QSaveFile writes beside the target and renames when complete, so a
    // broken download never looks finished.
    auto *file = new QSaveFile(path);
    if (!file->open(QIODevice::WriteOnly)) {
        setError(tr("%1 can't be written: %2").arg(path, file->errorString()));
        delete file;
        return;
    }

    QNetworkReply *reply = m_network->get(QNetworkRequest(QUrl(url)));
    file->setParent(reply);
    m_downloads.insert(messageId, reply);
    updateTransfer(messageId, "downloading", path);
    connect(reply, &QNetworkReply::readyRead, file, [reply, file] { file->write(reply->readAll()); });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, messageId](qint64 done, qint64 total) {
        if (total > 0)
            m_messages.setProgress(messageId, double(done) / double(total));
    });
    connect(reply, &QNetworkReply::finished, this, [this, messageId, reply, file, path] {
        m_downloads.remove(messageId);
        file->write(reply->readAll());
        if (reply->error() == QNetworkReply::NoError && file->commit()) {
            m_messages.setProgress(messageId, 1.0);
            updateTransfer(messageId, "done", path);
        } else {
            file->cancelWriting();
            const bool canceled = reply->error() == QNetworkReply::OperationCanceledError;
            updateTransfer(messageId, canceled ? "remote" : "failed", {});
            if (!canceled)
                setError(tr("%1 couldn't be downloaded: %2").arg(QFileInfo(path).fileName(), reply->errorString()));
        }
        reply->deleteLater();
    });
}

bool Backend::trustsCertificateOf(const QList<QSslError> &errors) const {
    const QList<QSslCertificate> pinned = trustedCertificates();
    for (const QSslError &error : errors)
        if (error.certificate().isNull() || !pinned.contains(error.certificate()))
            return false;
    return !errors.isEmpty();
}

void Backend::acceptFile(qint64 messageId) {
    auto it = m_jobs.find(messageId);
    if (it == m_jobs.end() || !it->job || it->job->direction() != QXmppTransferJob::IncomingDirection)
        return;
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (!QDir().mkpath(directory)) {
        setError(tr("The download folder %1 can't be created.").arg(directory));
        return;
    }
    // It arrives as a ".part" file and gets its real name only once it's
    // complete and checked, so a broken transfer never looks finished.
    it->finalPath = Files::uniquePath(directory, Files::safeFileName(it->job->fileName()));
    it->partPath = it->finalPath + ".part";
    it->job->accept(it->partPath);
    updateTransfer(messageId, "active", it->finalPath);
}

void Backend::refuseFile(qint64 messageId) {
    auto it = m_jobs.find(messageId);
    if (it == m_jobs.end())
        return;
    it->canceledHere = true;
    if (it->job)
        it->job->abort();
    updateTransfer(messageId, "refused", {});
}

void Backend::cancelTransfer(qint64 messageId) {
    if (auto upload = m_uploadJobs.value(messageId)) {
        upload->cancel();
        return;
    }
    if (QNetworkReply *download = m_downloads.value(messageId)) {
        download->abort();
        return;
    }
    auto it = m_jobs.find(messageId);
    if (it == m_jobs.end())
        return;
    it->canceledHere = true;
    if (it->job)
        it->job->abort();
}

void Backend::openFile(const QString &path) {
    if (!QFileInfo::exists(path)) {
        setError(tr("%1 isn't there anymore.").arg(path));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void Backend::watchTransfer(qint64 id, QXmppTransferJob *job) {
    connect(job, &QXmppTransferJob::stateChanged, this, [this, id, job](QXmppTransferJob::State state) {
        if (state != QXmppTransferJob::TransferState)
            return;
        // Now the method is settled: what the other side picked of what we offered.
        auto it = m_jobs.find(id);
        if (it != m_jobs.end()) {
            const bool ufw = it->route.endsWith("-ufw");
            if (job->method() == QXmppTransferJob::InBandMethod)
                it->route = ufw ? "ibb-ufw" : "ibb";
            else if (job->direction() == QXmppTransferJob::IncomingDirection)
                it->route = "socks";
            m_messages.setRoute(id, it->route);
            trace(QStringLiteral("si: transferring by %1").arg(it->route));
        }
        updateTransfer(id, "active", m_jobs.value(id).finalPath);
    });
    connect(job, &QXmppTransferJob::progress, this, [this, id](qint64 done, qint64 total) {
        if (total > 0)
            m_messages.setProgress(id, double(done) / double(total));
    });
    connect(job, &QXmppTransferJob::finished, this, [this, id] { finishTransfer(id); });
}

void Backend::finishTransfer(qint64 id) {
    auto it = m_jobs.find(id);
    if (it == m_jobs.end() || !it->job)
        return;
    QXmppTransferJob *job = it->job;
    const bool incoming = job->direction() == QXmppTransferJob::IncomingDirection;
    const QXmppTransferJob::Error error = job->error();

    QString state, path = it->finalPath;
    if (error == QXmppTransferJob::NoError) {
        state = "done";
        if (incoming) {
            // Someone may have taken the name while it downloaded.
            if (QFileInfo::exists(path))
                path = Files::uniquePath(QFileInfo(path).absolutePath(), QFileInfo(path).fileName());
            if (!QFile::rename(it->partPath, path)) {
                state = "failed";
                setError(tr("%1 arrived but couldn't be renamed from %2.").arg(path, it->partPath));
                path = it->partPath;
            } else if (m_notifier && m_status != "dnd") {
                m_notifier->notify("file:" + path, tr("File received"),
                                   tr("%1 from %2 is in %3.")
                                       .arg(QFileInfo(path).fileName(),
                                            m_contacts.nameOf(it->peer),
                                            QFileInfo(path).absolutePath()));
            }
        }
    } else {
        if (incoming && !it->partPath.isEmpty())
            QFile::remove(it->partPath);
        if (it->canceledHere) {
            state = incoming && it->partPath.isEmpty() ? "refused" : "canceled";
        } else if (error == QXmppTransferJob::AbortError) {
            state = incoming ? "canceled" : "refused";
        } else {
            state = "failed";
            const QString name = QFileInfo(it->finalPath).fileName();
            switch (error) {
            case QXmppTransferJob::FileAccessError:
                setError(tr("%1 couldn't be read or written.").arg(name));
                break;
            case QXmppTransferJob::FileCorruptError:
                setError(tr("%1 arrived damaged (size or checksum don't match), so it was discarded.").arg(name));
                break;
            default:
                setError(tr("The transfer with %1 failed. If you're sending, the other side may not be able to "
                            "reach this computer: check the firewall (ufw).")
                             .arg(m_contacts.nameOf(it->peer)));
            }
        }
        if (!incoming)
            path = it->finalPath;
        else
            path.clear();
    }
    m_messages.setProgress(id, state == "done" ? 1.0 : 0.0);
    updateTransfer(id, state, path);
    m_jobs.erase(it);
    job->deleteLater();
}

void Backend::updateTransfer(qint64 id, const QString &state, const QString &path) {
    m_history.updateFile(id, state, path);
    m_messages.updateFile(id, state, path);
    if (state == "done" && TextFormat::isImageName(path))
        m_messages.setPreview(id, path);
}

bool Backend::addContact(const QString &jid, const QString &name) {
    const QString bare = normalizedJid(jid);
    if (!bare.contains('@')) {
        setError(tr("Write the address as name@server."));
        return false;
    }
    if (!m_client->isAuthenticated()) {
        setError(tr("You're offline, so the contact can't be added."));
        return false;
    }
    m_roster->addItem(bare, name.trimmed());
    m_roster->subscribe(bare);
    return true;
}

void Backend::removeContact(const QString &jid) {
    if (m_client->isAuthenticated())
        m_roster->removeItem(jid);
}

void Backend::acceptRequest(const QString &jid) {
    m_roster->acceptSubscription(jid);
    // Accepting someone means wanting to see them too, as other clients do.
    const auto entry = m_roster->getRosterEntry(jid);
    if (entry.subscriptionType() != QXmppRosterIq::Item::Both &&
        entry.subscriptionType() != QXmppRosterIq::Item::To)
        m_roster->subscribe(jid);
    m_requests.removeAll(jid);
    emit subscriptionRequestsChanged();
}

void Backend::refuseRequest(const QString &jid) {
    m_roster->refuseSubscription(jid);
    m_requests.removeAll(jid);
    emit subscriptionRequestsChanged();
}

QString Backend::roomNickname(const QString &jid) const {
    if (auto room = m_muc->findRoom(jid); room && room->joined().value())
        return room->nickname().value();
    const QString chosen = m_rooms.value(jid).nickname;
    return chosen.isEmpty() ? QXmppUtils::jidToUser(m_jid) : chosen;
}

QStringList Backend::currentRoomParticipants() const {
    QStringList names;
    if (!m_rooms.contains(m_currentChat))
        return names;
    if (auto room = m_muc->findRoom(m_currentChat); room && room->joined().value()) {
        const auto participants = room->participants();
        for (const QXmppMucParticipant &participant : participants)
            names << participant.nickname().value();
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(names.begin(), names.end(), [&](const QString &a, const QString &b) { return collator.compare(a, b) < 0; });
    return names;
}

void Backend::addRoom(const QString &jid, const Room &room) {
    m_rooms.insert(jid, room);
    m_contacts.upsertRoom(jid, room.name);
    if (!m_history.isKnownChat(jid))
        openConversation(jid);
    refreshRoom(jid);
}

void Backend::enterRoom(const QString &jid) {
    if (!m_client->isAuthenticated() || m_joiningRooms.contains(jid))
        return;
    if (auto room = m_muc->findRoom(jid); room && room->joined().value()) {
        refreshRoom(jid);
        return;
    }
    m_joiningRooms.insert(jid);

    // Only what we haven't stored yet: since the newest line we have, or a
    // first page of history for a room we've never been in.
    QXmpp::Muc::HistoryOptions history;
    const QDateTime last = m_history.lastStamp(jid);
    if (last.isValid())
        history.setSince(last);
    else
        history.setMaxStanzas(RoomHistoryStanzas);

    m_muc->joinRoom(jid, roomNickname(jid), history, m_rooms.value(jid).password)
        .then(this, [this, jid](QXmpp::Result<QXmppMucRoomV2> &&result) {
            m_joiningRooms.remove(jid);
            if (auto *error = std::get_if<QXmppError>(&result)) {
                setError(tr("Couldn't join %1: %2")
                             .arg(m_contacts.nameOf(jid), error->description));
            }
            refreshRoom(jid);
        });
}

void Backend::refreshRoom(const QString &jid) {
    if (!m_rooms.contains(jid))
        return;
    auto room = m_muc->findRoom(jid);
    if (room && room->joined().value())
        m_contacts.setPresence(jid, "online", room->subject().value());
    else
        m_contacts.setPresence(jid, "offline", {});
    if (jid == m_currentChat)
        emit participantsChanged();
}

void Backend::ingestRoomMessage(const QString &roomJid, const QXmppMessage &message, Delivery delivery) {
    if (const auto reaction = message.reaction()) {
        applyReaction(roomJid, QXmppUtils::jidToResource(message.from()), *reaction);
        return;
    }
    // A message without a body is a subject change or a state; the header
    // shows the subject.
    if (message.body().isEmpty()) {
        refreshRoom(roomJid);
        return;
    }
    HistoryEntry entry;
    entry.peer = roomJid;
    entry.sender = QXmppUtils::jidToResource(message.from());
    entry.outgoing = !entry.sender.isEmpty() && entry.sender == roomNickname(roomJid);
    entry.body = message.body();
    if (const auto reply = message.reply()) {
        entry.replyId = reply->id;
        const QString stripped = message.readFallbackRemovedText(QXmppFallback::Body, {QStringLiteral("urn:xmpp:reply:0")});
        if (!stripped.trimmed().isEmpty())
            entry.body = stripped;
    }
    applyOutOfBand(entry, message);
    entry.forwarded = isForwarded(message);
    entry.xmppId = message.originId().isEmpty() ? message.id() : message.originId();
    const auto stanzaIds = message.stanzaIds();
    for (const QXmppStanzaId &stanzaId : stanzaIds)
        if (QXmppUtils::jidToBareJid(stanzaId.by).toLower() == roomJid)
            entry.archiveId = stanzaId.id;
    entry.stamp = message.stamp().isValid() ? message.stamp().toUTC() : QDateTime::currentDateTimeUtc();
    if (!message.replaceId().isEmpty() && applyCorrection(entry, message.replaceId()))
        return;
    if (m_history.contains(roomJid, entry.xmppId, entry.archiveId))
        return;
    addEntry(entry, delivery);
}

void Backend::saveBookmark(const QString &jid) {
    const Room room = m_rooms.value(jid);
    m_bookmarks->setBookmark(QXmppMucBookmark(jid, room.name, room.autojoin, room.nickname, room.password))
        .then(this, [this, jid](QXmpp::Result<> &&result) {
            if (auto *error = std::get_if<QXmppError>(&result))
                setError(tr("The server didn't keep %1 as a bookmark, so it won't be back after you sign out: %2")
                             .arg(jid, error->description));
        });
}

bool Backend::joinRoom(const QString &jid, const QString &nickname, const QString &password) {
    const QString bare = normalizedJid(jid);
    if (!bare.contains('@') || bare.startsWith('@') || bare.endsWith('@')) {
        setError(tr("Write the room address as room@server."));
        return false;
    }
    if (!m_client->isAuthenticated()) {
        setError(tr("You're offline, so you can't join a group chat."));
        return false;
    }
    Room room = m_rooms.value(bare);
    room.nickname = nickname.trimmed();
    room.password = password;
    room.autojoin = true;
    addRoom(bare, room);
    saveBookmark(bare);
    enterRoom(bare);
    openChat(bare);
    return true;
}

void Backend::leaveRoom(const QString &jid) {
    if (!m_rooms.contains(jid))
        return;
    if (auto room = m_muc->findRoom(jid))
        room->leave();
    m_rooms.remove(jid);
    m_bookmarks->removeBookmark(jid);
    m_history.setChatOpen(jid, false);
    m_contacts.remove(jid);
    if (jid == m_currentChat)
        closeChat();
}

void Backend::addInvitation(const QString &room, const QString &from, const QString &reason, const QString &password,
                            bool mediated) {
    const QString jid = room.toLower();
    if (m_rooms.contains(jid))
        return;
    for (const QVariant &invitation : std::as_const(m_invitations))
        if (invitation.toMap().value("room") == jid)
            return;
    const QString inviter = QXmppUtils::jidToBareJid(from).toLower();
    m_invitations << QVariantMap{{"room", jid}, {"from", inviter}, {"reason", reason},
                                 {"password", password}, {"mediated", mediated}};
    emit invitationsChanged();
    if (m_notifier && m_status != "dnd")
        m_notifier->notify("invite:" + jid, tr("Group chat invitation"),
                           tr("%1 invites you to %2").arg(m_contacts.nameOf(inviter), jid));
}

void Backend::acceptInvitation(int index) {
    if (index < 0 || index >= m_invitations.size())
        return;
    const QVariantMap invitation = m_invitations.takeAt(index).toMap();
    emit invitationsChanged();
    joinRoom(invitation.value("room").toString(), {}, invitation.value("password").toString());
}

void Backend::declineInvitation(int index) {
    if (index < 0 || index >= m_invitations.size())
        return;
    const QVariantMap invitation = m_invitations.takeAt(index).toMap();
    emit invitationsChanged();
    // Only an invitation sent through the room can be declined back.
    if (invitation.value("mediated").toBool() && m_client->isAuthenticated()) {
        QXmpp::Muc::Decline decline;
        decline.setTo(invitation.value("from").toString());
        m_muc->declineInvitation(invitation.value("room").toString(), decline);
    }
}

bool Backend::applyCorrection(const HistoryEntry &correction, const QString &replaceId) {
    if (correction.kind != "text")
        return false;
    const HistoryEntry original = m_history.findCorrectable(correction.peer, replaceId);
    // Only whoever wrote a line may change it.
    if (original.id == 0 || original.outgoing != correction.outgoing || original.sender != correction.sender)
        return false;
    m_history.applyCorrection(original.id, correction.body, correction.xmppId);
    if (original.peer == m_currentChat)
        m_messages.applyCorrection(original.id, correction.body);
    return true;
}

QString Backend::startEditingLast() {
    return beginEditing(m_history.lastOwnText(m_currentChat));
}

bool Backend::canEdit(qint64 messageId) const {
    const HistoryEntry entry = m_messages.entry(messageId);
    // Gajim's limit (MAX_MESSAGE_CORRECTION_DELAY): an hour.
    return entry.id != 0 && entry.outgoing && entry.kind == "text" && !entry.xmppId.isEmpty()
        && entry.stamp.secsTo(QDateTime::currentDateTimeUtc()) <= 3600;
}

QString Backend::startEditing(qint64 messageId) {
    return canEdit(messageId) ? beginEditing(m_messages.entry(messageId)) : QString();
}

QString Backend::beginEditing(const HistoryEntry &entry) {
    if (entry.id == 0)
        return {};
    // One thing at a time in the message field.
    cancelReply();
    m_editing = entry;
    emit editingChanged();
    return entry.body;
}

void Backend::cancelEditing() {
    if (m_editing.id == 0)
        return;
    m_editing = {};
    emit editingChanged();
}

bool Backend::sendCorrection(const QString &body, const QString &xhtml) {
    const HistoryEntry original = m_editing;
    cancelEditing();
    if (original.peer != m_currentChat)
        return false;
    if (body == original.body && xhtml == original.xhtml)
        return true;

    QXmppMessage message({}, m_currentChat, body);
    const QString id = QXmppUtils::generateStanzaUuid();
    message.setId(id);
    // Always the first line's id, as XEP-0308 asks.
    message.setReplaceId(original.xmppId);
    const QString name = currentChatName();
    auto report = [this, name](const QString &error) {
        setError(tr("The correction to %1 wasn't sent: %2").arg(name, error));
    };
    if (m_rooms.contains(m_currentChat)) {
        auto room = m_muc->findRoom(m_currentChat);
        if (!room || !room->joined().value()) {
            setError(tr("You aren't in %1 right now, so the message wasn't sent.").arg(name));
            return false;
        }
        message.setOriginId(id);
        room->sendMessage(message).then(this, [report](QXmpp::Result<> &&result) {
            if (auto *error = std::get_if<QXmppError>(&result))
                report(error->description);
        });
    } else {
        message.setType(QXmppMessage::Chat);
        m_client->send(std::move(message)).then(this, [report](QXmpp::SendResult &&result) {
            if (auto *error = std::get_if<QXmppError>(&result))
                report(error->description);
        });
    }
    m_history.applyCorrection(original.id, body, id);
    m_messages.applyCorrection(original.id, body);
    return true;
}

QString Backend::formatMessage(const QString &body, const QString &linkColor, const QString &quoteColor) const {
    return TextFormat::toHtml(body, linkColor, quoteColor);
}

void Backend::openLink(const QString &url) {
    if (TextFormat::isOpenableLink(url))
        QDesktopServices::openUrl(QUrl(url));
}

QString Backend::previewPath(const QString &url) const {
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex());
    const QString suffix = QFileInfo(QUrl(url).fileName()).suffix().toLower();
    return QDir(m_dataDirectory).filePath("accounts/" + m_jid + "/previews/" + hash + "." + suffix);
}

void Backend::ensurePreview(const HistoryEntry &entry) {
    if (entry.kind != "file" || !TextFormat::isImageName(entry.body))
        return;
    if (!entry.filePath.isEmpty() && entry.state == "done" && QFileInfo::exists(entry.filePath)) {
        m_messages.setPreview(entry.id, entry.filePath);
        return;
    }
    if (entry.url.isEmpty())
        return;
    const QString path = previewPath(entry.url);
    if (QFileInfo::exists(path)) {
        m_messages.setPreview(entry.id, path);
        return;
    }
    // Fetching a link tells its server our address, so only for people in
    // the roster and rooms we're in, never for strangers.
    const bool known = m_rooms.contains(entry.peer) || m_knownContacts.contains(entry.peer) || entry.outgoing;
    if (!known || m_previewDownloads.contains(entry.url) || !QDir().mkpath(QFileInfo(path).absolutePath()))
        return;

    auto *file = new QSaveFile(path);
    if (!file->open(QIODevice::WriteOnly)) {
        delete file;
        return;
    }
    QNetworkReply *reply = m_network->get(QNetworkRequest(QUrl(entry.url)));
    file->setParent(reply);
    m_previewDownloads.insert(entry.url, reply);
    const qint64 id = entry.id;
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64 total) {
        if (received > PreviewLimit || total > PreviewLimit)
            reply->abort();
    });
    connect(reply, &QNetworkReply::readyRead, file, [reply, file] { file->write(reply->readAll()); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, id, url = entry.url, path] {
        m_previewDownloads.remove(url);
        file->write(reply->readAll());
        // A preview that doesn't come is no error worth showing: the file
        // can still be downloaded by hand.
        if (reply->error() == QNetworkReply::NoError && file->commit())
            m_messages.setPreview(id, path);
        else
            file->cancelWriting();
        reply->deleteLater();
    });
}

QVariantMap Backend::findGajimHistory() const {
    // The file with the most of this account's messages, when both Gajim
    // and the Python Omajim have one.
    QVariantMap best{{"path", QString()}, {"found", 0}, {"error", QString()}};
    const QStringList paths = GajimImport::candidates();
    for (const QString &path : paths) {
        const auto result = GajimImport::count(path, m_jid);
        if (!result.error.isEmpty() && best.value("found").toInt() == 0)
            best.insert("error", result.error);
        if (result.found > best.value("found").toInt())
            best = {{"path", path}, {"found", result.found}, {"error", QString()}};
    }
    return best;
}

QVariantMap Backend::importGajimHistory(const QString &path) {
    if (!m_history.isOpen())
        return {{"imported", 0}, {"error", tr("The message history isn't open.")}};
    const auto result = GajimImport::import(path, m_jid, m_history);
    if (!m_currentChat.isEmpty()) {
        m_messages.reset(m_history.recent(m_currentChat, HistoryLimit));
        for (const HistoryEntry &entry : m_messages.entries())
            ensurePreview(entry);
    }
    trace(QStringLiteral("gajim import: %1 of %2 messages").arg(result.imported).arg(result.found));
    return {{"imported", result.imported}, {"found", result.found}, {"error", result.error}};
}

QString Backend::previewOf(const HistoryEntry &entry) const {
    const QString text = entry.kind == "file" ? tr("File: %1").arg(entry.body) : entry.body.simplified();
    // In a room, who said it.
    return !entry.outgoing && !entry.sender.isEmpty() ? QStringLiteral("%1: %2").arg(entry.sender, text) : text;
}

void Backend::openConversation(const QString &jid) {
    m_history.setChatOpen(jid, true);
    const HistoryEntry last = m_history.lastLine(jid);
    if (last.id != 0)
        m_contacts.setActivity(jid, last.stamp, previewOf(last), last.outgoing);
    else
        m_contacts.setActivity(jid, QDateTime::currentDateTimeUtc(), {}, false);
}

void Backend::closeConversation(const QString &jid) {
    const QString bare = normalizedJid(jid);
    if (bare.isEmpty())
        return;
    m_history.setChatOpen(bare, false);
    m_contacts.setOpen(bare, false);
    if (bare == m_currentChat)
        closeChat();
}

void Backend::discoverProxy() {
    auto *disco = m_client->findExtension<QXmppDiscoveryManager>();
    if (!disco)
        return;
    const QString account = m_jid;
    disco->items(QXmppUtils::jidToDomain(m_jid)).then(this, [this, disco, account](QXmpp::Result<QList<QXmppDiscoItem>> &&result) {
        if (account != m_jid || !std::holds_alternative<QList<QXmppDiscoItem>>(result))
            return;
        for (const QXmppDiscoItem &item : std::get<QList<QXmppDiscoItem>>(result)) {
            disco->info(item.jid()).then(this, [this, account, jid = item.jid()](QXmpp::Result<QXmppDiscoInfo> &&info) {
                if (account != m_jid || !m_proxy.isEmpty() || !std::holds_alternative<QXmppDiscoInfo>(info))
                    return;
                for (const QXmppDiscoIdentity &identity : std::get<QXmppDiscoInfo>(info).identities()) {
                    if (identity.category() == "proxy" && identity.type() == "bytestreams") {
                        m_proxy = jid;
                        m_transfers->setProxy(jid);
                        trace("si: proxy " + jid);
                        return;
                    }
                }
            });
        }
    });
}

void Backend::setAlias(const QString &jid, const QString &alias) {
    const QString bare = normalizedJid(jid);
    if (bare.isEmpty())
        return;
    m_history.setAlias(bare, alias);
    m_contacts.setAlias(bare, alias);
}

// One message as a transcript line, with the text given or its own.
static QString transcriptLine(const HistoryEntry &entry, const QString &who, const QString &text) {
    const QString when = QLocale().toString(entry.stamp.toLocalTime(), QLocale::ShortFormat);
    return who.isEmpty() ? QStringLiteral("[%1] %2").arg(when, text) : QStringLiteral("[%1] %2: %3").arg(when, who, text);
}

// Who wrote a line: our account's name, our nickname in a room, the
// nickname of whoever wrote in one, or the contact's name.
QString Backend::authorOf(const HistoryEntry &entry) const {
    if (entry.outgoing)
        return entry.sender.isEmpty() ? QXmppUtils::jidToUser(m_jid) : entry.sender;
    return !entry.sender.isEmpty() ? entry.sender : m_contacts.nameOf(entry.peer);
}

QString Backend::selectedMessagesText() const {
    QStringList lines;
    const auto entries = m_messages.selectedEntries();
    for (const HistoryEntry &entry : entries) {
        const QString who = authorOf(entry);
        const QString text = entry.kind == "file" ? (entry.url.isEmpty() ? tr("File: %1").arg(entry.body) : entry.url)
                                                  : entry.body;
        lines << transcriptLine(entry, who, text);
    }
    return lines.join('\n');
}

QString Backend::transcriptText(const QVariantList &parts) const {
    const auto &entries = m_messages.entries();
    if (parts.size() == 1)
        return parts.first().toMap().value("text").toString();
    QStringList lines;
    for (const QVariant &value : parts) {
        const QVariantMap part = value.toMap();
        const int row = part.value("row").toInt();
        if (row < 0 || row >= entries.size())
            continue;
        const HistoryEntry &entry = entries.at(row);
        const QString who = authorOf(entry);
        // A partial line starts or ends wherever the drag did, spaces and all.
        QString text = part.value("text").isNull() ? entry.body : part.value("text").toString().trimmed();
        if (entry.kind == "file")
            text = QStringLiteral("[%1]").arg(tr("File: %1").arg(entry.body));
        if (!text.isEmpty())
            lines << transcriptLine(entry, who, text);
    }
    return lines.join('\n');
}

void Backend::copyTranscript(const QVariantList &parts) {
    const QString text = transcriptText(parts);
    if (!text.isEmpty())
        QGuiApplication::clipboard()->setText(text);
}

void Backend::copySelectedMessages() {
    const QString text = selectedMessagesText();
    if (!text.isEmpty())
        QGuiApplication::clipboard()->setText(text);
}

void Backend::forwardSelected(const QString &jid) {
    const QList<HistoryEntry> entries = m_messages.selectedEntries();
    if (entries.isEmpty() || jid.isEmpty())
        return;
    if (!m_client->isAuthenticated()) {
        setError(tr("You're offline, so nothing was forwarded."));
        return;
    }
    // Each goes with who wrote it and when, as the copy has it, worked
    // out before the other conversation opens.
    // The header on a line of its own, the text below it.
    QStringList headers;
    for (const HistoryEntry &original : entries)
        headers << transcriptLine(original, authorOf(original),
                                  "\n" + (original.kind == "text" ? original.body : tr("File: %1").arg(original.body)))
                       .replace(": \n", ":\n");
    m_messages.clearSelection();
    openChat(jid);
    cancelEditing();
    cancelReply();
    const bool room = m_rooms.contains(jid);
    bool missing = false;
    for (int i = 0; i < entries.size(); ++i) {
        const HistoryEntry &original = entries.at(i);
        // A text in a single message; a file after a line saying whose it is.
        m_forwarding = true;
        sendMessage(headers.at(i));
        m_forwarding = false;
        if (original.kind == "text")
            continue;
        if (!original.url.isEmpty()) {
            // Already on the server: the link is enough.
            HistoryEntry entry;
            entry.peer = jid;
            entry.outgoing = true;
            entry.kind = "file";
            entry.body = original.body;
            entry.fileSize = original.fileSize;
            entry.url = original.url;
            entry.state = "done";
            if (original.state == "done")
                entry.filePath = original.filePath;
            if (room)
                entry.sender = roomNickname(jid);
            entry.xmppId = sendFileLink(jid, QUrl(original.url));
            addEntry(entry);
        } else if (original.state == "done" && QFileInfo(original.filePath).isReadable()) {
            sendFile(original.filePath, serverUploadAvailable() || room ? "server" : "direct");
        } else {
            missing = true;
        }
    }
    if (missing)
        setError(tr("Some files weren't forwarded: they're neither on the server nor on this computer."));
}

QString Backend::targetId(const HistoryEntry &entry) const {
    if (m_rooms.contains(entry.peer) && !entry.archiveId.isEmpty())
        return entry.archiveId;
    return entry.xmppId;
}

QString Backend::ownReactionKey(const QString &peer) const {
    return m_rooms.contains(peer) ? roomNickname(peer) : m_jid;
}

void Backend::decorate(const HistoryEntry &entry) {
    if (!entry.replyId.isEmpty()) {
        const HistoryEntry original = m_history.findByAnyId(entry.peer, entry.replyId);
        QVariantMap reply{{"id", entry.replyId}};
        if (original.id != 0) {
            reply.insert("name", original.outgoing ? tr("You")
                                 : !original.sender.isEmpty() ? original.sender : m_contacts.nameOf(original.peer));
            reply.insert("text", original.kind == "file" ? tr("File: %1").arg(original.body) : original.body.simplified());
        } else {
            reply.insert("name", QString());
            reply.insert("text", tr("A message that isn't in this history"));
        }
        m_messages.setReply(entry.id, reply);
    }

    // Some clients point at the room's id, some at the message's own.
    QList<Reaction> reactions = m_history.reactions(entry.peer, targetId(entry));
    if (!entry.xmppId.isEmpty() && targetId(entry) != entry.xmppId)
        reactions += m_history.reactions(entry.peer, entry.xmppId);
    const QString own = ownReactionKey(entry.peer);
    QList<QString> order;
    QHash<QString, QStringList> who;
    for (const Reaction &reaction : reactions) {
        for (const QString &emoji : reaction.emojis) {
            if (!who.contains(emoji))
                order << emoji;
            const QString name = reaction.sender == own ? tr("You")
                               : m_rooms.contains(entry.peer) ? reaction.sender
                               : m_contacts.nameOf(reaction.sender);
            if (!who[emoji].contains(name))
                who[emoji] << name;
        }
    }
    QVariantList list;
    for (const QString &emoji : order)
        list << QVariantMap{{"emoji", emoji}, {"count", who.value(emoji).size()},
                            {"mine", who.value(emoji).contains(tr("You"))}, {"who", who.value(emoji).join(", ")}};
    m_messages.setReactions(entry.id, list);
}

void Backend::applyReaction(const QString &peer, const QString &sender, const QXmppMessageReaction &reaction) {
    QStringList emojis;
    for (const QString &emoji : reaction.emojis())
        if (!emoji.trimmed().isEmpty() && !emojis.contains(emoji))
            emojis << emoji;
    m_history.setReaction(peer, reaction.messageId(), sender, emojis);
    if (peer != m_currentChat)
        return;
    const int row = m_messages.rowOfMessage(reaction.messageId());
    if (row >= 0)
        decorate(m_messages.entries().at(row));
}

void Backend::quoteReply(QXmppMessage &message, HistoryEntry &entry) {
    if (m_replyId == 0)
        return;
    const HistoryEntry original = m_messages.entry(m_replyId);
    const QString id = targetId(original);
    if (original.id == 0 || id.isEmpty())
        return;
    const QString to = m_rooms.contains(original.peer)
                           ? original.peer + "/" + (original.outgoing ? roomNickname(original.peer) : original.sender)
                           : (original.outgoing ? m_jid : original.peer);
    message.setReply(QXmpp::Reply{to, id});

    // Clients without replies see the quote the body starts with; the
    // fallback marker (XEP-0428) lets the others leave it out.
    const QString name = original.outgoing ? QXmppUtils::jidToUser(m_jid)
                       : !original.sender.isEmpty() ? original.sender : m_contacts.nameOf(original.peer);
    QString quote;
    for (const QString &line : original.body.split('\n'))
        quote += "> " + line + "\n";
    quote = QStringLiteral("> %1:\n").arg(name) + quote;
    message.setBody(quote + entry.body);
    const auto length = uint32_t(quote.toUcs4().size());
    message.setFallbackMarkers({QXmppFallback(QStringLiteral("urn:xmpp:reply:0"),
                                              {QXmppFallback::Reference{QXmppFallback::Body, QXmppFallback::Range{0, length}}})});
    entry.replyId = id;
}

QString Backend::replyingTo() const {
    if (m_replyId == 0)
        return {};
    const HistoryEntry original = m_messages.entry(m_replyId);
    const QString name = original.outgoing ? tr("You")
                       : !original.sender.isEmpty() ? original.sender : m_contacts.nameOf(original.peer);
    const QString text = original.kind == "file" ? tr("File: %1").arg(original.body) : original.body.simplified();
    return name.isEmpty() ? text : QStringLiteral("%1: %2").arg(name, text);
}

void Backend::startReply(qint64 messageId) {
    const HistoryEntry original = m_messages.entry(messageId);
    if (original.id == 0 || targetId(original).isEmpty()) {
        setError(tr("This message can't be answered: it has no id to point at."));
        return;
    }
    cancelEditing();
    m_replyId = messageId;
    emit replyChanged();
}

void Backend::cancelReply() {
    if (m_replyId == 0)
        return;
    m_replyId = 0;
    emit replyChanged();
}

void Backend::toggleReaction(qint64 messageId, const QString &emoji) {
    react(messageId, emoji, true);
}

void Backend::addReaction(qint64 messageId, const QString &emoji) {
    react(messageId, emoji, false);
}

void Backend::react(qint64 messageId, const QString &emoji, bool toggle) {
    const HistoryEntry entry = m_messages.entry(messageId);
    const QString target = targetId(entry);
    if (entry.id == 0 || target.isEmpty())
        return;
    if (!m_client->isAuthenticated()) {
        setError(tr("You're offline, so the reaction wasn't sent."));
        return;
    }
    // A reaction always carries our whole set for the message.
    const QString own = ownReactionKey(entry.peer);
    QStringList emojis;
    for (const Reaction &reaction : m_history.reactions(entry.peer, target))
        if (reaction.sender == own)
            emojis = reaction.emojis;
    if (emojis.contains(emoji) && !toggle)
        return;
    if (!emojis.removeAll(emoji))
        emojis << emoji;

    QXmppMessageReaction reaction;
    reaction.setMessageId(target);
    reaction.setEmojis(emojis);
    QXmppMessage message({}, entry.peer);
    message.setId(QXmppUtils::generateStanzaUuid());
    message.setReaction(reaction);
    if (m_rooms.contains(entry.peer)) {
        if (auto room = m_muc->findRoom(entry.peer))
            room->sendMessage(message);
    } else {
        message.setType(QXmppMessage::Chat);
        m_client->send(std::move(message));
    }
    m_history.setReaction(entry.peer, target, own, emojis);
    decorate(entry);
}

void Backend::copyText(const QString &text) {
    QGuiApplication::clipboard()->setText(text);
}

void Backend::setIdleLevel(int level) {
    // Gajim's auto away: available becomes away, then not available, and
    // comes back on its own; a status picked by hand is left alone.
    if (level == 0) {
        if (!m_statusBeforeIdle.isEmpty()) {
            const QString previous = m_statusBeforeIdle;
            m_statusBeforeIdle.clear();
            applyStatus(previous, false);
        }
        return;
    }
    if (m_statusBeforeIdle.isEmpty()) {
        if (m_status != "online" && m_status != "chat")
            return;
        m_statusBeforeIdle = m_status;
    }
    trace(QStringLiteral("idle: level %1").arg(level));
    applyStatus(level >= 2 ? "xa" : "away", false);
}

QVariantList Backend::searchConversation(const QString &text) const {
    QVariantList ids;
    if (m_currentChat.isEmpty())
        return ids;
    const auto found = m_history.search(m_currentChat, text);
    for (qint64 id : found)
        ids << id;
    return ids;
}

int Backend::revealMessage(qint64 id) {
    const auto &entries = m_messages.entries();
    for (int row = 0; row < entries.size(); ++row)
        if (entries.at(row).id == id)
            return row;
    const HistoryEntry entry = m_history.byId(id);
    if (entry.id == 0 || entry.peer != m_currentChat)
        return -1;
    // Further back than what's shown: show everything from it on.
    m_messages.reset(m_history.since(m_currentChat, entry.stamp));
    for (const HistoryEntry &line : m_messages.entries()) {
        ensurePreview(line);
        decorate(line);
    }
    const auto &shown = m_messages.entries();
    for (int row = 0; row < shown.size(); ++row)
        if (shown.at(row).id == id)
            return row;
    return -1;
}

QVariantMap Backend::clipboardContent() const {
    const QMimeData *data = QGuiApplication::clipboard()->mimeData();
    if (!data)
        return {};
    // Files copied in a file manager come as a list of file URLs.
    if (data->hasUrls()) {
        QStringList paths;
        for (const QUrl &url : data->urls())
            if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile())
                paths << url.toLocalFile();
        if (!paths.isEmpty())
            return {{"kind", "files"}, {"paths", paths}};
    }
    if (data->hasImage())
        return {{"kind", "image"}};
    return {};
}

QString Backend::savePastedImage() {
    const QMimeData *data = QGuiApplication::clipboard()->mimeData();
    if (!data || !data->hasImage())
        return {};
    const QString directory = QDir(m_dataDirectory).filePath("accounts/" + m_jid + "/pasted");
    QDir().mkpath(directory);
    const QString name = tr("pasted-%1.png").arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
    const QString path = Files::uniquePath(directory, name);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(tr("%1 can't be written: %2").arg(path, file.errorString()));
        return {};
    }
    // The PNG as the source gave it, when it did; otherwise encoded here.
    const QByteArray png = data->data("image/png");
    const bool written = !png.isEmpty() ? file.write(png) == png.size()
                                        : qvariant_cast<QImage>(data->imageData()).save(&file, "PNG");
    if (!written || !file.commit()) {
        setError(tr("The pasted picture couldn't be saved."));
        return {};
    }
    return path;
}

void Backend::setContactGroup(const QString &jid, const QString &group) {
    const QString bare = normalizedJid(jid);
    if (!m_client->isAuthenticated()) {
        setError(tr("You're offline, so the contact can't be moved."));
        return;
    }
    const QString name = group.trimmed();
    const QSet<QString> groups = name.isEmpty() ? QSet<QString>() : QSet<QString>{name};
    m_roster->updateRosterGroups(bare, groups).then(this, [this, bare](QXmppRosterManager::Result &&result) {
        if (auto *error = std::get_if<QXmppError>(&result))
            setError(tr("%1 couldn't be moved: %2").arg(m_contacts.nameOf(bare), error->description));
    });
    // The roster push from the server confirms it; show it right away.
    m_contacts.setGroups(bare, QStringList(groups.begin(), groups.end()));
}

void Backend::setShowAllContacts(bool show) {
    if (show == m_contacts.browsing())
        return;
    m_contacts.setBrowsing(show);
    m_settings.setValue("ui/showAllContacts", show);
    emit showAllContactsChanged();
}

void Backend::openFolder(const QString &path) {
    if (!QFileInfo::exists(path)) {
        setError(tr("%1 isn't there anymore.").arg(path));
        return;
    }
    // Most file managers show the file selected; the folder alone if none answers.
    QDBusMessage call = QDBusMessage::createMethodCall("org.freedesktop.FileManager1", "/org/freedesktop/FileManager1",
                                                       "org.freedesktop.FileManager1", "ShowItems");
    call << QStringList{QUrl::fromLocalFile(path).toString()} << QString();
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 3000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [path](QDBusPendingCallWatcher *watcher) {
        if (watcher->isError())
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
        watcher->deleteLater();
    });
}

void Backend::saveFileAs(const QString &path, const QString &suggestedName) {
    if (!m_filePicker)
        return;
    if (!QFileInfo::exists(path)) {
        setError(tr("%1 isn't there anymore.").arg(path));
        return;
    }
    m_saveSource = path;
    m_filePicker->saveFile(tr("Save %1").arg(suggestedName), suggestedName);
}
