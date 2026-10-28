#pragma once

#include <QHash>
#include <QList>
#include <QString>

namespace TextFormat {

// A message as rich text for the window: escaped, so nothing a contact sends
// can turn into markup, with web, mail, and XMPP links made clickable in the
// given color, the styling of XEP-0393 (*bold*, _italic_, ~strike~, `code`,
// ``` blocks, > quotes in the quote color), and line breaks kept. The text
// smileys of Pidgin's XMPP theme, like :) and :beer:, become their emoji, and
// the texts in images (custom smileys) become the HTML given for them; the
// text smileys can be left as they are. A forwarded line's header is set
// in semibold, on a line of its own.
QString toHtml(const QString &plain, const QString &linkColor, const QString &quoteColor = QStringLiteral("#888888"),
               const QHash<QString, QString> &images = {}, bool textSmileys = true, bool forwarded = false);

// A run of text, escaped, with its links made clickable and its smileys,
// but no XEP-0393 styling: for text whose styling came some other way.
QString textToHtml(const QString &text, const QString &linkColor, const QHash<QString, QString> &images = {},
                   bool textSmileys = true);

// How long the "[when] who:" header a forwarded line starts with is, up
// to the space or line break after it; 0 when there's none.
qsizetype forwardedHeaderLength(const QString &text);

// Where the links of a text start and end, the same ones toHtml() makes
// clickable.
QList<std::pair<qsizetype, qsizetype>> links(const QString &text);

// Whether a link from a message may be opened, so a contact can't hand over
// a file:// or other local URL to click.
bool isOpenableLink(const QString &url);

// An image the window can preview, by its file name.
bool isImageName(const QString &fileName);

}
