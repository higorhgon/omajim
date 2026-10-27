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

private slots:
    void onResponse(uint response, const QVariantMap &results);

private:
    QString m_requestPath;
};
