// Theme watching follows Omacut (MIT, David Heinemeier Hansson).
#include "theme.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <cmath>

Theme::Theme(const QString &directory, QObject *parent, const QString &hyprctl)
    : QObject(parent),
      m_directory(directory.isEmpty() ? QDir::homePath() + "/.local/state/omarchy/current" : directory) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(80);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });
    connect(&m_debounce, &QTimer::timeout, this, &Theme::reload);

    // Corners follow Hyprland's effective rounding, which themes and personal
    // overrides can both set, so ask hyprctl rather than reading its config.
    // Without Hyprland the corners stay sharp, like Omarchy's default.
    m_hyprctl = !hyprctl.isEmpty() ? hyprctl
              : directory.isEmpty() && !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE")
                  ? QStandardPaths::findExecutable("hyprctl") : QString();
    m_roundingTimeout.setSingleShot(true);
    m_roundingTimeout.setInterval(500);
    connect(&m_roundingTimeout, &QTimer::timeout, &m_roundingProcess, &QProcess::kill);
    connect(&m_roundingProcess, &QProcess::errorOccurred, this, [this] { m_roundingTimeout.stop(); });
    connect(&m_roundingProcess, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_roundingTimeout.stop();
        const auto value = QJsonDocument::fromJson(m_roundingProcess.readAllStandardOutput()).object().value("int");
        if (code != 0 || status != QProcess::NormalExit || !value.isDouble() || value.toInt(-1) < 0)
            return;
        if (value.toInt() != m_radius) {
            m_radius = value.toInt();
            emit changed();
        }
    });
    m_roundingPoll.setInterval(1000);
    connect(&m_roundingPoll, &QTimer::timeout, this, &Theme::refreshRounding);
    if (!m_hyprctl.isEmpty())
        m_roundingPoll.start();
    reload();
}

Theme::~Theme() {
    if (m_roundingProcess.state() != QProcess::NotRunning) {
        m_roundingProcess.kill();
        m_roundingProcess.waitForFinished(1000);
    }
}

void Theme::refreshRounding() {
    if (m_hyprctl.isEmpty() || m_roundingProcess.state() != QProcess::NotRunning)
        return;
    m_roundingProcess.start(m_hyprctl, {"-j", "getoption", "decoration:rounding"});
    m_roundingTimeout.start();
}

QString Theme::readAccent(const QString &path) {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const QRegularExpression expression(R"re(^\s*accent\s*=\s*["'](#[0-9a-fA-F]{6})["'])re",
                                            QRegularExpression::MultilineOption);
        const auto match = expression.match(QString::fromUtf8(file.readAll()));
        if (match.hasMatch())
            return match.captured(1);
    }
    return "#FFD60A";
}

// Black or white, whichever reads on the accent (WCAG relative luminance).
QString Theme::accentForeground() const {
    const QColor c(m_accent);
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    const double luminance = .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
    return luminance > .179 ? "black" : "white";
}

void Theme::reload() {
    refreshRounding();
    const auto paths = m_watcher.files() + m_watcher.directories();
    if (!paths.isEmpty())
        m_watcher.removePaths(paths);

    // A theme switch replaces symlinks and files, so watch the parents too and
    // re-arm after every change. Watching the nearest existing ancestor means
    // installing Omarchy later works without restarting the app.
    QString ancestor = m_directory;
    while (!QFileInfo::exists(ancestor) && ancestor != "/")
        ancestor = QFileInfo(ancestor).absolutePath();
    QStringList candidates{ancestor, QFileInfo(ancestor).absolutePath(), m_directory,
                           m_directory + "/theme", m_directory + "/theme/colors.toml"};
    candidates.removeDuplicates();
    for (const auto &path : candidates)
        if (QFileInfo::exists(path))
            m_watcher.addPath(path);

    const QString path = m_directory + "/theme/colors.toml";
    const auto accent = readAccent(path);
    const QVariantMap colors = palette(readColors(path));
    if (accent != m_accent || colors != m_colors) {
        m_accent = accent;
        m_colors = colors;
        emit changed();
    }
}

QMap<QString, QColor> Theme::readColors(const QString &path) {
    QMap<QString, QColor> values;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return values;
    // Quoted or bare hex, with an optional comment after it.
    const QRegularExpression entry(R"re(^\s*([a-z_]+)\s*=\s*(["']?)(#[0-9a-fA-F]{6})\2\s*(?:#.*)?\s*$)re");
    while (!file.atEnd()) {
        const auto match = entry.match(QString::fromUtf8(file.readLine()));
        if (match.hasMatch())
            values[match.captured(1)] = QColor(match.captured(3));
    }
    return values;
}

// The palette follows Hype (MIT, David Heinemeier Hansson): surfaces are the
// theme's background nudged toward its foreground or accent, so every theme
// gets a coherent set without listing them all.
static QColor mix(const QColor &base, const QColor &ink, double amount) {
    return QColor::fromRgbF(base.redF() + (ink.redF() - base.redF()) * amount,
                            base.greenF() + (ink.greenF() - base.greenF()) * amount,
                            base.blueF() + (ink.blueF() - base.blueF()) * amount);
}

static QColor contrastInk(const QColor &color) {
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    const double luminance = .2126 * linear(color.redF()) + .7152 * linear(color.greenF()) + .0722 * linear(color.blueF());
    return luminance > .179 ? QColor("#111111") : QColor("#ffffff");
}

QVariantMap Theme::palette(const QMap<QString, QColor> &values) {
    // Without Omarchy: a dark neutral with the default yellow accent.
    const QColor background = values.value("background", QColor("#16161a"));
    const QColor foreground = values.value("foreground", QColor("#e4e4e7"));
    const QColor accent = values.value("accent", QColor("#FFD60A"));
    const QColor selection = values.value("selection", mix(background, accent, .3));
    const QColor border = mix(background, foreground, .22);
    return {{"background", background},
            {"foreground", foreground},
            {"accent", accent},
            {"panel", mix(background, foreground, .025)},
            {"button", values.value("lighter_background", mix(background, foreground, .08))},
            {"hover", mix(background, accent, .22)},
            {"border", border},
            {"muted", mix(background, foreground, .62)},
            {"selection", selection},
            {"selectionText", contrastInk(selection)},
            {"accentText", contrastInk(accent)},
            {"accentHover", mix(accent, foreground, .15)},
            {"error", values.value("red", QColor("#e5534b"))},
            {"green", values.value("green", QColor("#4ade80"))},
            {"yellow", values.value("yellow", QColor("#facc15"))},
            {"orange", values.value("orange", values.value("yellow", QColor("#fb923c")))}};
}
