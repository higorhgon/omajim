#include "tray.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QGuiApplication>

#include "backend.h"
#include "contactmodel.h"

static const char *WatcherService = "org.kde.StatusNotifierWatcher";

QDBusArgument &operator<<(QDBusArgument &argument, const DBusMenuLayoutItem &item) {
    argument.beginStructure();
    argument << item.id << item.properties;
    argument.beginArray(qMetaTypeId<QDBusVariant>());
    for (const DBusMenuLayoutItem &child : item.children)
        argument << QDBusVariant(QVariant::fromValue(child));
    argument.endArray();
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, DBusMenuLayoutItem &item) {
    argument.beginStructure();
    argument >> item.id >> item.properties;
    argument.beginArray();
    item.children.clear();
    while (!argument.atEnd()) {
        QDBusVariant child;
        argument >> child;
        item.children << qdbus_cast<DBusMenuLayoutItem>(child.variant());
    }
    argument.endArray();
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const DBusMenuItemProperties &item) {
    argument.beginStructure();
    argument << item.id << item.properties;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, DBusMenuItemProperties &item) {
    argument.beginStructure();
    argument >> item.id >> item.properties;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const DBusMenuEvent &event) {
    argument.beginStructure();
    argument << event.id << event.eventId << event.data << event.timestamp;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, DBusMenuEvent &event) {
    argument.beginStructure();
    argument >> event.id >> event.eventId >> event.data >> event.timestamp;
    argument.endStructure();
    return argument;
}

// Menu ids. The statuses follow the order of the window's status menu.
enum : int { RootId = 0, ToggleWindowId = 1, StatusId = 2, SeparatorId = 3, QuitId = 4, FirstStatusId = 10 };
static const QStringList Statuses{"online", "chat", "away", "xa", "dnd", "offline"};

TrayMenu::TrayMenu(Backend *backend, QObject *parent) : QObject(parent), m_backend(backend) {
    qDBusRegisterMetaType<DBusMenuLayoutItem>();
    qDBusRegisterMetaType<DBusMenuItemProperties>();
    qDBusRegisterMetaType<QList<DBusMenuItemProperties>>();
    qDBusRegisterMetaType<DBusMenuEvent>();
    qDBusRegisterMetaType<QList<DBusMenuEvent>>();
}

QString TrayMenu::textDirection() const {
    return QGuiApplication::layoutDirection() == Qt::RightToLeft ? QStringLiteral("rtl") : QStringLiteral("ltr");
}

void TrayMenu::refresh() {
    emit LayoutUpdated(++m_revision, RootId);
}

static QString statusLabel(const QString &status) {
    if (status == "online") return TrayMenu::tr("Available");
    if (status == "chat") return TrayMenu::tr("Free to chat");
    if (status == "away") return TrayMenu::tr("Away");
    if (status == "xa") return TrayMenu::tr("Not available");
    if (status == "dnd") return TrayMenu::tr("Busy");
    return TrayMenu::tr("Offline");
}

DBusMenuLayoutItem TrayMenu::build() const {
    auto entry = [](int id, const QString &label) {
        return DBusMenuLayoutItem{id, {{"label", label}, {"enabled", true}, {"visible", true}}, {}};
    };

    DBusMenuLayoutItem status = entry(StatusId, tr("Status"));
    status.properties.insert("children-display", "submenu");
    for (int i = 0; i < Statuses.size(); ++i) {
        DBusMenuLayoutItem item = entry(FirstStatusId + i, statusLabel(Statuses.at(i)));
        item.properties.insert("toggle-type", "radio");
        item.properties.insert("toggle-state", m_backend->status() == Statuses.at(i) ? 1 : 0);
        // Picking a status needs an account to go online with.
        item.properties.insert("enabled", m_backend->signedIn());
        status.children << item;
    }

    DBusMenuLayoutItem separator{SeparatorId, {{"type", "separator"}, {"visible", true}}, {}};
    DBusMenuLayoutItem root{RootId, {{"children-display", "submenu"}}, {}};
    root.children << entry(ToggleWindowId, tr("Show or hide the window")) << status << separator
                  << entry(QuitId, tr("Quit"));
    return root;
}

static const DBusMenuLayoutItem *findItem(const DBusMenuLayoutItem &item, int id) {
    if (item.id == id)
        return &item;
    for (const DBusMenuLayoutItem &child : item.children)
        if (const DBusMenuLayoutItem *found = findItem(child, id))
            return found;
    return nullptr;
}

static DBusMenuLayoutItem pruned(const DBusMenuLayoutItem &item, int depth, const QStringList &names) {
    DBusMenuLayoutItem copy{item.id, {}, {}};
    for (auto it = item.properties.begin(); it != item.properties.end(); ++it)
        if (names.isEmpty() || names.contains(it.key()))
            copy.properties.insert(it.key(), it.value());
    // A depth of -1 means the whole tree.
    if (depth != 0)
        for (const DBusMenuLayoutItem &child : item.children)
            copy.children << pruned(child, depth < 0 ? -1 : depth - 1, names);
    return copy;
}

uint TrayMenu::GetLayout(int parentId, int recursionDepth, const QStringList &propertyNames,
                         DBusMenuLayoutItem &layout) {
    const DBusMenuLayoutItem root = build();
    const DBusMenuLayoutItem *parent = findItem(root, parentId);
    layout = parent ? pruned(*parent, recursionDepth, propertyNames) : DBusMenuLayoutItem{parentId, {}, {}};
    return m_revision;
}

