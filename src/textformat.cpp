#include "textformat.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>

namespace TextFormat {

// The smileys of Pidgin's default theme for XMPP (pidgin/pixmaps/emotes/
// default/24/theme), which Pidgin users write as text, as emoji.
static const QList<std::pair<QString, QStringList>> &textSmileys() {
    static const QList<std::pair<QString, QStringList>> smileys{
        {"🙂", {":)", ":-)", "=)"}},
        {"😃", {":-D", ":-d", ":D", ":d", "=D", "=d"}},
        {"🙁", {":-(", ":("}},
        {"😉", {";-)", ";)", ";^)"}},
        {"😛", {":P", ":p", ":-P", ":-p"}},
        {"😮", {"=-O", "=-o", ":-O", ":-o"}},
        {"😘", {":kiss:", ":-*"}},
        {"😎", {"8-)", "B-)"}},
        {"😳", {":-["}},
        {"😢", {":'-(", ":'("}},
        {"😕", {":-/", ":-\\"}},
        {"😇", {"O:-)", "o:-)"}},
        {"🤐", {":-X"}},
        {"🤑", {":-$"}},
        {"😬", {":-!"}},
        {"😤", {">:o", ">:O"}},
        {"😠", {">:-(", ">:(", "X-(", "x-("}},
        {"👍", {":yes:"}},
        {"👎", {":no:"}},
        {"✋", {":wait:"}},
        {"🌹", {"@->--", ":rose:"}},
        {"📞", {":telephone:"}},
        {"📧", {":email:"}},
        {"💡", {":jabber:"}},
        {"🎂", {":cake:"}},
        {"❤️", {":heart:", ":love:", "<3"}},
        {"💔", {":brokenheart:"}},
        {"🎵", {":music:"}},
        {"🍺", {":beer:"}},
        {"☕", {":coffee:"}},
        {"💰", {":money:"}},
        {"🌙", {":moon:"}},
        {"☀️", {":sun:"}},
        {"⭐", {":star:"}},
        {"😐", {":|", ":-|"}},
        {"🤘", {"\\m/"}},
    };
    return smileys;
}

// Escapes a run of text, with its smileys: a custom one wherever its text
// is, as Pidgin does, and a text one only standing apart, so "http://" or
// "a:b" stay as they are.
// The smileys a message is shown with.
struct Smileys {
    const QHash<QString, QString> &images;
    bool text;
};

static QString escapeWithSmileys(const QString &text, const Smileys &smileys) {
    const QHash<QString, QString> &images = smileys.images;
    static const QList<std::pair<QString, QString>> byLength = [] {
        QList<std::pair<QString, QString>> codes;
        for (const auto &[emoji, texts] : textSmileys())
            for (const QString &code : texts)
                codes.append({code, emoji});
        std::sort(codes.begin(), codes.end(), [](const auto &a, const auto &b) { return a.first.size() > b.first.size(); });
        return codes;
    }();
    QStringList custom = images.keys();
    std::sort(custom.begin(), custom.end(), [](const QString &a, const QString &b) { return a.size() > b.size(); });
    auto endsApart = [&](qsizetype end) {
        return end == text.size() || text.at(end).isSpace() || QStringLiteral(".,!?;").contains(text.at(end));
    };

    QString html;
    for (qsizetype i = 0; i < text.size();) {
        const QStringView rest = QStringView(text).mid(i);
        bool matched = false;
        for (const QString &alt : custom) {
            if (rest.startsWith(alt)) {
                html += images.value(alt);
                i += alt.size();
                matched = true;
                break;
            }
        }
        if (!matched && smileys.text && (i == 0 || text.at(i - 1).isSpace())) {
            for (const auto &[code, emoji] : byLength) {
                if (rest.startsWith(code) && endsApart(i + code.size())) {
                    html += emoji;
                    i += code.size();
                    matched = true;
                    break;
                }
            }
        }
        if (!matched)
            html += QString(text.at(i++)).toHtmlEscaped();
    }
    return html;
}

// Escapes a run of plain text and makes its links clickable.
// Links up to the next space, without the punctuation that usually ends
// the sentence around them.
static const QRegularExpression &linkPattern() {
    static const QRegularExpression link(
        R"re((?:https?://|www\.)[^\s<>"]+[^\s<>".,;:!?)\]'}]|(?:mailto:|xmpp:)[^\s<>"]+[^\s<>".,;:!?)\]'}])re",
        QRegularExpression::CaseInsensitiveOption);
    return link;
}

QList<std::pair<qsizetype, qsizetype>> links(const QString &text) {
    QList<std::pair<qsizetype, qsizetype>> found;
    auto it = linkPattern().globalMatch(text);
    while (it.hasNext()) {
        const auto match = it.next();
        found.append({match.capturedStart(), match.capturedEnd()});
    }
    return found;
}

static QString linkify(const QString &plain, const QString &linkColor, const Smileys &smileys) {
    const QRegularExpression &link = linkPattern();
    QString html;
    qsizetype last = 0;
    auto it = link.globalMatch(plain);
    while (it.hasNext()) {
        const auto match = it.next();
        html += escapeWithSmileys(plain.mid(last, match.capturedStart() - last), smileys);
        const QString text = match.captured();
        const QString target = text.startsWith("www.", Qt::CaseInsensitive) ? "https://" + text : text;
        html += QStringLiteral("<a href=\"%1\" style=\"color:%2\">%3</a>")
                    .arg(QString::fromUtf8(QUrl(target).toEncoded()).toHtmlEscaped(), linkColor, text.toHtmlEscaped());
        last = match.capturedEnd();
    }
    html += escapeWithSmileys(plain.mid(last), smileys);
    return html;
}

// Inline styling of XEP-0393 (Message Styling), as Gajim shows it: *strong*,
// _emphasis_, ~strike~, and `code`. A directive opens only at the start or
// after a space, with text right after it, and closes before a space or the
// end, so names_with_underscores and links stay as they are.
static QString styleInline(const QString &text, const QString &linkColor, const Smileys &smileys) {
    static const QString directives = QStringLiteral("*_~`");
    auto opensAt = [&](qsizetype i) {
        return (i == 0 || text.at(i - 1).isSpace() || QStringLiteral("([{\"'").contains(text.at(i - 1)))
               && i + 1 < text.size() && !text.at(i + 1).isSpace();
    };
    auto closesAt = [&](qsizetype j) {
        return !text.at(j - 1).isSpace()
               && (j + 1 == text.size() || text.at(j + 1).isSpace() || text.at(j + 1).isPunct());
    };

    QString html, plain;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (directives.contains(c) && opensAt(i)) {
            qsizetype close = -1;
            for (qsizetype j = i + 2; j < text.size(); ++j)
                if (text.at(j) == c && closesAt(j)) {
                    close = j;
                    break;
                }
            if (close > 0) {
                html += linkify(plain, linkColor, smileys);
                plain.clear();
                const QString inner = text.mid(i + 1, close - i - 1);
                if (c == '`')
                    html += "<code>" + inner.toHtmlEscaped() + "</code>";
                else {
                    const QString tag = c == '*' ? "b" : c == '_' ? "i" : "s";
                    html += "<" + tag + ">" + styleInline(inner, linkColor, smileys) + "</" + tag + ">";
                }
                i = close;
                continue;
            }
        }
        plain += c;
    }
    return html + linkify(plain, linkColor, smileys);
}

