#pragma once

#include "filepicker.h"

// Diálogos nativos de seleção de arquivos no Windows e no macOS.
class WidgetFilePicker : public FilePicker {
    Q_OBJECT

public:
    using FilePicker::FilePicker;

    void openFile(const QString &title) override;
    void saveFile(const QString &title, const QString &suggestedName) override;
};
