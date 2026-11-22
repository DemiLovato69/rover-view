import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

GridLayout {
    id: bar
    required property bool jobOpen
    required property bool occupying
    required property bool gradeOn
    required property bool surfaceOn
    required property bool hasGradeTarget
    required property bool hasSurface
    required property bool levelOn
    required property bool levelSupported
    signal action(string command)
    readonly property bool compact: width >= 430 && width < 830
    columns: width >= 430 ? 2 : 1
    rowSpacing: 2
    columnSpacing: 10
    component Group: Rectangle {
        Layout.fillWidth: true
        required property string heading
        implicitHeight: bar.compact ? 73 : 58
        radius: 3
        border.color: "#75858d"
        color: Material.theme === Material.Light ? "#e4edef" : "#26343b"
        Label {
            visible: bar.compact
            x: 6
            y: 1
            font.pixelSize: 10
            font.bold: true
            text: parent.heading
        }
    }
    component Tool: FieldButton {
        id: toolButton
        required property string symbol
        Accessible.name: text
        contentItem: ColumnLayout {
            spacing: 0
            ToolGlyph {
                Layout.alignment: Qt.AlignHCenter
                symbol: toolButton.symbol
                ink: toolText.color
            }
            Label {
                id: toolText
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: toolButton.text
                font.pixelSize: bar.compact ? 12 : 14
                font.bold: toolButton.highlighted
                color: !toolButton.enabled ? (toolButton.day ? "#55636a" : "#94a2a7") : toolButton.highlighted ? (toolButton.day ? "white" : "#112126") : toolButton.Material.foreground
            }
        }
        Layout.fillWidth: true
        Layout.minimumWidth: 56
        font.pixelSize: bar.compact ? 13 : 16
        topPadding: 3
        bottomPadding: 3
        leftPadding: 4
        rightPadding: 4
    }
    component Tag: Label {
        Layout.preferredWidth: 45
        visible: !bar.compact
        font.pixelSize: 11
        font.bold: true
        horizontalAlignment: Text.AlignHCenter
    }
    Group {
        heading: "SITE TOOLS"
        Layout.preferredWidth: 460
        RowLayout {
            anchors.fill: parent
            anchors.margins: 1
            anchors.topMargin: bar.compact ? 16 : 1
            spacing: 3
            Tag {
                text: "SITE"
            }
            Tool {
                objectName: "mapNewLine"
                symbol: "line"
                text: "New line"
                enabled: bar.jobOpen && !bar.occupying
                onClicked: bar.action("newLine")
            }
            Tool {
                text: "Follow"
                symbol: "follow"
                enabled: bar.jobOpen
                onClicked: bar.action("follow")
            }
            Tool {
                text: bar.compact ? "Area" : "Coverage"
                symbol: "coverage"
                Accessible.name: "Survey coverage"
                enabled: bar.jobOpen
                onClicked: bar.action("coverage")
            }
            Tool {
                text: "Design"
                symbol: "design"
                enabled: bar.jobOpen && !bar.occupying
                onClicked: bar.action("design")
            }
        }
    }
    Group {
        heading: "INSTRUMENTS"
        Layout.preferredWidth: 320
        RowLayout {
            anchors.fill: parent
            anchors.margins: 1
            anchors.topMargin: bar.compact ? 16 : 1
            spacing: 3
            Tag {
                text: "VIEW"
            }
            Tool {
                objectName: "toggleGrade"
                symbol: "grade"
                text: "Grade"
                enabled: bar.jobOpen && (!bar.occupying || bar.hasGradeTarget)
                highlighted: bar.gradeOn
                Accessible.checkable: true
                Accessible.checked: bar.gradeOn
                onClicked: bar.action("grade")
            }
            Tool {
                objectName: "toggleSurface"
                symbol: "surface"
                text: "3D"
                enabled: bar.jobOpen && (!bar.occupying || bar.hasSurface)
                highlighted: bar.surfaceOn
                Accessible.name: "3D surface"
                Accessible.checkable: true
                Accessible.checked: bar.surfaceOn
                onClicked: bar.action("surface")
            }
            Tool {
                objectName: "toggleBubble"
                symbol: "level"
                text: "Level"
                enabled: bar.levelSupported
                highlighted: bar.levelOn
                Accessible.checkable: true
                Accessible.checked: bar.levelOn
                onClicked: bar.action("level")
            }
        }
    }
}
