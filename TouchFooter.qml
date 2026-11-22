import QtQuick
import QtQuick.Controls

DialogButtonBox {
    delegate: FieldButton {
        implicitWidth: Math.max(88, contentItem.implicitWidth + 24)
    }
}
