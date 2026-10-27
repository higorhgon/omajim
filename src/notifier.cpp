#include "notifier.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QHash>

static const char *Service = "org.freedesktop.Notifications";
static const char *Path = "/org/freedesktop/Notifications";

Notifier::Notifier(QObject *parent) : QObject(parent) {
    QDBusConnection::sessionBus().connect(Service, Path, Service, "ActionInvoked", this,
                                          SLOT(onActionInvoked(uint, QString)));
}

void Notifier::notify(const QString &key, const QString &title, const QString &body) {
    // A new message from the same person replaces their last notification
    // instead of stacking up.
    QDBusMessage call = QDBusMessage::createMethodCall(Service, Path, Service, "Notify");
    call << QStringLiteral("Omajim") << m_ids.value(key, 0u) << QStringLiteral("omajim") << title << body
         << QStringList{"default", tr("Open")} << QVariantMap{{"desktop-entry", "omajim"}} << -1;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, key](QDBusPendingCallWatcher *watcher) {
        QDBusPendingReply<uint> reply = *watcher;
        watcher->deleteLater();
        if (reply.isError())
            return;
        m_ids.insert(key, reply.value());
        m_keys.insert(reply.value(), key);
    });
}

void Notifier::onActionInvoked(uint id, const QString &) {
    if (m_keys.contains(id))
        emit activated(m_keys.value(id));
}
