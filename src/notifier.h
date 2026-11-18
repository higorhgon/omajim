#pragma once

#include <QHash>
#include <QObject>

#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
class QSystemTrayIcon;
#endif

// Desktop notifications through D-Bus on Linux and through the tray icon on
// Windows and macOS. Clicking one brings the conversation up.
class Notifier : public QObject {
    Q_OBJECT

public:
    explicit Notifier(QObject *parent = nullptr);

    void notify(const QString &key, const QString &title, const QString &body);
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    void setTrayIcon(QSystemTrayIcon *icon);
#endif

signals:
    void activated(const QString &key);

#ifdef Q_OS_LINUX
private slots:
    void onActionInvoked(uint id, const QString &action);

private:
    QHash<uint, QString> m_keys;
    QHash<QString, uint> m_ids;
#elif defined(Q_OS_WIN) || defined(Q_OS_MACOS)
private:
    QSystemTrayIcon *m_trayIcon = nullptr;
    QString m_shownKey;
#endif
};
