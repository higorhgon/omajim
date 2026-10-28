#include "composer.h"

#include <QQuickTextDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <functional>

// Marks Qt has no format of its own for.
static const int CodeProperty = QTextFormat::UserProperty + 1;
static const int BlockProperty = QTextFormat::UserProperty + 2;
// A foreground set only so text reads on a background picked for it.
static const int AutoForegroundProperty = QTextFormat::UserProperty + 3;

static const QStringList Monospace{QStringLiteral("monospace")};

static QTextDocument *documentOf(QQuickTextDocument *document) {
    return document ? document->textDocument() : nullptr;
}

// Changes the format of every piece of text in a stretch, piece by piece,
// so a format can lose a property as well as gain one.
static void eachFormat(QTextDocument *document, int start, int end, const std::function<void(QTextCharFormat &)> &change) {
    if (!document || start >= end)
        return;
    QTextCursor cursor(document);
    cursor.beginEditBlock();
    for (QTextBlock block = document->findBlock(start); block.isValid() && block.position() < end; block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            const int from = std::max(start, fragment.position());
            const int to = std::min(end, fragment.position() + fragment.length());
            if (from >= to)
                continue;
            QTextCharFormat format = fragment.charFormat();
            change(format);
            cursor.setPosition(from);
            cursor.setPosition(to, QTextCursor::KeepAnchor);
            cursor.setCharFormat(format);
        }
    }
    cursor.endEditBlock();
}

static bool has(const QTextCharFormat &format, const QString &kind) {
    if (kind == "bold")
        return format.fontWeight() >= QFont::DemiBold;
    if (kind == "italic")
        return format.fontItalic();
    if (kind == "underline")
        return format.fontUnderline();
    if (kind == "strike")
        return format.fontStrikeOut();
    if (kind == "code")
        return format.property(CodeProperty).toBool();
    return false;
}

static void set(QTextCharFormat &format, const QString &kind, bool on) {
    if (kind == "bold") {
        format.setFontWeight(on ? QFont::Bold : QFont::Normal);
    } else if (kind == "italic") {
        format.setFontItalic(on);
    } else if (kind == "underline") {
        format.setFontUnderline(on);
    } else if (kind == "strike") {
        format.setFontStrikeOut(on);
    } else if (kind == "code") {
        if (on) {
            format.setProperty(CodeProperty, true);
            format.setFontFamilies(Monospace);
        } else {
            format.clearProperty(CodeProperty);
            format.clearProperty(QTextFormat::FontFamilies);
        }
    }
}

QVariantMap Composer::formatOf(QQuickTextDocument *quick, int start, int end) const {
    static const QStringList kinds{"bold", "italic", "underline", "strike", "code"};
    QVariantMap all;
    QTextDocument *document = documentOf(quick);
    if (!document)
        return all;
    QList<QTextCharFormat> formats;
    bool block = true;
    if (start >= end) {
        // At the cursor: how the letter before it looks.
        QTextCursor cursor(document);
        cursor.setPosition(std::clamp(start, 0, document->characterCount() - 1));
        formats << cursor.charFormat();
        block = cursor.blockFormat().property(BlockProperty).toBool();
    } else {
        for (QTextBlock b = document->findBlock(start); b.isValid() && b.position() < end; b = b.next()) {
            block = block && b.blockFormat().property(BlockProperty).toBool();
            for (auto it = b.begin(); !it.atEnd(); ++it) {
                const QTextFragment fragment = it.fragment();
                if (fragment.position() < end && fragment.position() + fragment.length() > start)
                    formats << fragment.charFormat();
            }
        }
    }
    for (const QString &kind : kinds)
        all.insert(kind, !formats.isEmpty() && std::all_of(formats.cbegin(), formats.cend(),
                                                           [&](const QTextCharFormat &f) { return has(f, kind); }));
    all.insert("block", block);
    return all;
}

void Composer::toggle(QQuickTextDocument *quick, int start, int end, const QString &kind) {
    const bool on = !formatOf(quick, start, end).value(kind).toBool();
    eachFormat(documentOf(quick), start, end, [&](QTextCharFormat &format) { set(format, kind, on); });
}

