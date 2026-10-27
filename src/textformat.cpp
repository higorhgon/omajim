#include "textformat.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>

namespace TextFormat {

// Escapes a run of plain text and makes its links clickable.
static QString linkify(const QString &plain, const QString &linkColor) {
    // Links up to the next space, without the punctuation that usually ends
    // the sentence around them.
    static const QRegularExpression link(
        R"re((?:https?://|www\.)[^\s<>"]+[^\s<>".,;:!?)\]'}]|(?:mailto:|xmpp:)[^\s<>"]+[^\s<>".,;:!?)\]'}])re",
        QRegularExpression::CaseInsensitiveOption);

    QString html;
    qsizetype last = 0;
    auto it = link.globalMatch(plain);
    while (it.hasNext()) {
        const auto match = it.next();
        html += plain.mid(last, match.capturedStart() - last).toHtmlEscaped();
        const QString text = match.captured();
        const QString target = text.startsWith("www.", Qt::CaseInsensitive) ? "https://" + text : text;
        html += QStringLiteral("<a href=\"%1\" style=\"color:%2\">%3</a>")
                    .arg(QString::fromUtf8(QUrl(target).toEncoded()).toHtmlEscaped(), linkColor, text.toHtmlEscaped());
        last = match.capturedEnd();
    }
    html += plain.mid(last).toHtmlEscaped();
    return html;
}

// Inline styling of XEP-0393 (Message Styling), as Gajim shows it: *strong*,
// _emphasis_, ~strike~, and `code`. A directive opens only at the start or
// after a space, with text right after it, and closes before a space or the
// end, so names_with_underscores and links stay as they are.
static QString styleInline(const QString &text, const QString &linkColor) {
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
                html += linkify(plain, linkColor);
                plain.clear();
                const QString inner = text.mid(i + 1, close - i - 1);
                if (c == '`')
                    html += "<code>" + inner.toHtmlEscaped() + "</code>";
                else {
                    const QString tag = c == '*' ? "b" : c == '_' ? "i" : "s";
                    html += "<" + tag + ">" + styleInline(inner, linkColor) + "</" + tag + ">";
                }
                i = close;
                continue;
            }
        }
        plain += c;
    }
    return html + linkify(plain, linkColor);
}

QString toHtml(const QString &plain, const QString &linkColor, const QString &quoteColor) {
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
            blocks << QStringLiteral("<span style=\"color:%1\">│ %2</span>").arg(quoteColor, styleInline(quoted, linkColor));
        } else {
            blocks << styleInline(line, linkColor);
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
