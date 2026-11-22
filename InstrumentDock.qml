import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One instrument instance moves between hosts, keeping its live state and camera.
Item {
    id: dock
    required property string title
    required property Component instrument
    required property Item floatLayer
    required property bool shown
    required property bool floating
    required property bool workVisible
    property string windowName: ""
    property bool bottomFloat: false
    property bool mobile: Qt.platform.os === "android" || Qt.platform.os === "ios"
    signal changePlacement
    signal hideInstrument
    visible: shown && !floating
    implicitHeight: 280
    Window {
        id: toolWindow
        objectName: dock.windowName
        title: "Field Rover · " + dock.title
        flags: Qt.Tool | Qt.WindowTitleHint | Qt.WindowCloseButtonHint
        transientParent: dock.Window.window
        width: 440
        height: dock.implicitHeight + 34
        minimumWidth: 320
        minimumHeight: 230
        visible: dock.shown && dock.floating && !dock.mobile && dock.workVisible && !!transientParent && transientParent.visible
        onClosing: dock.hideInstrument()
        Item {
            id: desktopHost
            anchors.fill: parent
        }
    }
    Item {
        id: tabletHost
        parent: dock.floatLayer
        visible: dock.mobile && dock.floating && dock.shown && dock.workVisible
        width: Math.min(parent.width, 390)
        height: Math.min(parent.height, dock.implicitHeight)
        property real dx: 0
        property real dy: 0
        readonly property real homeX: dock.bottomFloat ? 0 : parent.width - width
        x: Math.max(0, Math.min(parent.width - width, homeX + dx))
        y: Math.max(0, Math.min(parent.height - height, (dock.bottomFloat ? parent.height - height : 0) + dy))
        z: dock.bottomFloat ? 3 : 2
    }
    Rectangle {
        id: panel
        parent: dock.floating ? (dock.mobile ? tabletHost : desktopHost) : dock
        anchors.fill: parent
        visible: dock.shown
        Material.theme: dock.Material.theme
        Material.foreground: dock.Material.foreground
        Material.accent: dock.Material.accent
        Material.background: dock.Material.background
        color: Material.theme === Material.Light ? "#f8fbfb" : "#20292d"
        border.color: "#788c93"
        radius: 3
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 4
            spacing: 2
            RowLayout {
                Layout.fillWidth: true
                Layout.minimumHeight: 56
                Layout.preferredHeight: 56
                Layout.maximumHeight: 56
                spacing: 4
                Label {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 6
                    font.bold: true
                    elide: Text.ElideRight
                    text: dock.title
                    DragHandler {
                        enabled: dock.mobile && dock.floating
                        target: null
                        onTranslationChanged: delta => {
                            tabletHost.dx = tabletHost.x - tabletHost.homeX + delta.x;
                            tabletHost.dy = tabletHost.y - (dock.bottomFloat ? tabletHost.parent.height - tabletHost.height : 0) + delta.y;
                        }
                    }
                }
                FieldButton {
                    objectName: dock.windowName + "Placement"
                    text: dock.floating ? "Dock" : "Float"
                    Accessible.name: text + " " + dock.title
                    onClicked: dock.changePlacement()
                }
                FieldButton {
                    text: "×"
                    Accessible.name: "Hide " + dock.title
                    onClicked: dock.hideInstrument()
                }
            }
            Loader {
                objectName: dock.windowName + "Body"
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: dock.shown && dock.workVisible
                sourceComponent: dock.instrument
            }
        }
    }
}
