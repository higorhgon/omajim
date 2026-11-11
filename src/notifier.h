#pragma once

#include <QHash>
#include <QObject>

#ifdef Q_OS_WIN
class QSystemTrayIcon;
#endif

// Desktop notifications over org.freedesktop.Notifications, which Omarchy's
// notification daemon serves. Clicking one brings the conversation up. On
// Windows they come from the tray icon instead (notifier_win.cpp).
class Notifier : public QObject {
    Q_OBJECT

public:
    explicit Notifier(QObject *parent = nullptr);

    void notify(const QString &key, const QString &title, const QString &body);
#ifdef Q_OS_WIN
    void setTrayIcon(QSystemTrayIcon *icon);
#endif

signals:
    void activated(const QString &key);

private slots:
    void onActionInvoked(uint id, const QString &action);

private:
    QHash<uint, QString> m_keys;
    QHash<QString, uint> m_ids;
#ifdef Q_OS_WIN
    QSystemTrayIcon *m_trayIcon = nullptr;
    QString m_shownKey;
#endif
};
