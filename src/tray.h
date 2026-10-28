#pragma once

#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QList>
#include <QObject>
#include <QVariantMap>

class Backend;

// One entry of the tray menu as com.canonical.dbusmenu sends it: (ia{sv}av).
struct DBusMenuLayoutItem {
    int id = 0;
    QVariantMap properties;
    QList<DBusMenuLayoutItem> children;
};

// (ia{sv}): an entry's properties, for GetGroupProperties.
struct DBusMenuItemProperties {
    int id = 0;
    QVariantMap properties;
};

// (isvu): one event of EventGroup.
struct DBusMenuEvent {
    int id = 0;
    QString eventId;
    QDBusVariant data;
    uint timestamp = 0;
};

Q_DECLARE_METATYPE(DBusMenuLayoutItem)
Q_DECLARE_METATYPE(DBusMenuItemProperties)
Q_DECLARE_METATYPE(QList<DBusMenuItemProperties>)
Q_DECLARE_METATYPE(DBusMenuEvent)
Q_DECLARE_METATYPE(QList<DBusMenuEvent>)

QDBusArgument &operator<<(QDBusArgument &argument, const DBusMenuLayoutItem &item);
const QDBusArgument &operator>>(const QDBusArgument &argument, DBusMenuLayoutItem &item);
QDBusArgument &operator<<(QDBusArgument &argument, const DBusMenuItemProperties &item);
const QDBusArgument &operator>>(const QDBusArgument &argument, DBusMenuItemProperties &item);
QDBusArgument &operator<<(QDBusArgument &argument, const DBusMenuEvent &event);
const QDBusArgument &operator>>(const QDBusArgument &argument, DBusMenuEvent &event);

// The menu behind the tray item, served at /MenuBar.
class TrayMenu : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.canonical.dbusmenu")
    Q_PROPERTY(uint Version READ version)
    Q_PROPERTY(QString TextDirection READ textDirection)
    Q_PROPERTY(QString Status READ status)
    Q_PROPERTY(QStringList IconThemePath READ iconThemePath)

public:
    explicit TrayMenu(Backend *backend, QObject *parent = nullptr);

    uint version() const { return 3; }
    QString textDirection() const;
    QString status() const { return QStringLiteral("normal"); }
    QStringList iconThemePath() const { return {}; }

    // Labels and the checked status changed; hosts fetch the layout again.
    void refresh();
    void setBackend(Backend *backend) { m_backend = backend; refresh(); }

public slots:
    Q_SCRIPTABLE uint GetLayout(int parentId, int recursionDepth, const QStringList &propertyNames, DBusMenuLayoutItem &layout);
    Q_SCRIPTABLE QList<DBusMenuItemProperties> GetGroupProperties(const QList<int> &ids, const QStringList &propertyNames);
    Q_SCRIPTABLE QDBusVariant GetProperty(int id, const QString &name);
    Q_SCRIPTABLE void Event(int id, const QString &eventId, const QDBusVariant &data, uint timestamp);
    Q_SCRIPTABLE QList<int> EventGroup(const QList<DBusMenuEvent> &events);
    Q_SCRIPTABLE bool AboutToShow(int id);
    Q_SCRIPTABLE QList<int> AboutToShowGroup(const QList<int> &ids, QList<int> &idErrors);

signals:
    Q_SCRIPTABLE void LayoutUpdated(uint revision, int parent);
    void toggleWindowRequested();
    void quitRequested();
    // A status picked here, for every account.
    void statusRequested(const QString &status);

private:
    DBusMenuLayoutItem build() const;
    bool trigger(int id);

    Backend *m_backend;
    uint m_revision = 1;
};

// A StatusNotifierItem with the same id as the Python Omajim's, so the
// omajim.status widget in the Omarchy bar (and any other tray) shows the
// status, new messages, and the menu. Served at /StatusNotifierItem.
class Tray : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierItem")
    Q_PROPERTY(QString Category READ category)
    Q_PROPERTY(QString Id READ id)
    Q_PROPERTY(QString Title READ title)
    Q_PROPERTY(QString Status READ status)
    Q_PROPERTY(QString IconName READ iconName)
    Q_PROPERTY(QString AttentionIconName READ attentionIconName)
    Q_PROPERTY(QString IconThemePath READ iconThemePath)
    Q_PROPERTY(bool ItemIsMenu READ itemIsMenu)
    Q_PROPERTY(QDBusObjectPath Menu READ menu)

    // For QML: whether a tray host shows the item, so closing the window
    // can hide it instead of quitting.
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)

public:
    // Tests pass exportOnBus false, so nothing shows up in the real tray.
    explicit Tray(Backend *backend, QObject *parent = nullptr, bool exportOnBus = true);

    QString category() const { return QStringLiteral("Communications"); }
    QString id() const { return QStringLiteral("io.github.higorhgon.Omajim"); }
    QString title() const { return QStringLiteral("Omajim"); }
    QString status() const { return QStringLiteral("Active"); }
    QString iconName() const;
    QString attentionIconName() const { return {}; }
    QString iconThemePath() const { return {}; }
    bool itemIsMenu() const { return false; }
    QDBusObjectPath menu() const { return QDBusObjectPath("/MenuBar"); }
    bool available() const { return m_available; }

    // What the icon shows: the status, connecting, error, or message-new.
    QString state() const;
    // The language changed, so the menu labels did too.
    void retranslate() { m_menu.refresh(); }
    // The account the window shows, which the icon follows.
    void setBackend(Backend *backend);

public slots:
    Q_SCRIPTABLE void Activate(int x, int y);
    Q_SCRIPTABLE void SecondaryActivate(int x, int y);
    Q_SCRIPTABLE void ContextMenu(int x, int y);
    Q_SCRIPTABLE void Scroll(int delta, const QString &orientation);

signals:
    Q_SCRIPTABLE void NewIcon();
    Q_SCRIPTABLE void NewTitle();
    Q_SCRIPTABLE void NewStatus(const QString &status);
    Q_SCRIPTABLE void NewMenu();
    void availableChanged();
    void toggleWindowRequested();
    void quitRequested();
    void statusRequested(const QString &status);

private:
    void registerWithWatcher();
    void setAvailable(bool available);
    void refresh();

    Backend *m_backend = nullptr;
    QList<QMetaObject::Connection> m_watching;
    TrayMenu m_menu;
    QString m_serviceName;
    QString m_lastState;
    bool m_available = false;
};
