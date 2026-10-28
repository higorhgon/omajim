#pragma once

#include <QList>
#include <QObject>
#include <QSettings>
#include <QVariantList>

#include "backend.h"

class FilePicker;
class Notifier;
class SecretStore;

// Every account at once, each with its own connection, history and
// contacts (a Backend each); the window shows one, the current, and moves
// between them. Turned off, an account stays listed, offline. A new account
// signs in on a Backend of its own, kept once it connects.
class Accounts : public QObject {
    Q_OBJECT
    Q_PROPERTY(Backend *current READ current NOTIFY currentChanged)
    // {jid, enabled, signedIn, savePassword, host, unread, connection,
    // current} per account, in order.
    Q_PROPERTY(QVariantList list READ list NOTIFY listChanged)
    Q_PROPERTY(bool adding READ adding NOTIFY addingChanged)
    Q_PROPERTY(int totalUnread READ totalUnread NOTIFY listChanged)

public:
    Accounts(const QString &configDirectory, const QString &dataDirectory, FilePicker *filePicker,
             SecretStore *secrets, Notifier *notifier = nullptr, QObject *parent = nullptr);
    ~Accounts() override;

    // Brings up the accounts kept; on the first run, what Gajim has.
    void start(const QStringList &gajimSettings = {}, const QStringList &gajimHistories = {});

    Backend *current() const { return m_current; }
    QList<Backend *> backends() const { return m_backends; }
    QVariantList list() const;
    // Adding another account, with one to go back to.
    bool adding() const { return m_adding && m_beforeAdding; }
    int totalUnread() const;

    Q_INVOKABLE void select(const QString &jid);
    // Ctrl+1, Ctrl+2…: the account in that place.
    Q_INVOKABLE void selectIndex(int index);
    Q_INVOKABLE void setEnabled(const QString &jid, bool enabled);
    Q_INVOKABLE void signOut(const QString &jid);
    // An account's settings: its server, and whether the keyring keeps its
    // password.
    Q_INVOKABLE void configure(const QString &jid, const QString &host, bool savePassword);
    Q_INVOKABLE void addAccount();
    Q_INVOKABLE void cancelAdding();
    // Our status, on every account that's on.
    Q_INVOKABLE void setStatus(const QString &status);
    void setIdleLevel(int level);

signals:
    void currentChanged();
    void listChanged();
    void addingChanged();
    void raiseRequested();
    // The automatic away times changed, on whichever account.
    void idleTimesChanged();

private:
    Backend *create(const QString &jid);
    Backend *find(const QString &jid) const;
    void setCurrent(Backend *backend);
    void finishAdding();

    QSettings m_settings;
    QString m_configDirectory;
    QString m_dataDirectory;
    FilePicker *m_filePicker;
    SecretStore *m_secrets;
    Notifier *m_notifier;
    QList<Backend *> m_backends;  // the accounts kept, in order
    Backend *m_current = nullptr;
    Backend *m_adding = nullptr;  // signing in, not kept yet
    Backend *m_beforeAdding = nullptr;
};
