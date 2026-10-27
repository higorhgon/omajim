#pragma once

#include <QObject>

// Keeps Omajim to one copy per session: a second launch asks the running
// one, over D-Bus, to show its window, then exits.
class Instance : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.higorhgon.Omajim.Instance")

public:
    using QObject::QObject;

    // Takes the session-wide name, or, when another copy holds it, shows
    // that copy and returns false.
    bool claim();

public slots:
    Q_SCRIPTABLE void Show() { emit showRequested(); }

signals:
    void showRequested();
};
