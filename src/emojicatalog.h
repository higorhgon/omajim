#pragma once

#include <QHash>
#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QVariantList>

// The emoji the picker offers, from the list bin/emoji-data writes out of
// GTK's (the picker of the Python Omajim): in sections, found by name or
// keyword in English or Portuguese, with skin tones for those that take
// one, and the ones used last first.
class EmojiCatalog : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList recent READ recent NOTIFY recentChanged)

public:
    // The recent ones are kept in the settings file.
    explicit EmojiCatalog(const QString &settingsFile, const QString &dataFile = QStringLiteral(":/emoji.tsv"),
                          QObject *parent = nullptr);

    static constexpr int sectionCount = 8;
    static constexpr int recentLimit = 30;

    // Each emoji as {emoji, name, localName, toned}: toned when it takes a
    // skin tone.
    Q_INVOKABLE QVariantList section(int section) const;
    // The best matches first: names that start with the words, then words in
    // names, then keywords. Case and accents don't matter.
    Q_INVOKABLE QVariantList search(const QString &text) const;
    // The five skin tones of an emoji (or of one of its tones), lightest
    // first; empty when it takes none.
    Q_INVOKABLE QStringList tones(const QString &emoji) const;
    // Puts an emoji first among the recent ones.
    Q_INVOKABLE void use(const QString &emoji);
    QVariantList recent() const;

signals:
    void recentChanged();

private:
    struct Entry {
        int section;
        QString emoji;
        // Code points, with 0 where a skin tone goes.
        QList<char32_t> pattern;
        QString name;
        QString localName;
        QStringList nameWords;
        QStringList keywords;
    };

    QVariantMap item(const Entry &entry, const QString &shown = {}) const;
    const Entry *find(const QString &emoji) const;

    QList<Entry> m_entries;
    QHash<QString, int> m_index;
    QSettings m_settings;
    QStringList m_recent;
};
