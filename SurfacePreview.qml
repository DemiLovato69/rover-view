import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Rover.Native 1.0

Rectangle {
    id: preview
    property var mesh: ({})
    property var rover: ({})
    property bool daylight: false
    property real yaw: -25
    property real pitch: 38
    property real magnification: 1
    color: daylight ? "#e3e9eb" : "#182126"
    clip: true
    function zoomBy(factor) {
        magnification = Math.min(10, Math.max(.3, magnification * factor));
    }
    TinItem {
        anchors.fill: parent
        mesh: preview.mesh || ({})
        rover: preview.rover || ({})
        view: ({
                yaw: preview.yaw,
                pitch: preview.pitch,
                zoom: preview.magnification,
                daylight: preview.daylight
            })
    }
    DragHandler {
        target: null
        onTranslationChanged: delta => {
            preview.yaw += delta.x * .45;
            preview.pitch = Math.max(5, Math.min(85, preview.pitch + delta.y * .35));
        }
    }
    PinchHandler {
        target: null
        onScaleChanged: delta => preview.zoomBy(delta)
    }
    Label {
        anchors.left: parent.left
        anchors.top: parent.top
        padding: 5
        color: preview.daylight ? "#18232b" : "#e0e9eb"
        text: preview.mesh && preview.mesh.extent ? "3D   /   TRUE SCALE" : "No design loaded"
    }
    Row {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        FieldButton {
            text: "−"
            Accessible.name: "Zoom surface out"
            onClicked: preview.zoomBy(.8)
        }
        FieldButton {
            text: "+"
            Accessible.name: "Zoom surface in"
            onClicked: preview.zoomBy(1.25)
        }
        FieldButton {
            text: "Reset"
            onClicked: {
                preview.yaw = -25;
                preview.pitch = 38;
                preview.magnification = 1;
            }
        }
    }
}
