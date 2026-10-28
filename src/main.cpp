// Omajim: an XMPP chat client for Omarchy.

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDir>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QUrl>

#include "accounts.h"
#include "backend.h"
#include "composer.h"
#include "emojicatalog.h"
#include "gajimimport.h"
#include "idlemonitor.h"
#include "instance.h"
#include "notifier.h"
#include "portalfilepicker.h"
#include "secretstore.h"
#include "theme.h"
#include "tray.h"
#include "translator.h"

// The desktop's interface font, e.g. "Adwaita Sans 11", from the settings
// portal, as Hype does; without it Qt's default font stays.
static void adoptDesktopFont() {
    auto call = QDBusMessage::createMethodCall("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
                                               "org.freedesktop.portal.Settings", "ReadOne");
    call.setArguments({"org.gnome.desktop.interface", "font-name"});
    const auto reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 500);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return;
    const QString name = reply.arguments().first().value<QDBusVariant>().variant().toString();
    const int space = name.lastIndexOf(' ');
    const double size = name.mid(space + 1).toDouble();
    if (space <= 0 || size <= 0)
        return;
    QFont font(name.left(space));
    font.setPointSizeF(size);
    QGuiApplication::setFont(font);
}

int main(int argc, char *argv[]) {
    // Omajim draws every control in the Omarchy theme's colors itself; the
    // gtk3 platform theme would only bring GTK's own look and font rules.
    qputenv("QT_QPA_PLATFORMTHEME", "generic");
    QGuiApplication app(argc, argv);
    app.setApplicationName("omajim");
    app.setApplicationVersion("0.1.0");

    // Matches the window to omajim.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("omajim");
    app.setWindowIcon(QIcon::fromTheme("omajim"));

    Instance instance;
    if (!instance.claim())
        return 0;

    // Closing the window only hides it while the tray can bring it back, so
    // the app decides itself when to quit.
    app.setQuitOnLastWindowClosed(false);

    adoptDesktopFont();
    // The plain Basic style, restyled in QML with the theme's palette, the
    // way Omarchy's own apps (Hype) look.
    QQuickStyle::setStyle("Basic");

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
    // Every account, each connected on its own; the window shows one.
    Accounts accounts(config, data, &filePicker, &secrets, &notifier);
    // On the first run, what Gajim (or the Python Omajim) has comes over.
    accounts.start(GajimImport::settingsCandidates(), GajimImport::candidates());
    Tray tray(accounts.current());
    QObject::connect(&accounts, &Accounts::currentChanged, &tray, [&] { tray.setBackend(accounts.current()); });
    QObject::connect(&tray, &Tray::statusRequested, &accounts, &Accounts::setStatus);
    EmojiCatalog emojis(QDir(config).filePath("omajim.conf"));
    Composer composer;

    // Text drawn by FreeType with the desktop's hinting, like every other app:
    // Qt Quick's default distance-field text smears small bold type at 1x.
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    QObject::connect(&translator, &Translator::changed, &tray, &Tray::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("accounts", &accounts);
    // "backend" is the account on screen; switching accounts swaps it, and
    // every binding to it follows.
    engine.rootContext()->setContextProperty("backend", accounts.current());
    QObject::connect(&accounts, &Accounts::currentChanged, &engine,
                     [&] { engine.rootContext()->setContextProperty("backend", accounts.current()); });
    engine.rootContext()->setContextProperty("tray", &tray);
    engine.rootContext()->setContextProperty("emojis", &emojis);
    engine.rootContext()->setContextProperty("composerFormat", &composer);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    auto showWindow = [window] {
        window->show();
        window->raise();
        window->requestActivate();
    };
    QObject::connect(&instance, &Instance::showRequested, window, showWindow);
    QObject::connect(&tray, &Tray::toggleWindowRequested, window, [window, showWindow] {
        if (window->isVisible() && window->isActive())
            window->hide();
        else
            showWindow();
    });
    // exit(), not quit(): quit() asks the window to close first, and closing
    // it only hides it while the tray shows, which calls the quit off.
    QObject::connect(&tray, &Tray::quitRequested, &app, [] { QCoreApplication::exit(0); });

    // Away after a while without using the computer, as Gajim does; 0 turns
    // a step off. Watched anew when the preferences change the times.
    std::unique_ptr<IdleMonitor> idle;
    auto watchIdle = [&idle, &accounts] {
        // Changing them is using the computer: an automatic away ends.
        accounts.setIdleLevel(0);
        Backend *backend = accounts.current();
        idle = std::make_unique<IdleMonitor>(QList<int>{backend->awayMinutes() * 60000, backend->xaMinutes() * 60000});
        QObject::connect(idle.get(), &IdleMonitor::levelChanged, &accounts, &Accounts::setIdleLevel);
    };
    watchIdle();
    QObject::connect(&accounts, &Accounts::idleTimesChanged, &accounts, watchIdle);
    return app.exec();
}
