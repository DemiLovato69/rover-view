import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Button {
    id: button
    property bool danger: false
    readonly property bool day: Material.theme === Material.Light
    implicitHeight: 56
    leftPadding: 10
    rightPadding: 10
    implicitWidth: Math.max(56, contentItem.implicitWidth + 24)
    topInset: 2
    bottomInset: 2
    font.pixelSize: 16
    font.bold: highlighted || danger
    contentItem: Label {
        text: button.text
        font: button.font
        textFormat: Text.PlainText
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        color: !button.enabled ? (button.day ? "#5c6b70" : "#a0adb4") : button.highlighted ? (button.day ? "white" : "#112126") : button.danger ? (button.day ? "#922c1c" : "#ffc0b6") : button.Material.foreground
    }
    background: Rectangle {
        radius: 3
        border.width: button.visualFocus ? 3 : 1
        border.color: button.visualFocus ? (button.day ? "#082e4b" : "white") : button.danger ? "#b56d61" : "#71858c"
        color: button.day ? (!button.enabled ? "#dde3e4" : button.highlighted ? (button.down ? "#084556" : "#085e74") : button.down || button.hovered ? "#c8dadd" : "#f2f7f7") : !button.enabled ? "#2a3236" : button.highlighted ? (button.down ? "#87b6c3" : "#b7dbe4") : (button.down || button.hovered ? "#49606a" : "#35424a")
    }
}
