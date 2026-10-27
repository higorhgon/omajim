// Omajim: an XMPP chat client for Omarchy.

#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QUrl>

#include "backend.h"
#include "notifier.h"
#include "portalfilepicker.h"
#include "secretstore.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("omajim");
    app.setApplicationVersion("0.1.0");

    // Matches the window to omajim.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("omajim");
    app.setWindowIcon(QIcon::fromTheme("omajim"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    PortalFilePicker filePicker;
    SecretStore secrets;
    Notifier notifier;
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/omajim";
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/omajim";
    Backend backend(config, data, &filePicker, &secrets, &notifier);

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    backend.start();
    return app.exec();
}
