#pragma once

#include <QFileSystemWatcher>
#include <QColor>
#include <QMap>
#include <QObject>
#include <QVariantMap>
#include <QProcess>
#include <QTimer>

class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString accentForeground READ accentForeground NOTIFY changed)
    Q_PROPERTY(int radius READ radius NOTIFY changed)
    // The whole palette, derived from the Omarchy theme the way Hype does:
    // background, foreground, accent, panel, button, hover, border, muted,
    // selection, selectionText, accentText, accentHover, error, and the
    // green, yellow, and orange that presence dots use.
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)

public:
    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr, const QString &hyprctl = {});
    ~Theme() override;

    QString accent() const { return m_accent; }
    QString accentForeground() const;
    int radius() const { return m_radius; }
    QVariantMap colors() const { return m_colors; }

    static QString readAccent(const QString &path);
    // Every "name = #rrggbb" in a colors.toml.
    static QMap<QString, QColor> readColors(const QString &path);
    static QVariantMap palette(const QMap<QString, QColor> &values);

signals:
    void changed();

private:
    void reload();
    void refreshRounding();

    QString m_directory;
    QString m_accent = "#FFD60A";
    QVariantMap m_colors;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_hyprctl;
    int m_radius = 0;
    QProcess m_roundingProcess;
    QTimer m_roundingPoll, m_roundingTimeout;
};
