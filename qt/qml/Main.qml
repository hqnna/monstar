// The monstar-qt window: a nested Wayland compositor whose clients are
// Monstar terminals, shown as tabs of split panes.
import QtQuick
import QtQuick.Window
import QtQuick.Effects
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
            // window-decoration = client merges tabs into a drawn title bar;
            // none removes the desktop's title bar without drawing one.
            readonly property string decoration: root.host.config.windowDecoration
            readonly property bool frameless: decoration === "client" || decoration === "none"
            readonly property bool titleBarShown: decoration === "client"
            readonly property bool tabsMerged: titleBarShown && location === "top"
            readonly property bool tabsShown: root.host.tabBarVisible && !tabsMerged
            readonly property real top: titleBarShown ? titleBar.height : 0
            // Frameless windows draw their own rounded corners: the content is
            // masked to a rounded rectangle, and the desktop blurs the same
            // shape. Maximized windows stay square.
            readonly property bool rounded: frameless && root.host.config.windowCornerRadius > 0
                                            && visibility !== Window.Maximized
                                            && visibility !== Window.FullScreen

            width: 1024
            height: 720
            visible: true
            flags: frameless ? (Qt.Window | Qt.FramelessWindowHint) : Qt.Window
            title: root.host.windowTitle
            color: rounded ? "transparent" : root.host.config.translucentBackground
            Component.onCompleted: root.host.setWindow(appWindow)
            onRoundedChanged: root.host.setWindowCornerRadius(rounded ? root.host.config.windowCornerRadius : 0)

            Item {
                id: content
                anchors.fill: parent
                layer.enabled: appWindow.rounded
                layer.effect: MultiEffect {
                    maskEnabled: true
                    maskSource: cornerMask
                    maskThresholdMin: 0.5
                    maskSpreadAtMin: 1.0
                }

                Rectangle {
                    anchors.fill: parent
                    visible: appWindow.rounded
                    color: root.host.config.translucentBackground
                }

                TitleBar {
                    id: titleBar
                    host: root.host
                    window: appWindow
                    visible: appWindow.titleBarShown
                    showTabs: appWindow.tabsMerged
                    width: appWindow.width
                    height: implicitHeight
                }

                TabStrip {
                    id: tabStrip
                    host: root.host
                    location: appWindow.location
                    visible: appWindow.tabsShown
                    x: appWindow.location === "right" ? appWindow.width - width : 0
                    y: appWindow.location === "bottom" ? appWindow.height - height : appWindow.top
                    width: appWindow.verticalTabs ? 200 : appWindow.width
                    height: appWindow.verticalTabs ? appWindow.height - appWindow.top : implicitHeight
                }

                WaylandMouseTracker {
                    id: paneArea
                    x: appWindow.tabsShown && appWindow.location === "left" ? tabStrip.width : 0
                    y: appWindow.top + (appWindow.tabsShown && appWindow.location === "top" ? tabStrip.height : 0)
                    width: appWindow.width - (appWindow.tabsShown && appWindow.verticalTabs ? tabStrip.width : 0)
                    height: appWindow.height - appWindow.top - (appWindow.tabsShown && !appWindow.verticalTabs ? tabStrip.height : 0)
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

            Rectangle {
                id: cornerMask
                anchors.fill: parent
                radius: root.host.config.windowCornerRadius
                visible: false
                layer.enabled: true
            }

            // Without the desktop's frame, the window edges resize it.
            Repeater {
                model: appWindow.frameless && appWindow.visibility !== Window.Maximized ? [
                    { edges: Qt.TopEdge | Qt.LeftEdge, x: 0, y: 0, w: 8, h: 8, cursor: Qt.SizeFDiagCursor },
                    { edges: Qt.TopEdge | Qt.RightEdge, x: -8, y: 0, w: 8, h: 8, cursor: Qt.SizeBDiagCursor },
                    { edges: Qt.BottomEdge | Qt.LeftEdge, x: 0, y: -8, w: 8, h: 8, cursor: Qt.SizeBDiagCursor },
                    { edges: Qt.BottomEdge | Qt.RightEdge, x: -8, y: -8, w: 8, h: 8, cursor: Qt.SizeFDiagCursor },
                    { edges: Qt.TopEdge, x: 8, y: 0, w: -16, h: 4, cursor: Qt.SizeVerCursor },
                    { edges: Qt.BottomEdge, x: 8, y: -4, w: -16, h: 4, cursor: Qt.SizeVerCursor },
                    { edges: Qt.LeftEdge, x: 0, y: 8, w: 4, h: -16, cursor: Qt.SizeHorCursor },
                    { edges: Qt.RightEdge, x: -4, y: 8, w: 4, h: -16, cursor: Qt.SizeHorCursor }
                ] : []

                // Negative offsets and sizes are relative to the far edge.
                delegate: MouseArea {
                    required property var modelData
                    x: modelData.x < 0 ? appWindow.width + modelData.x : modelData.x
                    y: modelData.y < 0 ? appWindow.height + modelData.y : modelData.y
                    width: modelData.w < 0 ? appWindow.width + modelData.w : modelData.w
                    height: modelData.h < 0 ? appWindow.height + modelData.h : modelData.h
                    cursorShape: modelData.cursor
                    onPressed: appWindow.startSystemResize(modelData.edges)
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
