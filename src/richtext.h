#pragma once

#include <QColor>
#include <QHash>
#include <QList>
#include <QString>

// Formatted messages the way Pidgin sends and shows them: XHTML-IM
// (XEP-0071), with colors, beside the plain body that every client reads.
namespace RichText {

// A stretch of the message field with one formatting.
struct Run {
    QString text;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    bool code = false;
    bool block = false;  // in a code block
    QString color;       // "#rrggbb", or empty
    QString background;
};

struct Message {
    QString body;   // with XEP-0393 markers, for Gajim and the like
    QString xhtml;  // empty when there's nothing the body can't say
};

// The message field's text as it goes out. Bold, italic, strike and code
// become XEP-0393 markers in the body (what Gajim reads), and everything,
// colors and underline too, goes in XHTML-IM the way Pidgin writes it
// (gtkimhtml reads <strong>, <em> and a span's color, background,
// text-decoration and font-family). Custom smileys (shortcut to cid) go in
// as pictures, outside links.
Message compose(const QList<Run> &runs, const QHash<QString, QString> &smileys = {});

// A received XHTML-IM body as rich text for the window. Only text
// formatting gets through: colors, background, bold, italic, underline,
// strike, code, line breaks, links that may be opened, and custom smileys
// (the HTML for each cid given; others show their text). A color hard to
// read on the bubble is adjusted. Empty when it can't be read, or has no
// text, so the plain body shows instead.
QString toHtml(const QString &xhtml, const QString &linkColor, const QHash<QString, QString> &pictures,
               bool textSmileys, const QColor &bubble, const QColor &foreground);

// A color readable on a background: the same, or lightened (on dark) or
// darkened (on light) until it is.
QColor readable(const QColor &color, const QColor &background);

}
