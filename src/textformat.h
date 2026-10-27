#pragma once

#include <QString>

namespace TextFormat {

// A message as rich text for the window: escaped, so nothing a contact sends
// can turn into markup, with web, mail, and XMPP links made clickable in the
// given color, and line breaks kept.
QString toHtml(const QString &plain, const QString &linkColor);

// Whether a link from a message may be opened, so a contact can't hand over
// a file:// or other local URL to click.
bool isOpenableLink(const QString &url);

// An image the window can preview, by its file name.
bool isImageName(const QString &fileName);

}
