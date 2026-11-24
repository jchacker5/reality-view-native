import QtQuick

// A PillButton that reports "held" state, for on-screen pedals and steering.
PillButton {
    id: hold
    signal held(bool down)
    onPressedChanged: hold.held(hold.pressed)
    active: pressed
}
