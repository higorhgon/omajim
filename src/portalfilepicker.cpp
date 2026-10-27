#include "portalfilepicker.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusObjectPath>
#include <QUrl>
#include <QUuid>

static const char *Service = "org.freedesktop.portal.Desktop";
static const char *Path = "/org/freedesktop/portal/desktop";

void PortalFilePicker::openFile(const QString &title) {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        emit failed(tr("There's no session D-Bus, so the file chooser can't open."));
        return;
    }

    // Subscribe to the request's Response before calling, at the path the
    // portal will use, so a fast answer can't arrive before we listen.
    const QString token = "omajim" + QUuid::createUuid().toString(QUuid::Id128);
    QString sender = bus.baseService().mid(1);
    sender.replace('.', '_');
    if (!m_requestPath.isEmpty())
        bus.disconnect(Service, m_requestPath, "org.freedesktop.portal.Request", "Response", this,
                       SLOT(onResponse(uint, QVariantMap)));
    m_requestPath = QStringLiteral("%1/request/%2/%3").arg(Path, sender, token);
    bus.connect(Service, m_requestPath, "org.freedesktop.portal.Request", "Response", this,
                SLOT(onResponse(uint, QVariantMap)));

    QDBusMessage call = QDBusMessage::createMethodCall(Service, Path, "org.freedesktop.portal.FileChooser", "OpenFile");
    call << QString() << title << QVariantMap{{"handle_token", token}, {"multiple", false}};
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        watcher->deleteLater();
        if (reply.isError())
            emit failed(tr("The file chooser didn't open (xdg-desktop-portal): %1").arg(reply.error().message()));
    });
}

void PortalFilePicker::onResponse(uint response, const QVariantMap &results) {
    // 0 is a pick, 1 the user cancelling, anything else a portal error.
    if (response == 1) {
        emit picked({});
        return;
    }
    const QStringList uris = results.value("uris").toStringList();
    if (response != 0 || uris.isEmpty()) {
        emit failed(tr("The file chooser couldn't hand over the file."));
        return;
    }
    emit picked(QUrl(uris.first()).toLocalFile());
}
