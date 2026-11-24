import QtQuick
import RealityView

// Keyboard (and on-screen hold buttons) -> manual driving input. Steering is smoothed so a
// digital key behaves like a progressive wheel. Commands are only sent when values change.
Item {
    id: input
    required property VehicleViewModel vehicle

    property bool accelerating: false
    property bool braking: false
    property bool steeringLeft: false
    property bool steeringRight: false
    property real steer: 0

    property real _sentThrottle: -1
    property real _sentBrake: -1
    property real _sentSteer: 99

    signal zoomRequested(real factor)
    signal tiltToggled()

    function frame(dt) {
        const target = steeringLeft ? 1 : (steeringRight ? -1 : 0)
        steer += (target - steer) * Math.min(1, dt * 6)
        if (Math.abs(steer) < 0.01 && target === 0)
            steer = 0
        const t = accelerating ? 1 : 0
        const b = braking ? 1 : 0
        if (t !== _sentThrottle || b !== _sentBrake || Math.abs(steer - _sentSteer) > 0.01) {
            vehicle.setManualInput(t, b, steer)
            _sentThrottle = t
            _sentBrake = b
            _sentSteer = steer
        }
    }

    function setKey(key, down) {
        switch (key) {
        case Qt.Key_W: case Qt.Key_Up: accelerating = down; return true
        case Qt.Key_S: case Qt.Key_Down: braking = down; return true
        case Qt.Key_A: case Qt.Key_Left: steeringLeft = down; return true
        case Qt.Key_D: case Qt.Key_Right: steeringRight = down; return true
        }
        return false
    }

    Keys.onPressed: (event) => {
        if (setKey(event.key, true)) { event.accepted = true; return }
        if (event.isAutoRepeat) return
        switch (event.key) {
        case Qt.Key_Space: vehicle.toggleAutopilot(); break
        case Qt.Key_Q: vehicle.signalTurn(vehicle.turnSignal === 1 ? 0 : 1); break
        case Qt.Key_E: vehicle.signalTurn(vehicle.turnSignal === 2 ? 0 : 2); break
        case Qt.Key_R: vehicle.reset(); break
        case Qt.Key_T: input.tiltToggled(); break
        case Qt.Key_Plus: case Qt.Key_Equal: input.zoomRequested(1.2); break
        case Qt.Key_Minus: input.zoomRequested(1 / 1.2); break
        case Qt.Key_BracketRight: vehicle.adjustCruise(5); break
        case Qt.Key_BracketLeft: vehicle.adjustCruise(-5); break
        default: return
        }
        event.accepted = true
    }
    Keys.onReleased: (event) => {
        if (!event.isAutoRepeat && setKey(event.key, false))
            event.accepted = true
    }
}
