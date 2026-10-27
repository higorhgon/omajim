#pragma once

#include <QHash>
#include <QObject>

// Desktop notifications over org.freedesktop.Notifications, which Omarchy's
// notification daemon serves. Clicking one brings the conversation up.
class Notifier : public QObject {
    Q_OBJECT

public:
    explicit Notifier(QObject *parent = nullptr);

    void notify(const QString &key, const QString &title, const QString &body);

signals:
    void activated(const QString &key);

private slots:
    void onActionInvoked(uint id, const QString &action);

private:
    QHash<uint, QString> m_keys;
    QHash<QString, uint> m_ids;
};
