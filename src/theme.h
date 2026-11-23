#pragma once

#include <QFileSystemWatcher>
#include <QColor>
#include <QMap>
#include <QObject>
#include <QVariantMap>
#include <QProcess>
#include <QSettings>
#include <QTimer>

#include <memory>

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
    // Without an Omarchy theme (Windows, other distributions) the colors come
    // from a built-in scheme the user picks: system, light, dark, catppuccin.
    Q_PROPERTY(bool followsOmarchy READ followsOmarchy NOTIFY changed)
    Q_PROPERTY(QString scheme READ scheme WRITE setScheme NOTIFY changed)
    // Omarchy's Glassy windows (omarchy hyprland window transparency glassy):
    // the window's own surfaces turn see-through so Hyprland blurs what is
    // behind them, while text, cards and controls stay solid. The palette's
    // glassPanel and glassBackground carry it; they match panel and background
    // when Glassy is off.
    Q_PROPERTY(bool glassy READ glassy NOTIFY changed)
    // True under Glassy and under Omarchy's default Transparent windows
    // (omarchy hyprland window transparency transparent); false when Omarchy
    // asks for opaque windows or the theme isn't Omarchy's.
    Q_PROPERTY(bool translucent READ translucent NOTIFY changed)

public:
    explicit Theme(const QString &currentDirectory = {}, QObject *parent = nullptr, const QString &hyprctl = {},
                   const QString &settingsFile = {});
    ~Theme() override;

    QString accent() const { return m_accent; }
    QString accentForeground() const;
    int radius() const { return m_radius; }
    QVariantMap colors() const { return m_colors; }
    bool followsOmarchy() const { return m_followsOmarchy; }
    bool glassy() const { return m_glassy; }
    bool translucent() const { return m_opacity < 1.0; }
    QString scheme() const { return m_scheme; }
    void setScheme(const QString &scheme);

    // How opaque the window's surfaces are under Glassy: Omarchy fades GTK
    // windows to 0.75 there, and the text drawn over them stays solid here.
    static constexpr double glassOpacity = 0.75;
    // Transparent, the default: Omarchy's inactive window opacity (0.96), so
    // the blur shows faintly through the window's own surfaces.
    static constexpr double transparentOpacity = 0.96;

    static QString readAccent(const QString &path);
    // Every "name = #rrggbb" in a colors.toml.
    static QMap<QString, QColor> readColors(const QString &path);
    static QVariantMap palette(const QMap<QString, QColor> &values, double surfaceOpacity = 1.0);
    // The colors.toml values of a built-in scheme; "system" picks light or
    // dark from the desktop.
    static QMap<QString, QColor> schemeColors(const QString &scheme);

signals:
    void changed();

private:
    void reload();
    void refreshRounding();

    QString m_directory;
    QString m_accent = "#FFD60A";
    QVariantMap m_colors;
    bool m_followsOmarchy = false;
    bool m_glassy = false;
    double m_opacity = 1.0;
    QString m_scheme = "system";
    std::unique_ptr<QSettings> m_settings;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_hyprctl;
    int m_radius = 0;
    QProcess m_roundingProcess;
    QTimer m_roundingPoll, m_roundingTimeout;
};
