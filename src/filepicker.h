#pragma once

#include <QObject>

// Asks the user for a file to open. The desktop's own dialog does it in the
// app (PortalFilePicker); tests hand the backend a fake one instead.
class FilePicker : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    virtual void openFile(const QString &title) = 0;

signals:
    // An empty path means the user closed the dialog without picking.
    void picked(const QString &path);
    void failed(const QString &error);
};
