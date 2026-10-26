// Client-side title bar with the tabs merged in:
// [icon][tabs ... ][+][minimize][maximize][close]
// Drag empty space to move the window, double-click it to maximize.
import QtQuick
import QtQuick.Window
import Monstar.Host

Rectangle {
    id: bar

    required property TerminalHost host
    required property Window window
    // Tabs on another edge leave the bar to the title.
    property bool showTabs: true
    // A lone tab merges into the bar, which shows its title.
    readonly property bool tabsVisible: showTabs && host.tabs.length > 1

    implicitHeight: 36
    color: tint(palette.window)

    function tint(c) {
        return Qt.rgba(c.r, c.g, c.b, c.a * bar.host.config.backgroundOpacity)
    }

    readonly property bool maximized: window.visibility === Window.Maximized
                                      || window.visibility === Window.FullScreen

    SystemPalette {
        id: palette
        colorGroup: SystemPalette.Active
    }

    // Empty space moves and maximizes the window, as a native title bar does.
    DragHandler {
        target: null
        grabPermissions: PointerHandler.CanTakeOverFromAnything
        onActiveChanged: if (active) bar.window.startSystemMove()
    }

    TapHandler {
        onDoubleTapped: bar.toggleMaximized()
    }

    function toggleMaximized() {
        if (bar.maximized)
            bar.window.showNormal()
        else
            bar.window.showMaximized()
    }

    Image {
        id: icon
        anchors.left: parent.left
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        width: 20
        height: 20
        sourceSize: Qt.size(40, 40)
        source: "qrc:/monstar/dev.rockorager.monstar.svg"
    }

    TabStrip {
        id: tabs
        visible: bar.tabsVisible
        host: bar.host
        location: "top"
        showAddButton: false
        color: "transparent"
        anchors.left: icon.right
        anchors.leftMargin: 8
        anchors.right: addButton.left
        anchors.rightMargin: 8
        height: parent.height
    }

    // Centered in the bar while it fits between the icon and the buttons.
    Text {
        visible: !bar.tabsVisible
        readonly property real minX: icon.x + icon.width + 10
        readonly property real maxRight: addButton.x - 10
        width: Math.min(implicitWidth, maxRight - minX)
        x: Math.max(minX, Math.min((bar.width - width) / 2, maxRight - width))
        anchors.verticalCenter: parent.verticalCenter
        text: bar.host.windowTitle
        elide: Text.ElideRight
        color: palette.windowText
    }

    component BarButton: Rectangle {
        id: button
        property string glyph
        property bool danger: false
        signal activated

        width: 40
        height: parent.height
        color: hover.hovered ? (danger ? "#c42b1c" : Qt.lighter(buttonPalette.window, 1.25)) : "transparent"

        SystemPalette {
            id: buttonPalette
            colorGroup: SystemPalette.Active
        }

        Text {
            anchors.centerIn: parent
            text: button.glyph
            font.pixelSize: 15
            color: hover.hovered && button.danger ? "white" : buttonPalette.windowText
        }

        HoverHandler {
            id: hover
        }

        TapHandler {
            onTapped: button.activated()
        }
    }

    BarButton {
        id: addButton
        anchors.right: minimizeButton.left
        anchors.rightMargin: 8
        glyph: "+"
        onActivated: bar.host.newTab()
    }

    BarButton {
        id: minimizeButton
        anchors.right: maximizeButton.left
        glyph: "\u2013"
        onActivated: bar.window.showMinimized()
    }

    BarButton {
        id: maximizeButton
        anchors.right: closeButton.left
        glyph: bar.maximized ? "\u2750" : "\u25a1"
        onActivated: bar.toggleMaximized()
    }

    BarButton {
        id: closeButton
        anchors.right: parent.right
        glyph: "\u2715"
        danger: true
        onActivated: bar.window.close()
    }
}