QString textToHtml(const QString &text, const QString &linkColor, const QHash<QString, QString> &images,
                   bool textSmileys) {
    return linkify(text, linkColor, Smileys{images, textSmileys});
}

qsizetype forwardedHeaderLength(const QString &text) {
    // The date as any locale writes it (digits in it), then a name.
    static const QRegularExpression header(R"re(^\[[^\]\n]*\d[^\]\n]*\] [^:\n]{1,100}:[ \n])re");
    const auto match = header.match(text);
    return match.hasMatch() ? match.capturedLength() : 0;
}

QString toHtml(const QString &plain, const QString &linkColor, const QString &quoteColor,
               const QHash<QString, QString> &images, bool textSmileys, bool forwarded) {
    if (const qsizetype header = forwarded ? forwardedHeaderLength(plain) : 0)
        return QStringLiteral("<span style=\"font-weight:600\">%1</span><br>")
                   .arg(plain.left(header - 1).toHtmlEscaped())
               + toHtml(plain.mid(header), linkColor, quoteColor, images, textSmileys);
    const Smileys smileys{images, textSmileys};
    // Block styling: ``` fences preformatted lines, > starts a quote.
    QStringList blocks, preformatted;
    bool inBlock = false;
    for (const QString &line : plain.split('\n')) {
        if (line.startsWith("```")) {
            if (inBlock) {
                blocks << "<pre>" + preformatted.join('\n').toHtmlEscaped() + "</pre>";
                preformatted.clear();
            }
            inBlock = !inBlock;
            continue;
        }
        if (inBlock) {
            preformatted << line;
        } else if (line.startsWith('>')) {
            const QString quoted = line.mid(line.startsWith("> ") ? 2 : 1);
            blocks << QStringLiteral("<span style=\"color:%1\">│ %2</span>").arg(quoteColor, styleInline(quoted, linkColor, smileys));
        } else {
            blocks << styleInline(line, linkColor, smileys);
        }
    }
    // An unclosed fence runs to the end of the message.
    if (inBlock)
        blocks << "<pre>" + preformatted.join('\n').toHtmlEscaped() + "</pre>";

    QString html;
    for (qsizetype i = 0; i < blocks.size(); ++i) {
        const bool block = blocks.at(i).startsWith("<pre>");
        if (i > 0 && !block && !blocks.at(i - 1).startsWith("<pre>"))
            html += "<br>";
        html += blocks.at(i);
    }
    return html;
}

bool isOpenableLink(const QString &url) {
    static const QStringList schemes{"http", "https", "mailto", "xmpp"};
    return schemes.contains(QUrl(url).scheme().toLower());
}

bool isImageName(const QString &fileName) {
    static const QStringList suffixes{"png", "jpg", "jpeg", "gif", "webp", "bmp"};
    return suffixes.contains(QFileInfo(fileName).suffix().toLower());
}

}
