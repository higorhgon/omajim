#pragma once

#include <QString>
#include <QStringList>
#include <optional>

// Tells whether ufw drops incoming connections to a port, by reading its
// configuration: ufw keeps its rules in world-readable files, so no root is
// needed. Other firewalls (firewalld, plain nftables) aren't checked. Ported
// from the Python Omajim's gajim/common/util/firewall.py.
class Firewall {
public:
    // The defaults are ufw's own files; tests point at copies.
    explicit Firewall(const QString &configuration = QStringLiteral("/etc/ufw/ufw.conf"),
                      const QString &defaults = QStringLiteral("/etc/default/ufw"),
                      const QString &userRules = QStringLiteral("/etc/ufw/user.rules"));

    // True when ufw drops the connections, false when it lets them through,
    // nothing when that can't be told. The peer's address isn't known
    // beforehand; it's expected in the same network as one of ours, so a
    // rule limited to some addresses counts if it covers one of them.
    std::optional<bool> isIncomingPortBlocked(int port, const QString &protocol,
                                              const QStringList &localAddresses) const;

    // Our own IPv4 addresses on real networks, leaving out loopback and the
    // bridges of containers and virtual machines.
    static QStringList localAddresses();

private:
    QString m_configuration;
    QString m_defaults;
    QString m_userRules;
};
