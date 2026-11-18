// A primeira cópia escuta num socket local e a segunda pede que a janela apareça.
#include "instance.h"

#include <QLocalServer>
#include <QLocalSocket>

#ifdef Q_OS_MACOS
#include <QDir>
#include <QLockFile>
#include <unistd.h>
#endif

static QString serverName() {
#ifdef Q_OS_MACOS
    return QDir::temp().filePath(QStringLiteral("omajim-%1.sock").arg(getuid()));
#else
    return QStringLiteral("io.github.higorhgon.Omajim.Instance-") + qEnvironmentVariable("USERNAME");
#endif
}

bool Instance::claim() {
    const QString name = serverName();
#ifdef Q_OS_MACOS
    // Serializa a abertura e a limpeza do socket deixado por um encerramento abrupto.
    QLockFile startup(name + ".lock");
    if (!startup.tryLock(2000))
        return false;
#endif
    QLocalSocket other;
    other.connectToServer(name);
    if (other.waitForConnected(500)) {
        other.write("show\n");
        other.waitForBytesWritten(1000);
        other.disconnectFromServer();
        return false;
    }

#ifdef Q_OS_MACOS
    if (other.error() == QLocalSocket::ConnectionRefusedError)
        QLocalServer::removeServer(name);
#endif
    auto *server = new QLocalServer(this);
    server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!server->listen(name))
        return true;
    connect(server, &QLocalServer::newConnection, this, [this, server] {
        while (QLocalSocket *socket = server->nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            Show();
        }
    });
    return true;
}
