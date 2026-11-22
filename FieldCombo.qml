import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

ComboBox {
    id: selector
    implicitHeight: 56
    leftPadding: 12
    rightPadding: 36
    implicitWidth: Math.max(132, contentItem.implicitWidth + leftPadding + rightPadding)
    contentItem: Text {
        text: selector.displayText
        font: selector.font
        textFormat: Text.PlainText
        color: selector.Material.foreground
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    Material.background: Material.theme === Material.Light ? "#f2f7f7" : "#26343a"
    background: Rectangle {
        radius: 3
        border.width: selector.visualFocus ? 2 : 1
        border.color: selector.visualFocus ? selector.Material.accent : "#71858c"
        color: selector.Material.background
    }
    delegate: ItemDelegate {
        required property int index
        required property var modelData
        width: ListView.view ? ListView.view.width : selector.width
        height: 56
        text: selector.textRole ? String(modelData?.[selector.textRole] ?? "") : String(modelData ?? "")
        highlighted: selector.highlightedIndex === index
    }
}
