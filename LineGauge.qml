import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: gauge
    required property var reading
    required property var settings
    readonly property bool day: Material.theme === Material.Light
    readonly property color ink: day ? "#15323c" : "#e7f3f6"
    readonly property color accent: day ? "#00687c" : "#86dcec"
    readonly property bool tracking: Number.isFinite(reading?.rightM)
    readonly property real fullWidth: settings.widthM || 0
    readonly property real centre: settings.offsetM || 0
    readonly property real target: centre + (reading?.target === "Left edge" ? -fullWidth / 2 : reading?.target === "Right edge" ? fullWidth / 2 : 0)
    readonly property real span: Math.max(.5, fullWidth * 1.2, Math.abs(target) * 1.2)
    function side(v) {
        return Math.abs(v) < .0005 ? "ON LINE" : Math.abs(v).toFixed(3) + " m " + (v < 0 ? "LEFT" : "RIGHT");
    }
    color: day ? "#f8fbfb" : "#20292d"
    border.color: "#788c93"
    radius: 3
    implicitWidth: 245
    implicitHeight: 250
    Accessible.role: Accessible.Indicator
    Accessible.name: "Line guidance: " + (tracking ? side(reading.rightM) + " of " + reading.target : reading?.status || "")
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 9
        spacing: 4
        Label {
            text: "LINE FOLLOW"
            font.bold: true
            color: gauge.ink
        }
        Label {
            Layout.fillWidth: true
            elide: Text.ElideRight
            textFormat: Text.PlainText
            color: gauge.ink
            text: (reading.name || "") + "  ·  SEG " + (reading.segment || "—")
        }
        Rectangle {
            id: chart
            objectName: "crossTrackChart"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 68
            clip: true
            color: gauge.day ? "#dfebed" : "#19272d"
            function at(m) {
                return width / 2 + m / gauge.span * (width / 2 - 12);
            }
            Rectangle {
                visible: gauge.fullWidth > 0
                x: chart.at(gauge.centre - gauge.fullWidth / 2 - gauge.target)
                width: chart.at(gauge.fullWidth) - chart.at(0)
                height: parent.height
                color: gauge.day ? "#b7d8ce" : "#2a5149"
                border.color: gauge.day ? "#698a81" : "#81ae9f"
            }
            Rectangle {
                x: chart.at(-gauge.target) - 1
                width: 2
                height: parent.height
                color: gauge.day ? "#896425" : "#efc176"
            }
            Column {
                x: chart.at(0) - 1
                spacing: 3
                Repeater {
                    model: Math.ceil(chart.height / 11)
                    Rectangle {
                        width: 3
                        height: 8
                        color: gauge.accent
                    }
                }
            }
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 2
                text: "↑"
                font.bold: true
                color: gauge.accent
                background: Rectangle {
                    color: chart.color
                }
            }
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                text: Math.abs(gauge.target) < .0005 ? "PLAN / TARGET" : "TARGET"
                font.pixelSize: 10
                font.bold: true
                padding: 2
                color: gauge.accent
                background: Rectangle {
                    color: chart.color
                }
            }
            Rectangle {
                visible: gauge.tracking
                readonly property real end: chart.at(Math.min(gauge.span, Math.max(-gauge.span, gauge.reading.rightM || 0)))
                x: Math.min(end, chart.at(0))
                y: chart.height / 2 - 1
                width: Math.abs(end - chart.at(0))
                height: 2
                color: gauge.ink
            }
            Label {
                x: 4
                y: 2
                text: "L"
                color: gauge.ink
                font.bold: true
            }
            Label {
                anchors.right: parent.right
                anchors.rightMargin: 4
                y: 2
                text: "R"
                color: gauge.ink
                font.bold: true
            }
            Rectangle {
                objectName: "crossTrackRover"
                visible: gauge.tracking
                readonly property real delta: gauge.tracking ? gauge.reading.rightM : 0
                x: chart.at(Math.min(gauge.span, Math.max(-gauge.span, delta))) - width / 2
                y: parent.height / 2 - height / 2
                width: 20
                height: 20
                radius: 10
                color: "#ffffff"
                border.width: 3
                border.color: "#173640"
                Rectangle {
                    anchors.centerIn: parent
                    width: 8
                    height: 8
                    radius: 4
                    color: "#167d91"
                }
            }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.bold: true
            font.pixelSize: 18
            color: gauge.accent
            text: gauge.tracking ? gauge.side(reading.rightM) : reading.status || "Waiting for guidance"
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: 13
            color: gauge.ink
            text: (reading.target || "") + " target · ±" + Number(gauge.span.toFixed(2)) + " m scale" + (gauge.tracking && Math.abs(reading.rightM) > gauge.span ? " · off scale" : "")
        }
        Label {
            visible: gauge.tracking
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: 13
            color: gauge.ink
            text: (reading.inside === undefined ? "" : reading.inside ? "INSIDE trench  ·  " : "OUTSIDE trench  ·  ") + "Plan: " + (Number.isFinite(reading.fromFeatureM) ? gauge.side(reading.fromFeatureM) : "—")
        }
    }
}
