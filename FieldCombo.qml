import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

ComboBox {
    id: selector
    implicitHeight: 56
    Material.background: Material.theme === Material.Light ? "#f2f7f7" : "#26343a"
    background: Rectangle {
        radius: 3
        border.width: selector.visualFocus ? 2 : 1
        border.color: selector.visualFocus ? selector.Material.accent : "#71858c"
        color: selector.Material.background
    }
    delegate: ItemDelegate {
        required property int index
        width: ListView.view.width
        height: 56
        text: selector.textAt(index)
        highlighted: selector.highlightedIndex === index
    }
}
