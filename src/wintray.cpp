#include "wintray.h"

#include <QActionGroup>
#include <QCoreApplication>

#include "backend.h"
#include "contactmodel.h"

// Na ordem do menu de status da janela, como no tray.cpp.
static const QStringList Statuses{"online", "chat", "away", "xa", "dnd", "offline"};

// Os mesmos textos do menu do Linux, que já têm tradução.
static QString statusLabel(const QString &status) {
    if (status == "online") return QCoreApplication::translate("TrayMenu", "Available");
    if (status == "chat") return QCoreApplication::translate("TrayMenu", "Free to chat");
    if (status == "away") return QCoreApplication::translate("TrayMenu", "Away");
    if (status == "xa") return QCoreApplication::translate("TrayMenu", "Not available");
    if (status == "dnd") return QCoreApplication::translate("TrayMenu", "Busy");
    return QCoreApplication::translate("TrayMenu", "Offline");
}

WinTray::WinTray(Backend *backend, QObject *parent) : QObject(parent) {
    m_toggle = m_menu.addAction(QString());
    connect(m_toggle, &QAction::triggered, this, &WinTray::toggleWindowRequested);
    m_menu.addMenu(&m_statusMenu);
    auto *group = new QActionGroup(this);
    for (const QString &value : Statuses) {
        QAction *action = m_statusMenu.addAction(QString());
        action->setCheckable(true);
        group->addAction(action);
        connect(action, &QAction::triggered, this, [this, value] { emit statusRequested(value); });
        m_statusActions << action;
    }
    m_menu.addSeparator();
    m_quit = m_menu.addAction(QString());
    connect(m_quit, &QAction::triggered, this, &WinTray::quitRequested);

    m_icon.setIcon(QIcon(QStringLiteral(":/windows/omajim.svg")));
    m_icon.setContextMenu(&m_menu);
    // Um clique (ou dois) no ícone mostra ou esconde a janela; o direito abre
    // o menu.
    connect(&m_icon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            emit toggleWindowRequested();
    });
    setBackend(backend);
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_icon.show();
        emit availableChanged();
    }
}

void WinTray::setBackend(Backend *backend) {
    for (const auto &connection : std::as_const(m_watching))
        disconnect(connection);
    m_watching.clear();
    m_backend = backend;
    m_watching << connect(backend, &Backend::connectionStateChanged, this, &WinTray::refresh)
               << connect(backend, &Backend::errorTextChanged, this, &WinTray::refresh)
               << connect(backend->contacts(), &ContactModel::totalUnreadChanged, this, &WinTray::refresh)
               << connect(backend, &Backend::statusChanged, this, &WinTray::refresh)
               << connect(backend, &Backend::signedInChanged, this, &WinTray::refresh);
    refresh();
}

// Textos (o idioma pode ter mudado), o status marcado e a dica do ícone, que
// faz o papel dos ícones de status do Linux.
void WinTray::refresh() {
    QString tip = QStringLiteral("Omajim");
    if (m_backend->signedIn())
        tip += QStringLiteral(" — ") + statusLabel(m_backend->status());
    const int unread = m_backend->contacts()->totalUnread();
    if (unread > 0)
        tip += QStringLiteral(" (%1)").arg(unread);
    m_icon.setToolTip(tip);

    m_toggle->setText(QCoreApplication::translate("TrayMenu", "Show or hide the window"));
    m_statusMenu.setTitle(QCoreApplication::translate("TrayMenu", "Status"));
    for (int i = 0; i < Statuses.size(); ++i) {
        QAction *action = m_statusActions.at(i);
        action->setText(statusLabel(Statuses.at(i)));
        action->setChecked(m_backend->status() == Statuses.at(i));
        // Escolher um status pede uma conta com que ficar online.
        action->setEnabled(m_backend->signedIn());
    }
    m_quit->setText(QCoreApplication::translate("TrayMenu", "Quit"));
}
