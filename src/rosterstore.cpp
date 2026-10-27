#include "rosterstore.h"

#include <QXmppRosterIq.h>
#include <QXmppTask.h>

static QXmppTask<void> done() {
    QXmppPromise<void> promise;
    auto task = promise.task();
    promise.finish();
    return task;
}

// The subscription as RFC 6121 spells it; QXmpp keeps its own converters private.
static const QList<std::pair<QXmppRosterIq::Item::SubscriptionType, QString>> Subscriptions{
    {QXmppRosterIq::Item::None, "none"},
    {QXmppRosterIq::Item::From, "from"},
    {QXmppRosterIq::Item::To, "to"},
    {QXmppRosterIq::Item::Both, "both"},
    {QXmppRosterIq::Item::Remove, "remove"},
};

RosterRow RosterStore::toRow(const QXmppRosterIq::Item &item) {
    QStringList groups = item.groups().values();
    groups.sort();
    QString subscription = "none";
    for (const auto &[type, name] : Subscriptions)
        if (type == item.subscriptionType())
            subscription = name;
    return {item.bareJid().toLower(), item.name(), groups, subscription, item.isApproved()};
}

QXmppRosterIq::Item RosterStore::toItem(const RosterRow &row) {
    QXmppRosterIq::Item item;
    item.setBareJid(row.jid);
    item.setName(row.name);
    item.setGroups(QSet<QString>(row.groups.begin(), row.groups.end()));
    item.setSubscriptionType(QXmppRosterIq::Item::None);
    for (const auto &[type, name] : Subscriptions)
        if (name == row.subscription)
            item.setSubscriptionType(type);
    item.setIsApproved(row.approved);
    return item;
}

QXmppTask<QXmppRosterStorage::RosterCache> RosterStore::load() {
    RosterCache cache;
    // Rows without a version would make the server skip sending a roster
    // we don't fully have, so they count only together.
    cache.version = m_history->rosterVersion();
    if (!cache.version.isEmpty()) {
        const auto rows = m_history->roster();
        for (const RosterRow &row : rows)
            cache.items.push_back(toItem(row));
    }
    QXmppPromise<RosterCache> promise;
    auto task = promise.task();
    promise.finish(std::move(cache));
    return task;
}

QXmppTask<void> RosterStore::replaceAll(const QString &version, const std::vector<QXmppRosterIq::Item> &items) {
    QList<RosterRow> rows;
    for (const auto &item : items)
        rows << toRow(item);
    m_history->replaceRoster(version, rows);
    return done();
}

QXmppTask<void> RosterStore::upsertItem(const QString &version, const QXmppRosterIq::Item &item) {
    m_history->upsertRosterRow(version, toRow(item));
    return done();
}

QXmppTask<void> RosterStore::removeItem(const QString &version, const QString &bareJid) {
    m_history->removeRosterRow(version, bareJid.toLower());
    return done();
}

QXmppTask<void> RosterStore::clear() {
    m_history->clearRoster();
    return done();
}
