#include "updater.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <QVersionNumber>
#include <cstdio>
#include <memory>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

static const char *ReleaseApi = "https://api.github.com/repos/higorhgon/omajim/releases/latest";

static Updater::Kind detectKind() {
#ifdef Q_OS_WIN
    // The installer leaves its uninstaller next to the program.
    return QFile::exists(QCoreApplication::applicationDirPath() + "/unins000.exe")
               ? Updater::Kind::WindowsInstalled : Updater::Kind::WindowsPortable;
#else
    // The AppImage runtime says where the image is; a package doesn't.
    const QString image = qEnvironmentVariable("APPIMAGE");
    return !image.isEmpty() && QFileInfo(image).isFile() ? Updater::Kind::AppImage : Updater::Kind::None;
#endif
}

Updater::Updater(const QString &settingsFile, QObject *parent)
    : Updater(detectKind(), settingsFile, ReleaseApi, QCoreApplication::applicationVersion(), parent) {
    start();
}

Updater::Updater(Kind kind, const QString &settingsFile, const QString &releaseApi, const QString &currentVersion,
                 QObject *parent)
    : QObject(parent), m_kind(kind), m_settings(settingsFile, QSettings::IniFormat), m_releaseApi(releaseApi),
      m_current(currentVersion) {
    m_enabled = m_settings.value("updates/check", true).toBool();
    m_daily.setInterval(24 * 60 * 60 * 1000);
    connect(&m_daily, &QTimer::timeout, this, &Updater::check);
}

void Updater::start() {
    if (!supported() || !m_enabled)
        return;
    // A while after starting, out of the way of signing in, then daily.
    QTimer::singleShot(15000, this, &Updater::check);
    m_daily.start();
}

void Updater::setEnabled(bool enabled) {
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    m_settings.setValue("updates/check", enabled);
    if (enabled) {
        check();
        m_daily.start();
    } else {
        m_daily.stop();
    }
    emit enabledChanged();
}

bool Updater::isNewer(const QString &tag, const QString &current) {
    QString version = tag.trimmed();
    if (version.startsWith('v') || version.startsWith('V'))
        version.remove(0, 1);
    const QVersionNumber latest = QVersionNumber::fromString(version);
    return !latest.isNull() && latest > QVersionNumber::fromString(current);
}

QString Updater::listedHash(const QByteArray &sums, const QString &name) {
    // "<hash>  <name>", or "<hash> *<name>" for binary mode.
    for (const QByteArray &line : sums.split('\n')) {
        const QString text = QString::fromUtf8(line).trimmed();
        const int space = text.indexOf(' ');
        if (space != 64)
            continue;
        QString file = text.mid(space).trimmed();
        if (file.startsWith('*'))
            file.remove(0, 1);
        if (file == name)
            return text.left(64).toLower();
    }
    return {};
}

QString Updater::assetName() const {
    const QString version = m_latest;
    switch (m_kind) {
    case Kind::AppImage: return QString("omajim-%1-x86_64.AppImage").arg(version);
    case Kind::WindowsInstalled: return QString("omajim-%1-windows-x64-setup.exe").arg(version);
    default: return {};
    }
}

static QNetworkRequest apiRequest(const QUrl &url, const QString &version) {
    QNetworkRequest request(url);
    // GitHub asks for a User-Agent; the app's name and version, nothing else.
    request.setHeader(QNetworkRequest::UserAgentHeader, "Omajim/" + version);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(60000);
    return request;
}

void Updater::check() {
    if (!supported() || !m_enabled || m_checking || m_state == "downloading" || m_state == "installing")
        return;
    m_checking = m_network.get(apiRequest(QUrl(m_releaseApi), m_current));
    connect(m_checking, &QNetworkReply::finished, this, [this, reply = m_checking.data()] { received(reply); });
}

void Updater::received(QNetworkReply *reply) {
    reply->deleteLater();
    // Offline, rate-limited, or no release yet: try again tomorrow, quietly.
    if (reply->error() != QNetworkReply::NoError)
        return;
    const QJsonObject release = QJsonDocument::fromJson(reply->readAll()).object();
    const QString tag = release.value("tag_name").toString();
    if (release.value("draft").toBool() || release.value("prerelease").toBool() || !isNewer(tag, m_current))
        return;

    QString version = tag;
    if (version.startsWith('v') || version.startsWith('V'))
        version.remove(0, 1);
    m_assets.clear();
    for (const QJsonValue &asset : release.value("assets").toArray()) {
        const QJsonObject object = asset.toObject();
        m_assets.insert(object.value("name").toString(), object.value("browser_download_url").toString());
    }
    m_pageUrl = release.value("html_url").toString();
    m_dismissed = m_settings.value("updates/dismissed").toString() == version;
    if (version != m_latest || m_state == "failed") {
        m_latest = version;
        m_state = "idle";
        m_error.clear();
    }
    emit changed();
}

