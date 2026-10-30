import QtQuick

// Scrolling a list as ZapFast does (fastframe-scroll): a wheel notch
// glides 120 px, and turned fast each notch goes further; a touchpad goes
// 1.8 times as far as the fingers, and once they lift the list glides on,
// slowing down until it stops. At an edge it simply stops, without going
// past and coming back.
MouseArea {
    id: wheel

    required property Flickable view
    // How far one notch goes, turned slowly; turned fast, up to
    // maxBoost times as far.
    property real step: 120
    property real maxBoost: 5
    property real lastNotch: 0
    property real boost: 1
    // How much further than the fingers a touchpad goes.
    property real touchpadScale: 1.8
    // A notch or a touchpad move, up or down; then, once it stops.
    signal scrolled(bool up)
    signal settled()

    function clamp(y) {
        return Math.max(view.originY - view.topMargin,
                        Math.min(y, view.originY + view.contentHeight + view.bottomMargin - view.height))
    }
    // Moves the list to y, within its edges as they are now: a list
    // guesses the height of lines it hasn't laid out, and learns where it
    // really starts and ends only as they come into view. False once at
    // a real edge: its first or last line laid out, not just guessed.
    function moveTo(y) {
        view.contentY = clamp(y)
        // Again, now that the lines that came into view are laid out: they
        // may have moved the edge further.
        const reached = clamp(y)
        view.contentY = reached
        if (reached === y)
            return true
        if (view.itemAtIndex === undefined || view.count === 0)
            return false
        return view.itemAtIndex(y < reached ? 0 : view.count - 1) === null
    }

    // Declared in a list, it would land in its content; over the list
    // itself it gets the wheel anywhere on it.
    Component.onCompleted: parent = view
    anchors.fill: parent
    acceptedButtons: Qt.NoButton
    onWheel: event => {
        if (event.modifiers & Qt.ControlModifier) {
            event.accepted = false
            return
        }
        // Touchpads say when the fingers touch and lift; a wheel doesn't.
        // Neither the device nor the pixels tell them apart: on Wayland, Qt
        // gives every wheel the seat's "touchpad" once the compositor has
        // touchpad gestures (as Hyprland does), and Qt 6.12 sends a notch
        // with 15 px besides its 120.
        const touchpad = event.phase !== Qt.NoScrollPhase
        if (touchpad)
            fingers(event)
        else if (event.angleDelta.y !== 0)
            notch(event.angleDelta.y / 120 * step)
        else
            event.accepted = false
    }

    function notch(delta) {
        coast.stop()
        // Each quick notch after another goes further than the last.
        const now = Date.now()
        boost = now - lastNotch < 150 ? Math.min(maxBoost, boost * 1.35) : 1
        lastNotch = now
        // A notch while gliding goes on from where the glide was heading.
        const from = glide.running ? glide.to : view.contentY
        glide.stop()
        glide.from = view.contentY
        // Not held to the edges as guessed now: the glide stops at the
        // real ones, on the way.
        glide.to = from - delta * boost
        glide.start()
        scrolled(delta > 0)
    }

    // Where the fingers were lately, for their speed when they lift.
    property var recent: []
    function fingers(event) {
        glide.stop()
        coast.stop()
        const now = Date.now()
        if (event.phase === Qt.ScrollBegin)
            recent = []
        const delta = event.pixelDelta.y !== 0 ? event.pixelDelta.y * touchpadScale
                                               : event.angleDelta.y / 8 * touchpadScale
        if (delta !== 0) {
            moveTo(view.contentY - delta)
            recent.push({time: now, delta: delta})
            scrolled(delta > 0)
        }
        // Only the last 100 ms count: fingers that rested before lifting
        // don't fling the list.
        recent = recent.filter(sample => now - sample.time <= 100)
        if (event.phase !== Qt.ScrollEnd)
            return
        const moved = recent.reduce((sum, sample) => sum + sample.delta, 0)
        const span = recent.length > 1 ? (now - recent[0].time) / 1000 : 0
        recent = []
        coast.speed = span > 0 ? moved / span : 0
        if (Math.abs(coast.speed) > 120)
            coast.start()
        else
            settled()
    }

    // The glide after the fingers lift: its speed, in px a second, decays
    // by e every 0.35 s, and it stops below 40 px a second.
    FrameAnimation {
        id: coast
        property real speed: 0
        onTriggered: {
            const dt = Math.min(0.05, Math.max(0.001, frameTime))
            const atEdge = !wheel.moveTo(wheel.view.contentY - speed * dt)
            speed *= Math.exp(-dt / 0.35)
            if (atEdge || Math.abs(speed) < 40) {
                stop()
                wheel.settled()
            }
        }
    }

    // A notch's glide moves this, and the list follows it within its edges
    // on each frame; at an edge it stops there.
    property real glideY
    onGlideYChanged: {
        if (glide.running && !moveTo(glideY)) {
            glide.stop()
            settled()
        }
    }
    NumberAnimation {
        id: glide
        target: wheel
        property: "glideY"
        duration: 200
        easing.type: Easing.OutCubic
        onFinished: wheel.settled()
    }
}
