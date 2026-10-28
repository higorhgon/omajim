#include "secretstore.h"

#include <QProcess>
#include <QStandardPaths>

SecretStore::SecretStore(QObject *parent, const QString &secretTool)
    : QObject(parent),
      m_secretTool(secretTool.isEmpty() ? QStandardPaths::findExecutable("secret-tool") : secretTool) {}

void SecretStore::lookup(const QString &account, LookupDone done) {
    run({"lookup", "service", "omajim", "account", account}, {},
        [done](int code, const QByteArray &out, const QString &error) {
            // secret-tool exits 1 with no output when nothing is stored.
            if (!error.isEmpty())
                done({}, error);
            else
                done(code == 0 ? QString::fromUtf8(out) : QString(), {});
        });
}

void SecretStore::lookupGajim(const QString &account, LookupDone done) {
    run({"lookup", "service", "gajim", "username", account}, {},
        [done](int code, const QByteArray &out, const QString &error) {
            done(error.isEmpty() && code == 0 ? QString::fromUtf8(out) : QString(), {});
        });
}

void SecretStore::store(const QString &account, const QString &password, Done done) {
    run({"store", "--label", tr("Omajim password for %1").arg(account), "service", "omajim", "account", account},
        password.toUtf8(), [done](int code, const QByteArray &, const QString &error) {
            done(!error.isEmpty() ? error : code == 0 ? QString() : tr("The keyring refused to save the password."));
        });
}

void SecretStore::clear(const QString &account, Done done) {
    run({"clear", "service", "omajim", "account", account}, {},
        [done](int, const QByteArray &, const QString &error) { done(error); });
}

void SecretStore::run(const QStringList &arguments, const QByteArray &input,
                      std::function<void(int, const QByteArray &, const QString &)> done) {
    if (m_secretTool.isEmpty()) {
        done(-1, {}, tr("secret-tool is missing, so the password can't be kept in the keyring. "
                        "Install the libsecret package."));
        return;
    }

    auto *process = new QProcess(this);
    connect(process, &QProcess::errorOccurred, this, [process, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        done(-1, {}, tr("Couldn't start secret-tool: %1").arg(process->errorString()));
        process->deleteLater();
    });
    connect(process, &QProcess::finished, this, [process, done](int code, QProcess::ExitStatus status) {
        const QString error = status == QProcess::NormalExit ? QString()
                                                             : tr("secret-tool stopped unexpectedly.");
        done(code, process->readAllStandardOutput(), error);
        process->deleteLater();
    });
    process->start(m_secretTool, arguments);
    if (!input.isEmpty())
        process->write(input);
    process->closeWriteChannel();
}
