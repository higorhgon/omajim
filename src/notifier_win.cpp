// Notifier no Windows: as notificações saem pelo ícone da bandeja
// (QSystemTrayIcon::showMessage), que o Windows mostra como notificação do
// sistema. Clicar nela traz a conversa, como no Linux.
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
    // O Windows só diz que a última foi clicada, então é dela a chave.
    m_shownKey = key;
    m_trayIcon->showMessage(title, body, m_trayIcon->icon(), 10000);
}

void Notifier::onActionInvoked(uint, const QString &) {}
