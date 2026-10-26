// Tab strip along any window edge. Click a tab to select it, middle-click
// or press its close button to close it, and press + for a new tab.
import QtQuick
import Monstar.Host

Rectangle {
    id: strip

    required property TerminalHost host
    // The window edge the strip is on: top, bottom, left, or right.
    property string location: "top"
    readonly property bool vertical: location === "left" || location === "right"
    // The merged title bar draws its own new-tab button and background.
    property bool showAddButton: true

    implicitHeight: 34
    color: tint(palette.window)

    // Backgrounds share the terminals' background-opacity.
    function tint(c) {
        return Qt.rgba(c.r, c.g, c.b, c.a * strip.host.config.backgroundOpacity)
    }
    readonly property real addExtent: showAddButton ? (vertical ? addButton.height : addButton.width) : 0
    readonly property int tabExtent: vertical ? 30 : Math.min(220, Math.max(90, (width - addExtent) / Math.max(1, strip.host.tabs.length)))

    SystemPalette {
        id: palette
        colorGroup: SystemPalette.Active
    }

    ListView {
        id: list
        anchors.left: parent.left
        anchors.top: parent.top
        width: strip.vertical ? parent.width : Math.min(contentWidth, parent.width - strip.addExtent)
        height: strip.vertical ? Math.min(contentHeight, parent.height - strip.addExtent) : parent.height
        orientation: strip.vertical ? ListView.Vertical : ListView.Horizontal
        interactive: false
        clip: true
        model: strip.host.tabs
        currentIndex: strip.host.currentTab
        onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)

        delegate: Rectangle {
            id: tab

            required property var modelData
            required property int index

            width: strip.vertical ? list.width : strip.tabExtent
            height: strip.vertical ? strip.tabExtent : list.height
            color: strip.tint(modelData.active ? palette.base : (hover.hovered ? Qt.lighter(palette.window, 1.08) : palette.window))

            // Accent on the edge facing the terminals.
            Rectangle {
                visible: tab.modelData.active
                color: palette.highlight
                width: strip.vertical ? 2 : parent.width
                height: strip.vertical ? parent.height : 2
                x: strip.location === "left" ? parent.width - width : 0
                y: strip.location === "top" ? parent.height - height : 0
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.right: close.left
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                text: tab.modelData.title
                elide: Text.ElideRight
                color: tab.modelData.active ? palette.text : palette.windowText
                opacity: tab.modelData.active ? 1 : 0.75
            }

            HoverHandler {
                id: hover
            }

            TapHandler {
                acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                onTapped: (point, button) => {
                    if (button === Qt.MiddleButton)
                        strip.host.closeTab(tab.index)
                    else
                        strip.host.activateTab(tab.index)
                }
            }

            Text {
                id: close
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: "\u00d7"
                font.pixelSize: 16
                color: palette.windowText
                opacity: closeHover.hovered ? 1 : (hover.hovered || tab.modelData.active ? 0.6 : 0)

                HoverHandler {
                    id: closeHover
                }

                TapHandler {
                    onTapped: strip.host.closeTab(tab.index)
                }
            }
        }
    }

    Rectangle {
        id: addButton
        visible: strip.showAddButton
        width: strip.vertical ? strip.width : 34
        height: strip.vertical ? 30 : strip.height
        x: strip.vertical ? 0 : list.width
        y: strip.vertical ? list.height : 0
        color: strip.tint(addHover.hovered ? Qt.lighter(palette.window, 1.08) : palette.window)

        Text {
            anchors.centerIn: parent
            text: "+"
            font.pixelSize: 18
            color: palette.windowText
        }

        HoverHandler {
            id: addHover
        }

        TapHandler {
            onTapped: strip.host.newTab()
        }
    }
}
