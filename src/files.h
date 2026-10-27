#pragma once

#include <QString>

namespace Files {

// A name a contact sent is only a suggestion: strip anything that would
// leave the download directory or hide the file.
QString safeFileName(const QString &offered);

// The first free "name.ext", "name (2).ext", ... in the directory, so a
// received file never replaces one that is already there.
QString uniquePath(const QString &directory, const QString &fileName);

// "1.4 MB", in the current locale's number format.
QString formatSize(qint64 bytes);

}
