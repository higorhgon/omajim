#pragma once

#include <QXmppRosterStorage.h>

#include "history.h"

class QXmppRosterIq;

// Keeps QXmpp's roster in the account's SQLite database, so the contact list
// is there before the connection is, and the server only sends what changed
// since (roster versioning). SQLite on the local disk is quick enough to
// answer at once, so every task comes back already finished.
class RosterStore : public QXmppRosterStorage {
public:
    explicit RosterStore(History *history) : m_history(history) {}

    QXmppTask<RosterCache> load() override;
    QXmppTask<void> replaceAll(const QString &version, const std::vector<QXmppRosterIq::Item> &items) override;
    QXmppTask<void> upsertItem(const QString &version, const QXmppRosterIq::Item &item) override;
    QXmppTask<void> removeItem(const QString &version, const QString &bareJid) override;
    QXmppTask<void> clear() override;

    static RosterRow toRow(const QXmppRosterIq::Item &item);
    static QXmppRosterIq::Item toItem(const RosterRow &row);

private:
    History *m_history;
};
