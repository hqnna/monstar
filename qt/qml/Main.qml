// The monstar-qt window: a nested Wayland compositor whose clients are
// Monstar terminals, shown as tabs of split panes.
import QtQuick
import QtQuick.Window
import QtWayland.Compositor
import QtWayland.Compositor.XdgShell
import Monstar.Host

MonstarCompositor {
    id: root

    required property TerminalHost host

    WaylandOutput {
        id: output
        sizeFollowsWindow: true
        // Terminals render at an integer scale; Qt scales the result to a
        // fractional desktop scale.
        scaleFactor: Math.max(1, Math.ceil(appWindow.pixelRatio))

        window: Window {
            id: appWindow

            readonly property real pixelRatio: Screen.devicePixelRatio
            readonly property string location: root.host.config.tabsLocation
            readonly property bool verticalTabs: location === "left" || location === "right"
            readonly property bool tabsShown: root.host.tabBarVisible

            width: 1024
            height: 720
            visible: true
            title: root.host.windowTitle
            color: root.host.config.background

            TabStrip {
                id: tabStrip
                host: root.host
                location: appWindow.location
                visible: appWindow.tabsShown
                x: appWindow.location === "right" ? appWindow.width - width : 0
                y: appWindow.location === "bottom" ? appWindow.height - height : 0
                width: appWindow.verticalTabs ? 200 : appWindow.width
                height: appWindow.verticalTabs ? appWindow.height : implicitHeight
            }

            WaylandMouseTracker {
                id: paneArea
                x: appWindow.tabsShown && appWindow.location === "left" ? tabStrip.width : 0
                y: appWindow.tabsShown && appWindow.location === "top" ? tabStrip.height : 0
                width: appWindow.width - (appWindow.tabsShown && appWindow.verticalTabs ? tabStrip.width : 0)
                height: appWindow.height - (appWindow.tabsShown && !appWindow.verticalTabs ? tabStrip.height : 0)
                windowSystemCursorEnabled: !clientCursor.visible

                onWidthChanged: root.host.setPaneArea(width, height)
                onHeightChanged: root.host.setPaneArea(width, height)
                Component.onCompleted: root.host.setPaneArea(width, height)

                Repeater {
                    model: root.host.panes

                    delegate: Item {
                        id: pane

                        required property int paneId
                        required property var shellSurface
                        required property rect geometry
                        required property bool shown
                        required property bool focused
                        required property bool dimmed

                        x: geometry.x
                        y: geometry.y
                        width: geometry.width
                        height: geometry.height
                        visible: shown
                        clip: true

                        ShellSurfaceItem {
                            id: surfaceItem
                            anchors.left: parent.left
                            anchors.top: parent.top
                            shellSurface: pane.shellSurface
                            // The host decides the size; the terminal follows
                            // the configure event.
                            width: pane.width
                            height: pane.height
                            autoCreatePopupItems: true
                            focusOnClick: true
                            onActiveFocusChanged: if (activeFocus) root.host.focusPane(pane.paneId)

                            HoverHandler {
                                enabled: root.host.config.focusFollowsMouse
                                onHoveredChanged: if (hovered && !pane.focused) surfaceItem.takeFocus()
                            }
                        }

                        // Ghostty dims unfocused splits with the background color.
                        Rectangle {
                            anchors.fill: parent
                            visible: pane.dimmed
                            color: root.host.config.unfocusedSplitFill
                            opacity: 1 - root.host.config.unfocusedSplitOpacity
                        }

                        Connections {
                            target: root.host
                            function onFocusRequested(id) {
                                if (id === pane.paneId && pane.shown)
                                    surfaceItem.takeFocus()
                            }
                        }
                    }
                }

                Repeater {
                    model: root.host.dividers

                    delegate: Rectangle {
                        id: divider

                        required property int dividerId
                        required property rect geometry
                        required property bool vertical
                        required property rect area

                        x: geometry.x
                        y: geometry.y
                        width: geometry.width
                        height: geometry.height
                        color: root.host.config.splitDividerColor

                        // A wider grab area than the visible line.
                        MouseArea {
                            anchors.centerIn: parent
                            width: divider.vertical ? 9 : parent.width
                            height: divider.vertical ? parent.height : 9
                            cursorShape: divider.vertical ? Qt.SplitHCursor : Qt.SplitVCursor
                            onPositionChanged: mouse => {
                                if (!pressed)
                                    return
                                const point = mapToItem(paneArea, mouse.x, mouse.y)
                                const ratio = divider.vertical
                                    ? (point.x - divider.area.x) / divider.area.width
                                    : (point.y - divider.area.y) / divider.area.height
                                root.host.setDividerRatio(divider.dividerId, ratio)
                            }
                        }
                    }
                }

                WaylandCursorItem {
                    id: clientCursor
                    x: paneArea.mouseX
                    y: paneArea.mouseY
                    seat: root.defaultSeat
                    visible: surface !== null && paneArea.containsMouse
                }
            }
        }
    }

    XdgShell {
        onToplevelCreated: (toplevel, xdgSurface) => root.host.attachToplevel(toplevel, xdgSurface)
    }

    XdgDecorationManagerV1 {
        preferredMode: XdgToplevel.ServerSideDecoration
    }

    TextInputManagerV3 {}

    Component.onCompleted: host.setCompositor(root)
}