void Updater::dismiss() {
    m_dismissed = true;
    m_settings.setValue("updates/dismissed", m_latest);
    emit changed();
}

void Updater::fail(const QString &text) {
    m_state = "failed";
    m_error = text;
    m_progress = 0;
    emit changed();
}

void Updater::update() {
    if (m_latest.isEmpty() || m_state == "downloading" || m_state == "installing")
        return;
    const QString name = assetName();
    if (!canInstall() || !m_assets.contains(name) || !m_assets.contains("SHA256SUMS")) {
        QDesktopServices::openUrl(QUrl(m_pageUrl));
        return;
    }

    // Where the download goes: beside the AppImage, so putting it in place
    // is a rename on the same disk; for the setup, a temporary folder.
    QString target;
    if (m_kind == Kind::AppImage) {
        target = qEnvironmentVariable("APPIMAGE") + ".new";
    } else {
        const QString folder = QDir::temp().filePath("omajim-update");
        QDir().mkpath(folder);
        target = QDir(folder).filePath(name);
    }

    m_state = "downloading";
    m_progress = 0;
    m_error.clear();
    emit changed();

    // The sums first, small, then the file, hashed as it arrives.
    QNetworkReply *sums = m_network.get(apiRequest(QUrl(m_assets.value("SHA256SUMS").toString()), m_current));
    connect(sums, &QNetworkReply::finished, this, [this, sums, name, target] {
        sums->deleteLater();
        const QString expected = listedHash(sums->readAll(), name);
        if (sums->error() != QNetworkReply::NoError || expected.isEmpty())
            return fail(tr("Couldn't download the list of checksums."));

        auto file = std::make_shared<QFile>(target);
        if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate))
            return fail(tr("Couldn't write %1.").arg(QDir::toNativeSeparators(target)));
        auto hash = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha256);
        QNetworkRequest request = apiRequest(QUrl(m_assets.value(name).toString()), m_current);
        request.setRawHeader("Accept", "application/octet-stream");
        request.setTransferTimeout(120000);
        QNetworkReply *download = m_network.get(request);
        connect(download, &QNetworkReply::readyRead, this, [download, file, hash] {
            const QByteArray chunk = download->readAll();
            hash->addData(chunk);
            file->write(chunk);
        });
        connect(download, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
            if (total > 0) {
                m_progress = qreal(received) / qreal(total);
                emit changed();
            }
        });
        connect(download, &QNetworkReply::finished, this, [this, download, file, hash, expected, target] {
            download->deleteLater();
            const QByteArray rest = download->readAll();
            hash->addData(rest);
            file->write(rest);
            const bool written = file->flush() && file->error() == QFile::NoError;
            file->close();
            if (download->error() != QNetworkReply::NoError || !written) {
                QFile::remove(target);
                return fail(tr("The download didn't finish."));
            }
            if (QString::fromLatin1(hash->result().toHex()) != expected) {
                QFile::remove(target);
                return fail(tr("The download doesn't match its checksum, so it was discarded."));
            }
            install(target);
        });
    });
}

void Updater::install(const QString &file) {
    m_state = "installing";
    emit changed();
    const QString pid = QString::number(QCoreApplication::applicationPid());

    if (m_kind == Kind::AppImage) {
        const QString image = qEnvironmentVariable("APPIMAGE");
        QFile::setPermissions(file, QFile::permissions(image) | QFile::ExeOwner | QFile::ReadOwner);
        // A rename over the old image: the running copy keeps its open file,
        // and the next start is the new one.
        if (std::rename(QFile::encodeName(file).constData(), QFile::encodeName(image).constData()) != 0) {
            QFile::remove(file);
            return fail(tr("Couldn't replace %1.").arg(image));
        }
        // Started again once this one is gone, or it would just bring this
        // window forward.
        QProcess::startDetached("/bin/sh", {"-c", "while kill -0 \"$1\" 2>/dev/null; do sleep 0.2; done; exec \"$2\"",
                                            "sh", pid, image});
        emit restartRequested();
        return;
    }

#ifdef Q_OS_WIN
    // The setup updates the same installation quietly and opens Omajim
    // again (/relaunch=1, see omajim.iss); one for all users asks for
    // permission, so it's started the way Explorer would.
    const QString perUser = QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA"));
    const bool forUser = !perUser.isEmpty()
        && QCoreApplication::applicationDirPath().startsWith(perUser, Qt::CaseInsensitive);
    const QString parameters = QString("/SILENT /SUPPRESSMSGBOXES /NORESTART /relaunch=1 %1")
                                   .arg(forUser ? "/CURRENTUSER" : "/ALLUSERS");
    const std::wstring path = QDir::toNativeSeparators(file).toStdWString();
    const std::wstring arguments = parameters.toStdWString();
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", path.c_str(), arguments.c_str(), nullptr, SW_SHOWNORMAL));
    if (result <= 32)
        return fail(tr("Couldn't start the installer."));
    emit restartRequested();
#else
    Q_UNUSED(pid)
#endif
}