void Composer::toggleBlock(QQuickTextDocument *quick, int start, int end) {
    QTextDocument *document = documentOf(quick);
    if (!document)
        return;
    const bool on = !formatOf(quick, start, std::max(start, end)).value("block").toBool();
    QTextCursor cursor(document);
    cursor.beginEditBlock();
    QTextBlock last = document->findBlock(std::max(start, end));
    for (QTextBlock block = document->findBlock(start); block.isValid(); block = block.next()) {
        QTextBlockFormat format = block.blockFormat();
        if (on)
            format.setProperty(BlockProperty, true);
        else
            format.clearProperty(BlockProperty);
        cursor.setPosition(block.position());
        cursor.setBlockFormat(format);
        // Its letters in monospace too, as it will look.
        cursor.setPosition(block.position());
        cursor.setPosition(block.position() + block.length() - 1, QTextCursor::KeepAnchor);
        QTextCharFormat letters;
        letters.setFontFamilies(Monospace);
        if (on)
            cursor.mergeCharFormat(letters);
        else
            eachFormat(document, block.position(), block.position() + block.length() - 1, [](QTextCharFormat &format) {
                if (!format.property(CodeProperty).toBool())
                    format.clearProperty(QTextFormat::FontFamilies);
            });
        if (block == last)
            break;
    }
    cursor.endEditBlock();
}

void Composer::setColor(QQuickTextDocument *quick, int start, int end, const QString &color, bool background) {
    eachFormat(documentOf(quick), start, end, [&](QTextCharFormat &format) {
        if (!background) {
            format.clearProperty(AutoForegroundProperty);
            if (color.isEmpty())
                format.clearForeground();
            else
                format.setForeground(QColor(color));
        } else if (!color.isEmpty()) {
            format.setBackground(QColor(color));
            // Text that had no color of its own goes dark on the light
            // colors offered, here and for whoever gets it.
            if (!format.hasProperty(QTextFormat::ForegroundBrush) || format.property(AutoForegroundProperty).toBool()) {
                format.setForeground(QColor(QColor(color).lightness() > 110 ? "#000000" : "#ffffff"));
                format.setProperty(AutoForegroundProperty, true);
            }
        } else {
            format.clearBackground();
            if (format.property(AutoForegroundProperty).toBool()) {
                format.clearForeground();
                format.clearProperty(AutoForegroundProperty);
            }
        }
    });
}

void Composer::clearFormat(QQuickTextDocument *quick, int start, int end) {
    QTextDocument *document = documentOf(quick);
    eachFormat(document, start, end, [](QTextCharFormat &format) { format = QTextCharFormat(); });
    if (!document)
        return;
    // No line of it in a code block either.
    QTextCursor cursor(document);
    for (QTextBlock block = document->findBlock(start); block.isValid() && block.position() <= end; block = block.next()) {
        QTextBlockFormat format = block.blockFormat();
        format.clearProperty(BlockProperty);
        cursor.setPosition(block.position());
        cursor.setBlockFormat(format);
    }
}

void Composer::insertPlain(QQuickTextDocument *quick, int position, const QString &text) {
    QTextDocument *document = documentOf(quick);
    if (!document)
        return;
    QTextCursor cursor(document);
    cursor.setPosition(std::clamp(position, 0, document->characterCount() - 1));
    cursor.insertText(text);
}

void Composer::setPlainText(QQuickTextDocument *quick, const QString &text) {
    if (QTextDocument *document = documentOf(quick))
        document->setPlainText(text);
}

QString Composer::plainText(QQuickTextDocument *quick) const {
    QTextDocument *document = documentOf(quick);
    return document ? document->toPlainText() : QString();
}

QVariantList Composer::runs(QQuickTextDocument *quick) const {
    return runsOf(documentOf(quick));
}

QVariantList Composer::runsOf(const QTextDocument *document) {
    QVariantList runs;
    if (!document)
        return runs;
    auto add = [&runs](QVariantMap run) {
        // One run for neighbors that look the same.
        if (!runs.isEmpty()) {
            QVariantMap last = runs.last().toMap();
            QVariantMap a = last, b = run;
            a.remove("text");
            b.remove("text");
            if (a == b) {
                last.insert("text", last.value("text").toString() + run.value("text").toString());
                runs.last() = last;
                return;
            }
        }
        runs << run;
    };
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        const bool inBlock = block.blockFormat().property(BlockProperty).toBool();
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            const QTextCharFormat format = fragment.charFormat();
            QString text = fragment.text();
            text.replace(QChar::LineSeparator, '\n').replace(QChar::Nbsp, ' ');
            add({{"text", text},
                 {"bold", has(format, "bold")},
                 {"italic", has(format, "italic")},
                 {"underline", has(format, "underline")},
                 {"strike", has(format, "strike")},
                 {"code", has(format, "code")},
                 {"block", inBlock},
                 {"color", format.hasProperty(QTextFormat::ForegroundBrush) ? format.foreground().color().name() : QString()},
                 {"background", format.hasProperty(QTextFormat::BackgroundBrush) ? format.background().color().name() : QString()}});
        }
        if (block.next().isValid())
            add({{"text", QStringLiteral("\n")}, {"bold", false}, {"italic", false}, {"underline", false},
                 {"strike", false}, {"code", false}, {"block", inBlock}, {"color", QString()}, {"background", QString()}});
    }
    return runs;
}
