#pragma once

#include <QObject>
#include <functional>

// Keeps the account password in the desktop keyring (Secret Service) through
// secret-tool, never in a config file. The keyring may ask the user to
// unlock it, so every call is asynchronous.
class SecretStore : public QObject {
    Q_OBJECT

public:
    // Called with the password (empty when none is stored) or an error.
    using LookupDone = std::function<void(const QString &password, const QString &error)>;
    using Done = std::function<void(const QString &error)>;

    explicit SecretStore(QObject *parent = nullptr, const QString &secretTool = {});

    virtual void lookup(const QString &account, LookupDone done);
    virtual void store(const QString &account, const QString &password, Done done);
    virtual void clear(const QString &account, Done done);

private:
    void run(const QStringList &arguments, const QByteArray &input,
             std::function<void(int exitCode, const QByteArray &out, const QString &error)> done);

    QString m_secretTool;
};
