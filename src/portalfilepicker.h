#pragma once

#include <QVariantMap>

#include "filepicker.h"

// Opens the desktop's file chooser through xdg-desktop-portal
// (org.freedesktop.portal.FileChooser), so the dialog matches the rest of
// Omarchy and works the same inside a sandbox.
class PortalFilePicker : public FilePicker {
    Q_OBJECT

public:
    using FilePicker::FilePicker;

    void openFile(const QString &title) override;
    void saveFile(const QString &title, const QString &suggestedName) override;

private slots:
    void onResponse(uint response, const QVariantMap &results);

private:
    // OpenFile or SaveFile, with its options; both answer the same way.
    void request(const QString &method, const QString &title, QVariantMap options);

    QString m_requestPath;
    bool m_saving = false;
};
