#pragma once

#include <QList>
#include <QMenu>
#include <QObject>
#include <QSystemTrayIcon>

class Backend;

// A bandeja do Windows e a barra de menus do macOS usam o mesmo menu de status.
class WidgetTray : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)

public:
    explicit WidgetTray(Backend *backend, QObject *parent = nullptr);

    bool available() const { return m_icon.isVisible(); }
    QSystemTrayIcon *icon() { return &m_icon; }

    void retranslate() { refresh(); }
    void setBackend(Backend *backend);

signals:
    void availableChanged();
    void toggleWindowRequested();
    void quitRequested();
    void statusRequested(const QString &status);

private:
    void refresh();

    Backend *m_backend = nullptr;
    QList<QMetaObject::Connection> m_watching;
    QSystemTrayIcon m_icon;
    QMenu m_menu;
    QMenu m_statusMenu;
    QAction *m_toggle = nullptr;
    QAction *m_quit = nullptr;
    QList<QAction *> m_statusActions;
};
