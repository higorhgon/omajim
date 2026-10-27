#include "textformat.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>

namespace TextFormat {

QString toHtml(const QString &plain, const QString &linkColor) {
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
    html.replace('\n', "<br>");
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
