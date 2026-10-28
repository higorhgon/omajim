#include "richtext.h"

#include "textformat.h"

#include <QRegularExpression>
#include <QXmlStreamReader>
#include <algorithm>
#include <cmath>

namespace RichText {

// --- Going out ---------------------------------------------------------

// The XEP-0393 markers of one line: each formatting's stretches, without
// the spaces at their ends (a marker must touch the text), nested so the
// innermost closes first.
static QString markLine(const QString &line, const QList<const Run *> &runs) {
    struct Insert {
        qsizetype at;
        bool open;
        qsizetype other;  // where the stretch ends (an opener) or starts (a closer)
        QString marker;
    };
    QList<Insert> inserts;
    auto mark = [&](auto has, const QString &marker) {
        for (qsizetype i = 0; i < line.size();) {
            if (!has(runs.at(i))) {
                ++i;
                continue;
            }
            qsizetype end = i;
            while (end < line.size() && has(runs.at(end)))
                ++end;
            qsizetype from = i, to = end;
            while (from < to && line.at(from).isSpace())
                ++from;
            while (to > from && line.at(to - 1).isSpace())
                --to;
            if (to > from) {
                inserts << Insert{from, true, to, marker};
                inserts << Insert{to, false, from, marker};
            }
            i = end;
        }
    };
    // Code is as it is: no other marker inside it.
    mark([](const Run *run) { return run->code; }, QStringLiteral("`"));
    mark([](const Run *run) { return run->bold && !run->code; }, QStringLiteral("*"));
    mark([](const Run *run) { return run->italic && !run->code; }, QStringLiteral("_"));
    mark([](const Run *run) { return run->strike && !run->code; }, QStringLiteral("~"));
    std::sort(inserts.begin(), inserts.end(), [](const Insert &a, const Insert &b) {
        if (a.at != b.at)
            return a.at < b.at;
        if (a.open != b.open)
            return !a.open;  // closers first
        return a.other > b.other;  // the inner closer, the outer opener first
    });

    QString marked;
    qsizetype next = 0;
    for (qsizetype i = 0; i <= line.size(); ++i) {
        while (next < inserts.size() && inserts.at(next).at == i)
            marked += inserts.at(next++).marker;
        if (i < line.size())
            marked += line.at(i);
    }
    return marked;
}

static bool formatted(const Run &run) {
    return run.bold || run.italic || run.underline || run.strike || run.code || run.block || !run.color.isEmpty() ||
           !run.background.isEmpty();
}

// The span Pidgin writes for a stretch, and what goes inside it.
static QString openTags(const Run &run) {
    QStringList style;
    if (!run.color.isEmpty())
        style << QStringLiteral("color: %1;").arg(run.color);
    if (!run.background.isEmpty())
        style << QStringLiteral("background: %1;").arg(run.background);
    QStringList decoration;
    if (run.underline)
        decoration << QStringLiteral("underline");
    if (run.strike)
        decoration << QStringLiteral("line-through");
    if (!decoration.isEmpty())
        style << QStringLiteral("text-decoration: %1;").arg(decoration.join(' '));
    if (run.code || run.block)
        style << QStringLiteral("font-family: monospace;");
    QString tags = style.isEmpty() ? QString() : QStringLiteral("<span style=\"%1\">").arg(style.join(' '));
    if (run.bold)
        tags += QStringLiteral("<strong>");
    if (run.italic)
        tags += QStringLiteral("<em>");
    return tags;
}

static QString closeTags(const Run &run) {
    QString tags;
    if (run.italic)
        tags += QStringLiteral("</em>");
    if (run.bold)
        tags += QStringLiteral("</strong>");
    if (!run.color.isEmpty() || !run.background.isEmpty() || run.underline || run.strike || run.code || run.block)
        tags += QStringLiteral("</span>");
    return tags;
}

Message compose(const QList<Run> &runs, const QHash<QString, QString> &smileys) {
    // One run per character, the text in one piece.
    QString text;
    QList<const Run *> of;
    bool anyFormat = false;
    for (const Run &run : runs) {
        text += run.text;
        for (qsizetype i = 0; i < run.text.size(); ++i)
            of << &run;
        anyFormat = anyFormat || (formatted(run) && !run.text.trimmed().isEmpty());
    }
    static const Run plain;
    QStringList shortcuts = smileys.keys();
    std::sort(shortcuts.begin(), shortcuts.end(), [](const QString &a, const QString &b) { return a.size() > b.size(); });

    QStringList bodyLines, xhtmlLines;
    QStringList block;  // code block lines waiting for their fence
    bool anySmiley = false;
    qsizetype start = 0;
    while (start <= text.size()) {
        qsizetype end = text.indexOf('\n', start);
        if (end < 0)
            end = text.size();
        const QString line = text.mid(start, end - start);
        const QList<const Run *> lineRuns = of.mid(start, end - start);
        // A line is in a code block by its text, or by its line break when empty.
        const Run *lineRun = !lineRuns.isEmpty() ? lineRuns.first() : end < of.size() ? of.at(end) : &plain;

        if (lineRun->block) {
            block << line;
            xhtmlLines << QStringLiteral("<span style=\"font-family: monospace;\">%1</span>").arg(line.toHtmlEscaped());
        } else {
            if (!block.isEmpty()) {
                bodyLines << "```\n" + block.join('\n') + "\n```";
                block.clear();
            }
            bodyLines << markLine(line, lineRuns);

            // Custom smileys where their text is, outside links, longest first.
            const auto links = TextFormat::links(line);
            QHash<qsizetype, QString> smileyAt;
            for (qsizetype i = 0; i < line.size();) {
                bool found = false;
                for (const QString &shortcut : std::as_const(shortcuts)) {
                    const bool inLink = std::any_of(links.cbegin(), links.cend(), [&](const auto &link) {
                        return i < link.second && i + shortcut.size() > link.first;
                    });
                    if (!inLink && QStringView(line).mid(i).startsWith(shortcut)) {
                        smileyAt.insert(i, shortcut);
                        i += shortcut.size();
                        found = true;
                        anySmiley = true;
                        break;
                    }
                }
                if (!found)
                    ++i;
            }

            QString xhtml;
            for (qsizetype i = 0; i < line.size();) {
                const Run *run = lineRuns.at(i);
                qsizetype to = i;
                while (to < line.size() && lineRuns.at(to) == run)
                    ++to;
                QString inner;
                for (qsizetype j = i; j < to;) {
                    const QString shortcut = smileyAt.value(j);
                    if (!shortcut.isEmpty() && j + shortcut.size() <= to) {
                        inner += QStringLiteral("<img alt=\"%1\" src=\"cid:%2\"/>")
                                     .arg(shortcut.toHtmlEscaped(), smileys.value(shortcut).toHtmlEscaped());
                        j += shortcut.size();
                    } else {
                        inner += QString(line.at(j++)).toHtmlEscaped();
                    }
                }
                xhtml += openTags(*run) + inner + closeTags(*run);
                i = to;
            }
            xhtmlLines << xhtml;
        }
        start = end + 1;
    }
    if (!block.isEmpty())
        bodyLines << "```\n" + block.join('\n') + "\n```";

    Message message;
    message.body = bodyLines.join('\n');
    if (anyFormat || anySmiley)
        message.xhtml = "<p>" + xhtmlLines.join("<br/>") + "</p>";
    return message;
}

// --- Coming in ---------------------------------------------------------

static double luminance(const QColor &color) {
    auto channel = [](double c) { return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
    return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) + 0.0722 * channel(color.blueF());
}

static double contrast(const QColor &a, const QColor &b) {
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

QColor readable(const QColor &color, const QColor &background) {
    // Pidgin users pick colors for a white window; on a dark bubble a dark
    // blue or black has to come up.
    static const double Enough = 3.5;
    if (!color.isValid() || !background.isValid() || contrast(color, background) >= Enough)
        return color;
    const QColor toward = luminance(background) < 0.4 ? QColor(Qt::white) : QColor(Qt::black);
    QColor adjusted = color;
    for (int step = 1; step <= 10 && contrast(adjusted, background) < Enough; ++step) {
        const double t = step / 10.0;
        adjusted = QColor::fromRgbF(color.redF() + (toward.redF() - color.redF()) * t,
                                    color.greenF() + (toward.greenF() - color.greenF()) * t,
                                    color.blueF() + (toward.blueF() - color.blueF()) * t);
    }
    return adjusted;
}

static QColor parseColor(const QString &value) {
    const QString text = value.trimmed().toLower();
    static const QRegularExpression rgb(R"re(^rgba?\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+))re");
    if (const auto match = rgb.match(text); match.hasMatch())
        return QColor(std::min(255, match.captured(1).toInt()), std::min(255, match.captured(2).toInt()),
                      std::min(255, match.captured(3).toInt()));
    // Only the first word of a "background" shorthand.
    const QColor color = QColor::fromString(text.section(' ', 0, 0));
    return color.isValid() && color.alpha() > 0 ? color : QColor();
}

namespace {
struct Style {
    QColor color;
    QColor background;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    bool code = false;
    bool pre = false;
    bool skip = false;
    QString href;
};
}

static void applyCss(Style &style, const QString &css) {
    for (const QString &declaration : css.split(';')) {
        const QString property = declaration.section(':', 0, 0).trimmed().toLower();
        const QString value = declaration.section(':', 1).trimmed().toLower();
        if (property == "color") {
            style.color = parseColor(value);
        } else if (property == "background" || property == "background-color") {
            style.background = parseColor(value);
        } else if (property == "font-weight") {
            style.bold = value == "bold" || value == "bolder" || value.toInt() >= 600;
        } else if (property == "font-style") {
            style.italic = value == "italic" || value == "oblique";
        } else if (property == "text-decoration") {
            style.underline = value.contains("underline");
            style.strike = value.contains("line-through");
        } else if (property == "font-family") {
            style.code = style.code || value.contains("mono") || value.contains("courier");
        }
    }
}

QString toHtml(const QString &xhtml, const QString &linkColor, const QHash<QString, QString> &pictures,
               bool textSmileys, const QColor &bubble, const QColor &foreground) {
    QXmlStreamReader reader("<omajim>" + xhtml + "</omajim>");
    QList<Style> stack{Style{}};
    QString html;
    bool anyText = false;
    auto lineBreak = [&] {
        if (!html.isEmpty() && !html.endsWith("<br>"))
            html += "<br>";
    };
    auto styled = [&](const Style &style, const QString &inner) {
        QStringList css;
        QColor color = style.color;
        // On a background of their own, plain letters go black or white,
        // whichever reads; the theme's would often be lost on it.
        if (!color.isValid() && style.background.isValid())
            color = contrast(foreground, style.background) >= 4.5 ? foreground
                  : luminance(style.background) > 0.4 ? QColor("#1a1a1a") : QColor("#f5f5f5");
        const QColor behind = style.background.isValid() ? style.background : bubble;
        if (color.isValid())
            css << QStringLiteral("color:%1").arg(readable(color, behind).name());
        if (style.background.isValid())
            css << QStringLiteral("background-color:%1").arg(style.background.name());
        if (style.bold)
            css << QStringLiteral("font-weight:600");
        if (style.italic)
            css << QStringLiteral("font-style:italic");
        QStringList decoration;
        if (style.underline)
            decoration << QStringLiteral("underline");
        if (style.strike)
            decoration << QStringLiteral("line-through");
        if (!decoration.isEmpty())
            css << QStringLiteral("text-decoration:%1").arg(decoration.join(' '));
        QString text = style.code ? "<code>" + inner + "</code>" : inner;
        if (!css.isEmpty())
            text = QStringLiteral("<span style=\"%1\">%2</span>").arg(css.join(';'), text);
        return text;
    };

    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            Style style = stack.last();
            const QString name = reader.name().toString().toLower();
            if (!style.skip) {
                if (name == "script" || name == "style" || name == "head" || name == "title") {
                    style.skip = true;
                } else if (name == "p" || name == "div" || name == "blockquote" || name == "li" || name == "tr" ||
                           (name.size() == 2 && name.at(0) == 'h' && name.at(1).isDigit())) {
                    lineBreak();
                    if (name == "li")
                        html += QStringLiteral("• ");
                } else if (name == "br") {
                    html += "<br>";
                } else if (name == "strong" || name == "b") {
                    style.bold = true;
                } else if (name == "em" || name == "i" || name == "cite") {
                    style.italic = true;
                } else if (name == "u" || name == "ins") {
                    style.underline = true;
                } else if (name == "s" || name == "strike" || name == "del") {
                    style.strike = true;
                } else if (name == "code" || name == "tt" || name == "kbd" || name == "samp") {
                    style.code = true;
                } else if (name == "pre") {
                    lineBreak();
                    style.code = true;
                    style.pre = true;
                } else if (name == "a") {
                    const QString href = reader.attributes().value("href").toString().trimmed();
                    if (TextFormat::isOpenableLink(href))
                        style.href = href;
                } else if (name == "font") {
                    // Old clients' <font color back face>.
                    const auto attributes = reader.attributes();
                    if (attributes.hasAttribute("color"))
                        style.color = parseColor(attributes.value("color").toString());
                    if (attributes.hasAttribute("back"))
                        style.background = parseColor(attributes.value("back").toString());
                } else if (name == "img") {
                    const QString src = reader.attributes().value("src").toString();
                    const QString alt = reader.attributes().value("alt").toString();
                    const QString picture = src.startsWith("cid:") ? pictures.value(src.mid(4)) : QString();
                    html += !picture.isEmpty() ? picture : styled(style, alt.toHtmlEscaped());
                    anyText = anyText || !picture.isEmpty() || !alt.trimmed().isEmpty();
                }
                if (reader.attributes().hasAttribute("style"))
                    applyCss(style, reader.attributes().value("style").toString());
            }
            stack << style;
        } else if (token == QXmlStreamReader::EndElement) {
            if (stack.size() > 1)
                stack.removeLast();
        } else if (token == QXmlStreamReader::Characters) {
            const Style &style = stack.last();
            if (style.skip)
                continue;
            QString text = reader.text().toString();
            if (!style.pre) {
                // XHTML's own line breaks are spaces; <br> breaks lines.
                text.replace('\n', ' ').replace('\t', ' ');
                if (text.trimmed().isEmpty() && (html.isEmpty() || html.endsWith("<br>")))
                    continue;
            }
            anyText = anyText || !text.trimmed().isEmpty();
            QString inner;
            if (!style.href.isEmpty()) {
                inner = QStringLiteral("<a href=\"%1\" style=\"color:%2\">%3</a>")
                            .arg(style.href.toHtmlEscaped(), linkColor, text.toHtmlEscaped());
            } else if (style.code) {
                inner = text.toHtmlEscaped();
            } else {
                inner = TextFormat::textToHtml(text, linkColor, {}, textSmileys);
            }
            if (style.pre)
                inner.replace('\n', "<br>");
            html += styled(style, inner);
        }
    }
    if (reader.hasError() || !anyText)
        return {};
    while (html.endsWith("<br>"))
        html.chop(4);
    return html;
}

}
