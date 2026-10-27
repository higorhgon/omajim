#include "backend.h"

#include <QCryptographicHash>
#include <QDesktopServices>
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
#include <QXmppMessage.h>
#include <QXmppPresence.h>
#include <QXmppRosterManager.h>
#include <QXmppSocks.h>
#include <QXmppTask.h>
#include <QXmppTransferManager.h>
#include <QXmppUtils.h>

#include "filepicker.h"
#include "files.h"
#include "notifier.h"
#include "secretstore.h"

static const int HistoryLimit = 300;

// Contacts connect to this port to fetch a file sent directly. QXmpp picks a
// random one, which no firewall rule can allow, so use a fixed port, the one
// the Python Omajim used, and an existing ufw rule keeps working.
static const quint16 DirectTransferPort = 28011;

Backend::Backend(const QString &configDirectory, const QString &dataDirectory, FilePicker *filePicker,
                 SecretStore *secrets, Notifier *notifier, QObject *parent)
    : QObject(parent),
      m_settings(QDir(configDirectory).filePath("omajim.conf"), QSettings::IniFormat),
      m_dataDirectory(dataDirectory),
      m_filePicker(filePicker),
      m_secrets(secrets),
      m_notifier(notifier),
      m_client(std::make_unique<QXmppClient>()) {
    m_jid = m_settings.value("account/jid").toString();
    m_status = m_settings.value("account/status", "online").toString();

    m_roster = m_client->findExtension<QXmppRosterManager>();
    m_client->addNewExtension<QXmppCarbonManagerV2>();
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
        if (state == QXmppClient::DisconnectedState) {
            m_contacts.setAllOffline();
            emit currentChatChanged();
        }
        if (state == QXmppClient::ConnectedState)
            dismissError();
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
    connect(m_client.get(), &QXmppClient::messageReceived, this, &Backend::handleMessage);

    connect(m_roster, &QXmppRosterManager::rosterReceived, this, &Backend::syncRoster);
    connect(m_roster, &QXmppRosterManager::itemAdded, this, [this](const QString &jid) {
        m_contacts.upsert(jid, m_roster->getRosterEntry(jid).name());
    });
    connect(m_roster, &QXmppRosterManager::itemChanged, this, [this](const QString &jid) {
        m_contacts.upsert(jid, m_roster->getRosterEntry(jid).name());
    });
    connect(m_roster, &QXmppRosterManager::itemRemoved, this, [this](const QString &jid) {
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
                sendFile(path);
        });
        connect(m_filePicker, &FilePicker::failed, this, &Backend::setError);
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
    m_secrets->lookup(m_jid, [this](const QString &password, const QString &error) {
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
    return ContactModel::displayName(m_currentChat, m_contacts.contact(m_currentChat).name);
}

QString Backend::currentChatPresence() const {
    return m_contacts.contains(m_currentChat) ? m_contacts.contact(m_currentChat).presence : QString("offline");
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
    m_requests.clear();
    emit subscriptionRequestsChanged();
    closeChat();
    setSignedIn(false);
}

void Backend::setStatus(const QString &status) {
    static const QStringList known{"online", "chat", "away", "xa", "dnd", "offline"};
    if (!known.contains(status))
        return;
    if (status != m_status) {
        m_status = status;
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
    if (m_jid.isEmpty() || m_password.isEmpty() || m_status == "offline")
        return;
    QXmppConfiguration configuration;
    configuration.setJid(m_jid);
    configuration.setPassword(m_password);
    configuration.setResource("omajim");
    configuration.setAutoReconnectionEnabled(true);
    configuration.setCaCertificates(trustedCertificates());
    const QString server = host();
    if (!server.isEmpty()) {
        // "host" or "host:port", for servers without SRV records.
        const qsizetype colon = server.lastIndexOf(':');
        bool numeric = false;
        const int port = colon > 0 ? server.mid(colon + 1).toInt(&numeric) : 0;
        configuration.setHost(numeric ? server.left(colon) : server);
        if (numeric)
            configuration.setPort(port);
    }
    m_client->connectToServer(configuration, presenceFor(m_status));
}

void Backend::openHistory() {
    const QString path = QDir(m_dataDirectory).filePath("accounts/" + m_jid + "/history.sqlite");
    if (!m_history.open(path)) {
        setError(tr("The message history couldn't be opened (%1): %2").arg(path, m_history.errorString()));
        return;
    }
    m_history.failInterruptedTransfers();
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
    const QStringList jids = m_roster->getRosterBareJids();
    for (const QString &jid : jids)
        m_contacts.upsert(jid, m_roster->getRosterEntry(jid).name());
    for (const QString &jid : jids)
        refreshPresence(jid);
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

qint64 Backend::addEntry(HistoryEntry &entry) {
    if (!m_contacts.contains(entry.peer))
        m_contacts.upsert(entry.peer, {});
    if (!m_history.add(entry)) {
        setError(tr("The message couldn't be saved to the history: %1").arg(m_history.errorString()));
        return 0;
    }
    if (entry.peer == m_currentChat)
        m_messages.append(entry);

    if (!entry.outgoing && (entry.peer != m_currentChat || !m_windowActive)) {
        m_contacts.addUnread(entry.peer);
        if (m_notifier && m_status != "dnd") {
            const QString name = ContactModel::displayName(entry.peer, m_contacts.contact(entry.peer).name);
            const QString body = entry.kind == "file" ? tr("Wants to send you %1").arg(entry.body)
                                                      : entry.body.left(200);
            m_notifier->notify(entry.peer, name, body);
        }
    }
    return entry.id;
}

void Backend::handleMessage(const QXmppMessage &message) {
    if (message.type() == QXmppMessage::Error) {
        setError(tr("A message to %1 didn't arrive.").arg(QXmppUtils::jidToBareJid(message.from())));
        return;
    }
    if (message.type() == QXmppMessage::GroupChat || message.body().isEmpty())
        return;

    HistoryEntry entry;
    // A carbon of a message sent from another of our devices comes "from"
    // us; it belongs to the conversation with whoever it went to.
    const QString from = QXmppUtils::jidToBareJid(message.from()).toLower();
    entry.outgoing = from == m_jid;
    entry.peer = entry.outgoing ? QXmppUtils::jidToBareJid(message.to()).toLower() : from;
    if (entry.peer.isEmpty() || entry.peer == m_jid)
        return;
    entry.body = message.body();
    entry.xmppId = message.id();
    entry.stamp = message.stamp().isValid() ? message.stamp().toUTC() : QDateTime::currentDateTimeUtc();
    if (m_history.contains(entry.peer, entry.xmppId))
        return;
    addEntry(entry);
}

void Backend::openChat(const QString &jid) {
    const QString bare = normalizedJid(jid);
    if (bare.isEmpty())
        return;
    m_currentChat = bare;
    m_messages.reset(m_history.recent(bare, HistoryLimit));
    if (m_windowActive)
        m_contacts.clearUnread(bare);
    emit currentChatChanged();
}

void Backend::closeChat() {
    if (m_currentChat.isEmpty())
        return;
    m_currentChat.clear();
    m_messages.reset({});
    emit currentChatChanged();
}

bool Backend::sendMessage(const QString &text) {
    const QString body = text.trimmed();
    if (body.isEmpty() || m_currentChat.isEmpty())
        return false;
    if (!m_client->isAuthenticated()) {
        setError(tr("You're offline, so the message wasn't sent."));
        return false;
    }

    QXmppMessage message({}, m_currentChat, body);
    message.setType(QXmppMessage::Chat);
    HistoryEntry entry;
    entry.peer = m_currentChat;
    entry.outgoing = true;
    entry.body = body;
    entry.xmppId = message.id();
    const QString peer = m_currentChat;
    m_client->send(std::move(message)).then(this, [this, peer](QXmpp::SendResult &&result) {
        if (std::holds_alternative<QXmppError>(result))
            setError(tr("The message to %1 wasn't sent: %2").arg(peer, std::get<QXmppError>(result).description));
    });
    addEntry(entry);
    return true;
}

void Backend::chooseFileToSend() {
    if (m_currentChat.isEmpty() || !m_filePicker)
        return;
    m_filePicker->openFile(tr("Send a file to %1").arg(currentChatName()));
}

void Backend::sendFile(const QString &path) {
    if (m_currentChat.isEmpty())
        return;
    // A file dropped on the window arrives as a file:// URL.
    const QFileInfo info(path.startsWith("file:") ? QUrl(path).toLocalFile() : path);
    if (!info.isFile() || !info.isReadable()) {
        setError(tr("%1 can't be read.").arg(path));
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
    QXmppTransferJob *job = m_transfers->sendFile(m_currentChat + "/" + resource, path);
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
    m_jobs.insert(entry.id, {job, entry.peer, entry.filePath, {}, false});
    watchTransfer(entry.id, job);
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
    connect(job, &QXmppTransferJob::stateChanged, this, [this, id](QXmppTransferJob::State state) {
        if (state == QXmppTransferJob::TransferState)
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
                             .arg(ContactModel::displayName(it->peer, m_contacts.contact(it->peer).name)));
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
