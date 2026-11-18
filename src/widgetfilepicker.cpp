#include "widgetfilepicker.h"

#include <QDir>
#include <QFileDialog>
#include <QStandardPaths>
#include <QTimer>

// O diálogo abre depois, fora de quem pediu (em geral um clique no QML),
// para não rodar o laço modal dele no meio da chamada.
void WidgetFilePicker::openFile(const QString &title) {
    QTimer::singleShot(0, this, [this, title] {
        emit picked(QFileDialog::getOpenFileName(nullptr, title));
    });
}

void WidgetFilePicker::saveFile(const QString &title, const QString &suggestedName) {
    QTimer::singleShot(0, this, [this, title, suggestedName] {
        const QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        emit savePicked(QFileDialog::getSaveFileName(nullptr, title, QDir(downloads).filePath(suggestedName)));
    });
}
