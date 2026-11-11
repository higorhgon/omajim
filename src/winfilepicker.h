#pragma once

#include "filepicker.h"

// O seletor de arquivos no Windows: o diálogo do próprio Explorer, pelo
// QFileDialog. No Linux quem faz isso é o PortalFilePicker, sobre D-Bus.
class WinFilePicker : public FilePicker {
    Q_OBJECT

public:
    using FilePicker::FilePicker;

    void openFile(const QString &title) override;
    void saveFile(const QString &title, const QString &suggestedName) override;
};
