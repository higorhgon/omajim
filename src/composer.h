#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class QQuickTextDocument;
class QTextDocument;

// The formatting of the message field, a rich TextArea, as Pidgin's: bold,
// italic, underline, strike, code, code blocks, and text and background
// colors, on what's selected. What goes out comes from runs(), one per
// stretch of a single formatting.
class Composer : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    // Turns a formatting on over a stretch, or off where it all has it
    // already: "bold", "italic", "underline", "strike" or "code".
    Q_INVOKABLE void toggle(QQuickTextDocument *document, int start, int end, const QString &kind);
    // The lines a stretch touches become a code block, or stop being one.
    Q_INVOKABLE void toggleBlock(QQuickTextDocument *document, int start, int end);
    // A color for the letters or behind them; an empty color takes it off.
    Q_INVOKABLE void setColor(QQuickTextDocument *document, int start, int end, const QString &color,
                              bool background);
    // Back to plain text.
    Q_INVOKABLE void clearFormat(QQuickTextDocument *document, int start, int end);
    // What a stretch has: {bold, italic, underline, strike, code, block}
    // true when all of it does.
    Q_INVOKABLE QVariantMap formatOf(QQuickTextDocument *document, int start, int end) const;

    // Text in as text, never as markup, looking like the letter before it.
    Q_INVOKABLE void insertPlain(QQuickTextDocument *document, int position, const QString &text);
    Q_INVOKABLE void setPlainText(QQuickTextDocument *document, const QString &text);
    Q_INVOKABLE QString plainText(QQuickTextDocument *document) const;

    // The text in runs of one formatting each: {text, bold, italic,
    // underline, strike, code, block, color, background}.
    Q_INVOKABLE QVariantList runs(QQuickTextDocument *document) const;

    static QVariantList runsOf(const QTextDocument *document);
};
