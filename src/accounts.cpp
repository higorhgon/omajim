#include "accounts.h"

#include <QDir>

#include "backend.h"
#include "contactmodel.h"

Accounts::Accounts(const QString &configDirectory, const QString &dataDirectory, FilePicker *filePicker,
                   SecretStore *secrets, Notifier *notifier, QObject *parent)
    : QObject(parent),
      m_settings(QDir(configDirectory).filePath("omajim.conf"), QSettings::IniFormat),
      m_configDirectory(configDirectory),
      m_dataDirectory(dataDirectory),
      m_filePicker(filePicker),
      m_secrets(secrets),
      m_notifier(notifier) {}

Accounts::~Accounts() {
    // Before the window's engine lets go of them: each leaves cleanly.
    qDeleteAll(m_backends);
    delete m_adding;
}

void Accounts::start(const QStringList &gajimSettings, const QStringList &gajimHistories) {
    // Before several accounts there was one, with its server under account/.
    const QString single = m_settings.value("account/jid").toString();
    if (!single.isEmpty() && !m_settings.contains("accounts/list")) {
        m_settings.setValue("accounts/list", QStringList{single});
        if (m_settings.contains("account/host"))
            m_settings.setValue("hosts/" + single, m_settings.value("account/host"));
        m_settings.remove("account/host");
    }

    QStringList jids = m_settings.value("accounts/list").toStringList();
    if (jids.isEmpty()) {
        // The first run: Gajim's accounts if there are any, else signing in.
        Backend *first = create({});
        first->setGajimSources(gajimSettings, gajimHistories);
        first->start();
        if (first->jid().isEmpty()) {
            m_adding = first;
            setCurrent(first);
            return;
        }
        m_backends << first;
        jids = m_settings.value("accounts/list").toStringList();
    }
    for (const QString &jid : std::as_const(jids)) {
        if (find(jid))
            continue;
        Backend *backend = create(jid);
        m_backends << backend;
        backend->start();
    }
    const QString chosen = m_settings.value("account/jid").toString();
    setCurrent(find(chosen) ? find(chosen) : m_backends.value(0));
    emit listChanged();
}

Backend *Accounts::create(const QString &jid) {
    auto *backend = new Backend(m_configDirectory, m_dataDirectory, m_filePicker, m_secrets, m_notifier, this);
    backend->setAccount(jid);
    auto changed = [this] { emit listChanged(); };
    connect(backend, &Backend::signedInChanged, this, changed);
    connect(backend, &Backend::enabledChanged, this, changed);
    connect(backend, &Backend::savePasswordChanged, this, changed);
    connect(backend->contacts(), &ContactModel::totalUnreadChanged, this, changed);
    connect(backend, &Backend::connectionStateChanged, this, [this, backend] {
        if (backend == m_adding && backend->connectionState() == "online")
            finishAdding();
        emit listChanged();
    });
    connect(backend, &Backend::idleTimesChanged, this, &Accounts::idleTimesChanged);
    // A notification of another account brings that one up.
    connect(backend, &Backend::raiseRequested, this, [this, backend] {
        setCurrent(backend);
        emit raiseRequested();
    });
    return backend;
}

Backend *Accounts::find(const QString &jid) const {
    for (Backend *backend : m_backends)
        if (!jid.isEmpty() && backend->jid() == jid)
            return backend;
    return nullptr;
}

void Accounts::setCurrent(Backend *backend) {
    if (!backend || backend == m_current)
        return;
    m_current = backend;
    if (!backend->jid().isEmpty() && backend != m_adding)
        m_settings.setValue("account/jid", backend->jid());
    emit currentChanged();
    emit listChanged();
}

QVariantList Accounts::list() const {
    QVariantList entries;
    for (Backend *backend : m_backends)
        entries << QVariantMap{{"jid", backend->jid()},
                               {"enabled", backend->enabled()},
                               {"signedIn", backend->signedIn()},
                               {"savePassword", backend->savePassword()},
                               {"host", backend->host()},
                               {"unread", backend->contacts()->totalUnread()},
                               {"connection", backend->connectionState()},
                               {"current", backend == m_current}};
    return entries;
}

int Accounts::totalUnread() const {
    int total = 0;
    for (Backend *backend : m_backends)
        total += backend->contacts()->totalUnread();
    return total;
}

void Accounts::select(const QString &jid) {
    if (m_adding && m_beforeAdding)
        cancelAdding();
    setCurrent(find(jid));
}

void Accounts::selectIndex(int index) {
    if (index >= 0 && index < m_backends.size())
        select(m_backends.at(index)->jid());
}

void Accounts::setEnabled(const QString &jid, bool enabled) {
    Backend *backend = find(jid);
    if (!backend)
        return;
    // One account on, at least.
    if (!enabled && std::count_if(m_backends.cbegin(), m_backends.cend(), [](Backend *b) { return b->enabled(); }) <= 1)
        return;
    backend->setEnabled(enabled);
    // Back on without its password: shown, so the window asks for it.
    if (enabled && !backend->signedIn())
        select(jid);
}

void Accounts::configure(const QString &jid, const QString &host, bool savePassword) {
    if (Backend *backend = find(jid)) {
        backend->setHost(host);
        backend->setSavePassword(savePassword);
    }
}

void Accounts::signOut(const QString &jid) {
    Backend *backend = find(jid);
    if (!backend)
        return;
    m_backends.removeAll(backend);
    if (backend == m_current) {
        if (!m_backends.isEmpty()) {
            setCurrent(m_backends.first());
        } else {
            // None left: signing in again.
            m_adding = create({});
            m_beforeAdding = nullptr;
            setCurrent(m_adding);
            emit addingChanged();
        }
    }
    backend->signOut();
    backend->deleteLater();
    emit listChanged();
}

void Accounts::addAccount() {
    if (m_adding)
        return;
    m_beforeAdding = m_current;
    m_adding = create({});
    setCurrent(m_adding);
    emit addingChanged();
}

void Accounts::cancelAdding() {
    if (!m_adding || !m_beforeAdding)
        return;
    Backend *adding = m_adding;
    m_adding = nullptr;
    setCurrent(m_beforeAdding);
    m_beforeAdding = nullptr;
    adding->deleteLater();
    emit addingChanged();
}

void Accounts::finishAdding() {
    Backend *added = m_adding;
    m_adding = nullptr;
    m_beforeAdding = nullptr;
    // Signed in again to one already here: that one stays.
    if (Backend *existing = find(added->jid())) {
        setCurrent(existing);
        added->deleteLater();
    } else {
        m_backends << added;
        QStringList jids;
        for (Backend *backend : std::as_const(m_backends))
            jids << backend->jid();
        m_settings.setValue("accounts/list", jids);
        m_settings.setValue("account/jid", added->jid());
    }
    emit addingChanged();
    emit listChanged();
}

void Accounts::setStatus(const QString &status) {
    for (Backend *backend : std::as_const(m_backends))
        backend->setStatus(status);
    if (m_adding)
        m_adding->setStatus(status);
}

void Accounts::setIdleLevel(int level) {
    for (Backend *backend : std::as_const(m_backends))
        backend->setIdleLevel(level);
}
