import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: editor
    required property var draft
    property bool mapMode: false
    signal switchView
    readonly property bool day: Material.theme === Material.Light
    color: day ? "#e2eff1" : "#25383e"
    border.color: day ? "#547782" : "#69909b"
    radius: 3
    implicitHeight: body.implicitHeight + 16
    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.margins: 8
        spacing: 2
        Label {
            font.bold: true
            Layout.fillWidth: true
            elide: Text.ElideRight
            textFormat: Text.PlainText
            text: "BUILD LINE    /    " + draft.vertices.length + " points" + (draft.vertices.length ? "   ·   Last: " + draft.label(draft.vertices.length - 1) : "")
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: draft.error || (editor.mapMode ? "Tap a saved point to snap, or tap empty map for a sketch corner." : "Tap saved points in the order the line should run.")
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            FieldButton {
                text: editor.mapMode ? "Use list" : "Use map"
                onClicked: editor.switchView()
            }
            FieldButton {
                text: "Undo"
                enabled: draft.vertices.length > 0
                onClicked: draft.remove(draft.vertices.length - 1)
            }
            FieldButton {
                text: "Cancel"
                onClicked: draft.cancel()
            }
            FieldButton {
                objectName: "reviewLine"
                text: "Finish…"
                highlighted: true
                Layout.fillWidth: true
                enabled: draft.vertices.length >= 2
                onClicked: review.open()
            }
        }
    }
    FieldDialog {
        id: review
        title: "Save feature line"
        primaryText: "Save line"
        primaryName: "saveDraftLine"
        primaryEnabled: draft.name.trim().length > 0 && draft.vertices.length >= 2
        problem: draft.error
        onPrimaryAction: if (draft.save())
            close()
        TextField {
            objectName: "draftLineName"
            Layout.fillWidth: true
            implicitHeight: 56
            placeholderText: "Line name"
            text: draft.name
            onTextEdited: draft.name = text
        }
        PlanSketch {
            Layout.fillWidth: true
            implicitHeight: 90
            numbered: true
            paths: [draft.vertices]
        }
        Label {
            text: "Direction: first point → last point"
        }
        ListView {
            Layout.fillWidth: true
            implicitHeight: Math.min(4, draft.vertices.length) * 56
            clip: true
            model: draft.vertices
            delegate: RowLayout {
                required property int index
                required property var modelData
                width: ListView.view.width
                height: 56
                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: (index + 1) + ".  " + (modelData.id || "Map corner")
                }
                FieldButton {
                    text: "↑"
                    Accessible.name: "Move up"
                    enabled: index > 0
                    onClicked: draft.move(index, -1)
                }
                FieldButton {
                    text: "↓"
                    Accessible.name: "Move down"
                    enabled: index + 1 < draft.vertices.length
                    onClicked: draft.move(index, 1)
                }
                FieldButton {
                    text: "×"
                    Accessible.name: "Remove point"
                    onClicked: draft.remove(index)
                }
            }
        }
    }
}
