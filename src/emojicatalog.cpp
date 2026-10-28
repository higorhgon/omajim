#include "emojicatalog.h"

#include <QFile>
#include <QRegularExpression>

#include <algorithm>

namespace {

// Where a skin tone goes: the emoji without a tone takes nothing there, or
// the emoji presentation selector.
constexpr char32_t toneSlot = 0;
constexpr char32_t selectorSlot = 1;
constexpr char32_t presentationSelector = 0xFE0F;
constexpr char32_t lightestTone = 0x1F3FB;

// GTK's groups in the picker's sections: people join the smileys, and the
// skin tones and hair styles (2) have none.
int sectionOf(int group) {
    switch (group) {
    case 0: case 1: return 0;
    case 3: return 1;
    case 4: return 2;
    case 5: return 3;
    case 6: return 4;
    case 7: return 5;
    case 8: return 6;
    case 9: return 7;
    }
    return -1;
}

QString fold(const QString &text) {
    QString folded;
    for (const QChar c : text.normalized(QString::NormalizationForm_D))
        if (c.category() != QChar::Mark_NonSpacing)
            folded += c.toLower();
    return folded;
}

QString compose(const QList<char32_t> &pattern, char32_t tone) {
    QString text;
    for (char32_t point : pattern) {
        if (point == toneSlot || point == selectorSlot) {
            point = tone ? tone : point == selectorSlot ? presentationSelector : 0;
            if (!point)
                continue;
        }
        text += QString::fromUcs4(&point, 1);
    }
    return text;
}

bool takesTone(const QList<char32_t> &pattern) {
    return pattern.contains(toneSlot) || pattern.contains(selectorSlot);
}

} // namespace

EmojiCatalog::EmojiCatalog(const QString &settingsFile, const QString &dataFile, QObject *parent)
    : QObject(parent), m_settings(settingsFile, QSettings::IniFormat) {
    QFile file(dataFile);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        static const QRegularExpression space("\\s+");
        while (!file.atEnd()) {
            QString line = QString::fromUtf8(file.readLine());
            if (line.endsWith('\n'))
                line.chop(1);
            const QStringList fields = line.split('\t');
            if (fields.size() < 5)
                continue;
            Entry entry;
            entry.section = sectionOf(fields.at(0).toInt());
            if (entry.section < 0)
                continue;
            for (const QString &point : fields.at(1).split(' ', Qt::SkipEmptyParts))
                entry.pattern << (point == "T" ? toneSlot : point == "S" ? selectorSlot : point.toUInt(nullptr, 16));
            entry.emoji = compose(entry.pattern, 0);
            entry.name = fields.at(2);
            entry.localName = fields.at(3);
            entry.nameWords = fold(entry.name + ' ' + entry.localName).split(space, Qt::SkipEmptyParts);
            entry.keywords = fold(fields.at(4)).split(space, Qt::SkipEmptyParts);
            const int index = m_entries.size();
            m_index.insert(entry.emoji, index);
            if (takesTone(entry.pattern))
                for (char32_t tone = lightestTone; tone < lightestTone + 5; ++tone)
                    m_index.insert(compose(entry.pattern, tone), index);
            m_entries << entry;
        }
    }
    m_recent = m_settings.value("emoji/recent").toStringList();
}

QVariantMap EmojiCatalog::item(const Entry &entry, const QString &shown) const {
    return {{"emoji", shown.isEmpty() ? entry.emoji : shown},
            {"name", entry.name},
            {"localName", entry.localName},
            {"toned", takesTone(entry.pattern)}};
}

const EmojiCatalog::Entry *EmojiCatalog::find(const QString &emoji) const {
    const auto it = m_index.constFind(emoji);
    return it == m_index.constEnd() ? nullptr : &m_entries.at(*it);
}

QVariantList EmojiCatalog::section(int section) const {
    QVariantList items;
    for (const Entry &entry : m_entries)
        if (entry.section == section)
            items << item(entry);
    return items;
}

QVariantList EmojiCatalog::search(const QString &text) const {
    static const QRegularExpression space("\\s+");
    const QString query = fold(text.trimmed());
    const QStringList words = query.split(space, Qt::SkipEmptyParts);
    if (words.isEmpty())
        return {};

    // How well a word matches: 1 a name's word starts with it, 2 a keyword
    // does, 3 either holds it; 0 when nothing does.
    auto rank = [](const Entry &entry, const QString &word) {
        auto starts = [&](const QStringList &list) {
            return std::any_of(list.begin(), list.end(), [&](const QString &w) { return w.startsWith(word); });
        };
        auto holds = [&](const QStringList &list) {
            return std::any_of(list.begin(), list.end(), [&](const QString &w) { return w.contains(word); });
        };
        return starts(entry.nameWords) ? 1 : starts(entry.keywords) ? 2
             : holds(entry.nameWords) || holds(entry.keywords) ? 3 : 0;
    };

    QList<QPair<int, int>> found;
    for (int index = 0; index < m_entries.size(); ++index) {
        const Entry &entry = m_entries.at(index);
        int worst = 0;
        for (const QString &word : words) {
            const int score = rank(entry, word);
            if (!score) {
                worst = 0;
                break;
            }
            worst = std::max(worst, score);
        }
        if (!worst)
            continue;
        // A name that is the search, or starts with it, comes first of all.
        if (fold(entry.localName).startsWith(query) || fold(entry.name).startsWith(query))
            worst = 0;
        found << qMakePair(worst, index);
    }
    std::stable_sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return a.first < b.first; });

    QVariantList items;
    for (const auto &[score, index] : found)
        items << item(m_entries.at(index));
    return items;
}

QStringList EmojiCatalog::tones(const QString &emoji) const {
    const Entry *entry = find(emoji);
    if (!entry || !takesTone(entry->pattern))
        return {};
    QStringList variants;
    for (char32_t tone = lightestTone; tone < lightestTone + 5; ++tone)
        variants << compose(entry->pattern, tone);
    return variants;
}

void EmojiCatalog::use(const QString &emoji) {
    if (emoji.isEmpty())
        return;
    m_recent.removeAll(emoji);
    m_recent.prepend(emoji);
    while (m_recent.size() > recentLimit)
        m_recent.removeLast();
    m_settings.setValue("emoji/recent", m_recent);
    emit recentChanged();
}

QVariantList EmojiCatalog::recent() const {
    QVariantList items;
    for (const QString &emoji : m_recent)
        if (const Entry *entry = find(emoji))
            items << item(*entry, emoji);
    return items;
}
