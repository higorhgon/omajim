// Instance no Windows: sem D-Bus, a primeira cópia escuta num pipe nomeado
// (QLocalServer) e a segunda só pede por ele que a janela apareça.
#include "instance.h"

#include <QLocalServer>
#include <QLocalSocket>

// Um por usuário do Windows: cada sessão tem a sua cópia.
static QString serverName() {
    return QStringLiteral("io.github.higorhgon.Omajim.Instance-") + qEnvironmentVariable("USERNAME");
}

bool Instance::claim() {
    QLocalSocket other;
    other.connectToServer(serverName());
    if (other.waitForConnected(500)) {
        other.write("show\n");
        other.waitForBytesWritten(1000);
        other.disconnectFromServer();
        return false;
    }

    auto *server = new QLocalServer(this);
    server->setSocketOptions(QLocalServer::UserAccessOption);
    // Sem o pipe, cada cópia segue sozinha, como no Linux sem barramento.
    if (!server->listen(serverName()))
        return true;
    connect(server, &QLocalServer::newConnection, this, [this, server] {
        while (QLocalSocket *socket = server->nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            Show();
        }
    });
    return true;
}
