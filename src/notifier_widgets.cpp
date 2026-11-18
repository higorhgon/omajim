// Notificações do sistema pelo ícone da bandeja no Windows e no macOS.
#include "notifier.h"

#include <QSystemTrayIcon>

Notifier::Notifier(QObject *parent) : QObject(parent) {}

void Notifier::setTrayIcon(QSystemTrayIcon *icon) {
    m_trayIcon = icon;
    connect(icon, &QSystemTrayIcon::messageClicked, this, [this] {
        if (!m_shownKey.isEmpty())
            emit activated(m_shownKey);
    });
}

void Notifier::notify(const QString &key, const QString &title, const QString &body) {
    if (!m_trayIcon || !m_trayIcon->isVisible())
        return;
    // A bandeja só informa que houve um clique, sem identificar a mensagem.
    m_shownKey = key;
    m_trayIcon->showMessage(title, body, m_trayIcon->icon(), 10000);
}
