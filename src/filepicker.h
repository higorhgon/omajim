#pragma once

#include <QObject>

// Asks the user for a file to open, or where to save one. The desktop's own
// dialog does it in the app (PortalFilePicker); tests hand the backend a fake
// one instead.
class FilePicker : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    virtual void openFile(const QString &title) = 0;
    // Asks where to save a file, offering a name.
    virtual void saveFile(const QString &title, const QString &suggestedName) {
        Q_UNUSED(title)
        Q_UNUSED(suggestedName)
        emit failed(tr("Saving isn't available here."));
    }

signals:
    // An empty path means the user closed the dialog without picking.
    void picked(const QString &path);
    void savePicked(const QString &path);
    void failed(const QString &error);
};
