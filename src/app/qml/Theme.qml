pragma Singleton
import QtQuick

// Design tokens. One place to change the look; components never hard-code colours.
QtObject {
    readonly property color bg: "#0b0d10"
    readonly property color panel: "#111419"
    readonly property color card: "#171b21"
    readonly property color cardBorder: "#232934"
    readonly property color text: "#e8ebf0"
    readonly property color muted: "#8a93a3"
    readonly property color faint: "#4a5262"
    readonly property color accent: "#3d7bff"
    readonly property color good: "#3ecf8e"
    readonly property color warn: "#ffb020"
    readonly property color bad: "#ff4d4f"
    readonly property color road: "#1c2026"

    readonly property int radius: 12
    readonly property int gap: 12
    readonly property string mono: Qt.platform.os === "osx" ? "Menlo" : "monospace"
}
