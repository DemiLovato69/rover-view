import QtQuick
import QtQuick.Layouts

RowLayout {
    id: strip
    required property var choices
    property int currentIndex: 0
    readonly property string currentText: choices[currentIndex] || ""
    spacing: 4
    Repeater {
        model: strip.choices
        FieldButton {
            required property int index
            required property string modelData
            text: modelData
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.minimumWidth: 56
            highlighted: strip.currentIndex === index
            Accessible.checkable: true
            Accessible.checked: highlighted
            onClicked: strip.currentIndex = index
        }
    }
}
