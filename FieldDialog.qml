import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: sheet
    default property alias fields: form.data
    property real preferredWidth: 500
    property string primaryText: ""
    property bool primaryEnabled: true
    property string primaryName: ""
    property string secondaryText: ""
    property string secondaryName: ""
    property bool secondaryEnabled: true
    property string dismissText: primaryText ? "Cancel" : "Close"
    property string dismissName: ""
    property bool destructive: false
    property string problem: ""
    signal primaryAction
    signal secondaryAction
    readonly property bool day: Material.theme === Material.Light
    parent: Overlay.overlay
    modal: true
    focus: true
    padding: 14
    spacing: 0
    width: Math.min(preferredWidth, Math.max(1, (parent ? parent.width : preferredWidth + 20) - 20))
    readonly property real visibleBottom: Qt.inputMethod.visible && Qt.inputMethod.keyboardRectangle.height > 0 && Qt.inputMethod.keyboardRectangle.y > 0 ? Math.min(parent ? parent.height : 0, Qt.inputMethod.keyboardRectangle.y) : parent ? parent.height : 0
    height: Math.min(Math.max(1, visibleBottom - 20), head.implicitHeight + form.implicitHeight + topPadding + bottomPadding + actions.implicitHeight)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: Math.max(0, Math.round((visibleBottom - height) / 2))
    closePolicy: Popup.CloseOnEscape
    background: Rectangle {
        color: sheet.day ? "#ffffff" : "#212b30"
        border.color: sheet.day ? "#7d9197" : "#62757f"
        radius: 4
    }
    header: Label {
        id: head
        text: sheet.title
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
        padding: 14
        font.pixelSize: 21
        font.bold: true
        background: Rectangle {
            color: sheet.day ? "#e3edef" : "#2f3e45"
        }
    }
    contentItem: ScrollView {
        id: viewport
        clip: true
        contentWidth: availableWidth
        contentHeight: form.implicitHeight
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            id: form
            width: viewport.availableWidth
            spacing: 6
        }
    }
    function toTop() {
        if (viewport.contentItem)
            viewport.contentItem.contentY = 0;
    }
    function revealFocus() {
        if (!opened)
            return;
        let item = viewport.Window.window?.activeFocusItem;
        for (let p = item; p; p = p.parent)
            if (p === form) {
                const y = item.mapToItem(form, 0, 0).y, f = viewport.contentItem;
                f.contentY = Math.max(0, Math.min(Math.max(0, form.height - viewport.height), Math.min(y, Math.max(f.contentY, y + item.height - viewport.height))));
                break;
            }
    }
    onAboutToShow: Qt.callLater(toTop)
    onVisibleBottomChanged: Qt.callLater(revealFocus)
    Connections {
        target: viewport.Window.window
        function onActiveFocusItemChanged() {
            if (sheet.opened)
                Qt.callLater(sheet.revealFocus);
        }
    }
    footer: Rectangle {
        id: actions
        implicitHeight: buttons.implicitHeight + 12
        color: sheet.day ? "#e3edef" : "#2f3e45"
        ColumnLayout {
            id: buttons
            anchors.fill: parent
            anchors.margins: 6
            spacing: 3
            Label {
                visible: !!sheet.problem
                text: sheet.problem
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                color: sheet.day ? "#8b3316" : "#ffc1a4"
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                FieldButton {
                    objectName: sheet.dismissName
                    text: sheet.dismissText
                    onClicked: sheet.reject()
                }
                Item {
                    Layout.fillWidth: true
                    visible: !sheet.primaryText
                }
                FieldButton {
                    objectName: sheet.secondaryName
                    visible: !!sheet.secondaryText
                    text: sheet.secondaryText
                    enabled: sheet.secondaryEnabled
                    onClicked: sheet.secondaryAction()
                }
                FieldButton {
                    objectName: sheet.primaryName
                    visible: !!sheet.primaryText
                    Layout.fillWidth: true
                    text: sheet.primaryText
                    highlighted: !sheet.destructive
                    danger: sheet.destructive
                    enabled: sheet.primaryEnabled
                    onClicked: sheet.primaryAction()
                }
            }
        }
    }
}
