#include "firewall.h"

#include <QFile>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QRegularExpression>

Firewall::Firewall(const QString &configuration, const QString &defaults, const QString &userRules)
    : m_configuration(configuration), m_defaults(defaults), m_userRules(userRules) {}

static std::optional<QStringList> readLines(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return QString::fromUtf8(file.readAll()).split('\n');
}

static std::optional<QString> readSetting(const QString &path, const QString &name) {
    const auto lines = readLines(path);
    if (!lines)
        return std::nullopt;
    for (const QString &raw : *lines) {
        const QString line = raw.trimmed();
        if (line.startsWith(name + "=")) {
            QString value = line.mid(name.size() + 1).trimmed();
            value.remove('"');
            value.remove('\'');
            return value;
        }
    }
    return std::nullopt;
}

// "any", "28011", "6000:6007", or a comma-separated list of these.
static bool portMatches(const QString &spec, int port) {
    if (spec == "any")
        return true;
    for (const QString &part : spec.split(',')) {
        if (part.contains(':')) {
            if (port >= part.section(':', 0, 0).toInt() && port <= part.section(':', 1, 1).toInt())
                return true;
        } else if (part.toInt() == port) {
            return true;
        }
    }
    return false;
}

static bool sourceMatches(const QString &source, const QStringList &localAddresses) {
    // Without our addresses, a rule limited to some addresses still counts.
    if (localAddresses.isEmpty() || source == "any" || source == "0.0.0.0/0" || source == "::/0")
        return true;
    const auto network = QHostAddress::parseSubnet(source.contains('/') ? source : source + "/32");
    if (network.first.isNull())
        return true;
    for (const QString &local : localAddresses) {
        const QHostAddress address(local);
        if (address.protocol() == network.first.protocol() && address.isInSubnet(network))
            return true;
    }
    return false;
}

// ### tuple ### <action> <proto> <dport> <dst> <sport> <src> [<dapp> <sapp>] <direction> [comment=...]
static std::optional<QString> ruleAction(const QString &line, int port, const QString &protocol,
                                         const QStringList &localAddresses) {
    QStringList fields = line.mid(QStringLiteral("### tuple ###").size()).split(' ', Qt::SkipEmptyParts);
    fields.erase(std::remove_if(fields.begin(), fields.end(), [](const QString &f) { return f.startsWith("comment="); }),
                 fields.end());
    if (fields.size() < 7)
        return std::nullopt;
    const QString action = fields.at(0), ruleProtocol = fields.at(1), dport = fields.at(2), source = fields.at(5);
    if (!fields.last().startsWith("in"))
        return std::nullopt;
    if (ruleProtocol != "any" && ruleProtocol != protocol)
        return std::nullopt;
    if (!portMatches(dport, port) || !sourceMatches(source, localAddresses))
        return std::nullopt;
    // Logging variants read "allow_log" or "allow_log-all".
    return action.section('_', 0, 0);
}

std::optional<bool> Firewall::isIncomingPortBlocked(int port, const QString &protocol,
                                                    const QStringList &localAddresses) const {
    if (!QFile::exists(m_configuration))
        return false;
    const auto enabled = readSetting(m_configuration, "ENABLED");
    if (!enabled)
        return std::nullopt;
    if (*enabled != "yes")
        return false;

    const auto rules = readLines(m_userRules);
    if (!rules)
        return std::nullopt;
    // The first rule that fits decides, as in ufw.
    for (const QString &line : *rules) {
        if (!line.startsWith("### tuple ###"))
            continue;
        if (const auto action = ruleAction(line, port, protocol, localAddresses))
            return *action == "deny" || *action == "reject";
    }
    const auto policy = readSetting(m_defaults, "DEFAULT_INPUT_POLICY");
    if (!policy)
        return std::nullopt;
    return policy->toUpper() == "DROP" || policy->toUpper() == "REJECT";
}

QStringList Firewall::localAddresses() {
    static const QRegularExpression virtualInterface("^(docker|br-|veth|virbr|vmnet|vboxnet|lxc|lxd|podman|cni|flannel)");
    QStringList addresses;
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &interface : interfaces) {
        if (!(interface.flags() & QNetworkInterface::IsUp) || (interface.flags() & QNetworkInterface::IsLoopBack)
            || virtualInterface.match(interface.name()).hasMatch())
            continue;
        for (const QNetworkAddressEntry &entry : interface.addressEntries())
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol)
                addresses << entry.ip().toString();
    }
    return addresses;
}
