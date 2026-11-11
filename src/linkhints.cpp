#include "linkhints.h"

#include <QCoreApplication>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>

namespace {

// Vimium's, easy on the fingers.
const QString alphabet = QStringLiteral("sadfjklewcmpgh");

bool editable(const QObject *item)
{
    return (item->inherits("QQuickTextInput") || item->inherits("QQuickTextEdit"))
        && !item->property("readOnly").toBool();
}

bool takesLeftClick(const QObject *object)
{
    return object->property("acceptedButtons").value<Qt::MouseButtons>() & Qt::LeftButton;
}

bool clickable(const QQuickItem *item)
{
    if (item->inherits("QQuickAbstractButton") || editable(item))
        return true;
    // Most mouse areas select or drag; the ones meant to be clicked say so
    // with the hand.
    if (item->inherits("QQuickMouseArea"))
        return takesLeftClick(item)
            && item->property("cursorShape").value<Qt::CursorShape>() == Qt::PointingHandCursor;
    for (const QObject *child : item->children()) {
        if (child->inherits("QQuickTapHandler") && child->property("enabled").toBool() && takesLeftClick(child))
            return true;
    }
    return false;
}

void collect(QQuickItem *item, QQuickItem *root, QRectF clip, QVariantList &found)
{
    if (!item->isVisible() || !item->isEnabled() || item->opacity() <= 0 || item->inherits("QQuickOverlay")
        || item->objectName() == QLatin1String("hintLayer"))
        return;
    // The item's own box: a text field's bounding rect follows its font's
    // metrics, so the label would drift with the fonts installed.
    const QRectF rect = item->mapRectToItem(root, QRectF(0, 0, item->width(), item->height()));
    if (!rect.isEmpty() && clip.contains(rect.center()) && clickable(item)) {
        const QPointF at = (rect & clip).topLeft();
        found.append(QVariantMap{{"item", QVariant::fromValue<QObject *>(item)}, {"x", at.x()}, {"y", at.y()}});
    }
    if (item->clip())
        clip &= rect;
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children)
        collect(child, root, clip, found);
}

}

QStringList LinkHints::labels(int count) const
{
    int length = 1;
    for (int fits = alphabet.size(); fits < count; fits *= alphabet.size())
        ++length;
    QStringList labels;
    for (int i = 0; i < count; ++i) {
        QString label;
        for (int n = i, place = 0; place < length; ++place, n /= alphabet.size())
            label.prepend(alphabet.at(n % alphabet.size()));
        labels.append(label);
    }
    return labels;
}

QVariantList LinkHints::targets(QQuickWindow *window) const
{
    QVariantList found;
    if (!window)
        return found;
    QQuickItem *root = window->contentItem();
    collect(root, root, QRectF(QPointF(0, 0), window->size()), found);
    return found;
}

void LinkHints::click(QQuickItem *item) const
{
    if (!item || !item->window())
        return;
    if (editable(item)) {
        item->forceActiveFocus(Qt::ShortcutFocusReason);
        return;
    }
    QQuickWindow *window = item->window();
    const QPointF at = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    const QPointF global = window->mapToGlobal(at);
    QMouseEvent press(QEvent::MouseButtonPress, at, at, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, at, at, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &release);
}
