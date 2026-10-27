#include "files.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>

namespace Files {

QString safeFileName(const QString &offered) {
    QString name = QFileInfo(QString(offered).replace('\\', '/')).fileName().trimmed();
    name.remove(QChar(0));
    while (name.startsWith('.'))
        name.remove(0, 1);
    return name.isEmpty() ? QStringLiteral("file") : name;
}

QString uniquePath(const QString &directory, const QString &fileName) {
    const QDir dir(directory);
    const QFileInfo info(fileName);
    const QString base = info.completeBaseName();
    const QString suffix = info.suffix().isEmpty() ? QString() : "." + info.suffix();

    QString candidate = dir.filePath(fileName);
    // The ".part" file of a transfer in progress counts as taken too.
    for (int n = 2; QFileInfo::exists(candidate) || QFileInfo::exists(candidate + ".part"); ++n)
        candidate = dir.filePath(QStringLiteral("%1 (%2)%3").arg(base).arg(n).arg(suffix));
    return candidate;
}

QString formatSize(qint64 bytes) {
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeSIFormat);
}

}