QList<DBusMenuItemProperties> TrayMenu::GetGroupProperties(const QList<int> &ids, const QStringList &propertyNames) {
    const DBusMenuLayoutItem root = build();
    QList<DBusMenuItemProperties> result;
    std::function<void(const DBusMenuLayoutItem &)> collect = [&](const DBusMenuLayoutItem &item) {
        if (ids.isEmpty() || ids.contains(item.id))
            result << DBusMenuItemProperties{item.id, pruned(item, 0, propertyNames).properties};
        for (const DBusMenuLayoutItem &child : item.children)
            collect(child);
    };
    collect(root);
    return result;
}

QDBusVariant TrayMenu::GetProperty(int id, const QString &name) {
    const DBusMenuLayoutItem root = build();
    const DBusMenuLayoutItem *item = findItem(root, id);
    return QDBusVariant(item ? item->properties.value(name) : QVariant());
}

void TrayMenu::Event(int id, const QString &eventId, const QDBusVariant &, uint) {
    if (eventId == "clicked")
        trigger(id);
}

QList<int> TrayMenu::EventGroup(const QList<DBusMenuEvent> &events) {
    QList<int> errors;
    for (const DBusMenuEvent &event : events)
        if (event.eventId == "clicked" && !trigger(event.id))
            errors << event.id;
    return errors;
}

bool TrayMenu::AboutToShow(int) {
    return false;
}

QList<int> TrayMenu::AboutToShowGroup(const QList<int> &, QList<int> &idErrors) {
    idErrors.clear();
    return {};
}

bool TrayMenu::trigger(int id) {
    if (id == ToggleWindowId) {
        emit toggleWindowRequested();
    } else if (id == QuitId) {
        emit quitRequested();
    } else if (id >= FirstStatusId && id < FirstStatusId + Statuses.size()) {
        m_backend->setStatus(Statuses.at(id - FirstStatusId));
    } else {
        return false;
    }
    return true;
}

Tray::Tray(Backend *backend, QObject *parent, bool exportOnBus)
    : QObject(parent),
      m_backend(backend),
      m_menu(backend),
      m_serviceName(QStringLiteral("org.kde.StatusNotifierItem-%1-1").arg(QCoreApplication::applicationPid())) {
    connect(&m_menu, &TrayMenu::toggleWindowRequested, this, &Tray::toggleWindowRequested);
    connect(&m_menu, &TrayMenu::quitRequested, this, &Tray::quitRequested);

    connect(backend, &Backend::connectionStateChanged, this, &Tray::refresh);
    connect(backend, &Backend::errorTextChanged, this, &Tray::refresh);
    connect(backend->contacts(), &ContactModel::totalUnreadChanged, this, &Tray::refresh);
    connect(backend, &Backend::statusChanged, this, [this] {
        refresh();
        m_menu.refresh();
    });
    connect(backend, &Backend::signedInChanged, this, [this] {
        refresh();
        m_menu.refresh();
    });
    m_lastState = state();

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!exportOnBus || !bus.isConnected())
        return;
    const auto options = QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals |
                         QDBusConnection::ExportAllProperties;
    bus.registerObject("/StatusNotifierItem", this, options);
    bus.registerObject("/MenuBar", &m_menu, options);
    bus.registerService(m_serviceName);

    // The bar can restart (a theme or config change), and its watcher forgets
    // every item, so register again whenever it comes back.
    auto *watcher = new QDBusServiceWatcher(WatcherService, bus,
                                            QDBusServiceWatcher::WatchForRegistration |
                                                QDBusServiceWatcher::WatchForUnregistration,
                                            this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, &Tray::registerWithWatcher);
    connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] { setAvailable(false); });
    registerWithWatcher();
}

QString Tray::state() const {
    if (m_backend->contacts()->totalUnread() > 0)
        return QStringLiteral("message-new");
    const QString connection = m_backend->connectionState();
    if (connection == "online")
        return m_backend->status();
    if (connection == "connecting")
        return QStringLiteral("connecting");
    if (m_backend->signedIn() && m_backend->status() != "offline" && !m_backend->errorText().isEmpty())
        return QStringLiteral("error");
    return QStringLiteral("offline");
}

QString Tray::iconName() const {
    return QStringLiteral("omajim-status-%1-symbolic").arg(state());
}

void Tray::Activate(int, int) {
    emit toggleWindowRequested();
}

void Tray::SecondaryActivate(int, int) {
    emit toggleWindowRequested();
}

void Tray::ContextMenu(int, int) {
    // Hosts draw the menu from /MenuBar themselves.
}

void Tray::Scroll(int, const QString &) {}

void Tray::registerWithWatcher() {
    QDBusMessage call = QDBusMessage::createMethodCall(WatcherService, "/StatusNotifierWatcher", WatcherService,
                                                       "RegisterStatusNotifierItem");
    call << m_serviceName;
    auto *pending = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(pending, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *pending) {
        setAvailable(!pending->isError());
        pending->deleteLater();
    });
}

void Tray::setAvailable(bool available) {
    if (available == m_available)
        return;
    m_available = available;
    emit availableChanged();
}

void Tray::refresh() {
    const QString current = state();
    if (current == m_lastState)
        return;
    m_lastState = current;
    emit NewIcon();
}
