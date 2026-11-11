#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QSettings>
#include <QTimer>
#include <QVariantMap>

class QNetworkReply;

// New versions from the GitHub releases, for the builds that don't come from
// a package manager: the AppImage replaces itself, the Windows installer runs
// the new setup quietly, and the portable zip opens the release page. Each
// download is checked against the release's SHA256SUMS before it's used.
class Updater : public QObject {
    Q_OBJECT
    // Whether this build can look for updates at all (not the Arch package).
    Q_PROPERTY(bool supported READ supported CONSTANT)
    // Whether it updates itself or only points at the release page.
    Q_PROPERTY(bool canInstall READ canInstall CONSTANT)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    // idle, downloading, installing or failed.
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(qreal progress READ progress NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)

public:
    enum class Kind { None, AppImage, WindowsInstalled, WindowsPortable };

    explicit Updater(const QString &settingsFile, QObject *parent = nullptr);
    // For tests: a given kind, release API and version.
    Updater(Kind kind, const QString &settingsFile, const QString &releaseApi, const QString &currentVersion,
            QObject *parent = nullptr);

    bool supported() const { return m_kind != Kind::None; }
    bool canInstall() const { return m_kind == Kind::AppImage || m_kind == Kind::WindowsInstalled; }
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    QString latestVersion() const { return m_latest; }
    bool available() const { return !m_latest.isEmpty() && !m_dismissed; }
    QString state() const { return m_state; }
    qreal progress() const { return m_progress; }
    QString errorText() const { return m_error; }

    // Whether a release tag ("v1.2.0") is newer than a version ("1.1.3").
    static bool isNewer(const QString &tag, const QString &current);
    // The hash SHA256SUMS lists for a file name, lowercase, or empty.
    static QString listedHash(const QByteArray &sums, const QString &name);

    Q_INVOKABLE void check();
    // Downloads, checks and installs; the portable build opens the page.
    Q_INVOKABLE void update();
    // Hides the notice until the next version.
    Q_INVOKABLE void dismiss();

signals:
    void enabledChanged();
    void changed();
    // The new version is in place and Omajim should close for it.
    void restartRequested();

private:
    void start();
    void received(QNetworkReply *reply);
    void fail(const QString &text);
    void install(const QString &file);
    QString assetName() const;

    Kind m_kind;
    QSettings m_settings;
    QString m_releaseApi;
    QString m_current;
    bool m_enabled = true;
    QString m_latest;
    QString m_pageUrl;
    QVariantMap m_assets;  // name -> download URL
    bool m_dismissed = false;
    QString m_state = "idle";
    qreal m_progress = 0;
    QString m_error;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_checking;
    QTimer m_daily;
};
