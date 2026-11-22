import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material

Rectangle {
    id: instrument
    required property var grade
    property Component levelGauge
    readonly property bool available: Number.isFinite(grade.cutMinusFillM)
    readonly property real span: 0.250
    readonly property real tolerance: Math.min(span, grade.gradeToleranceM || 0)
    readonly property real deviation: available ? grade.cutMinusFillM : 0
    readonly property bool day: Material.theme === Material.Light
    readonly property color muted: day ? "#415a63" : "#b2c0c6"
    readonly property color signalColor: !available ? muted : grade.gradeStatus === "CUT" ? (day ? "#944000" : "#ffac55") : grade.gradeStatus === "FILL" ? (day ? "#115d8c" : "#85d1ff") : (day ? "#186633" : "#96ea9b")
    color: Material.background
    border.color: "#68757b"
    radius: 3
    implicitHeight: Math.max(210, readout.implicitHeight + 24)
    implicitWidth: 390
    Accessible.role: Accessible.Indicator
    Accessible.name: available ? grade.gradeStatus + ", " + Math.abs(deviation).toFixed(3) + " metres" : grade.gradeStatus || "Grade unavailable"

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 12
        Item {
            id: ruler
            Layout.preferredWidth: instrument.width >= 370 ? 178 : 142
            Layout.fillHeight: true
            readonly property real travel: Math.max(0, height - 18)
            function level(value) {
                return 9 + travel * (1 - value / instrument.span) / 2;
            }
            Rectangle {
                id: track
                objectName: "gradeTrack"
                x: 42
                y: 9
                width: parent.width - x - 8
                height: ruler.travel
                opacity: instrument.available ? 1 : 0.22
                gradient: Gradient {
                    GradientStop {
                        position: 0
                        color: "#c74b0c"
                    }
                    GradientStop {
                        position: 0.49
                        color: "#f9c56d"
                    }
                    GradientStop {
                        position: 0.51
                        color: "#9cd9ee"
                    }
                    GradientStop {
                        position: 1
                        color: "#1769aa"
                    }
                }
                Rectangle {
                    y: parent.height * (1 - instrument.tolerance / instrument.span) / 2
                    height: parent.height * instrument.tolerance / instrument.span
                    width: parent.width
                    color: "#69c583"
                    border.width: height >= 2 ? 1 : 0
                    border.color: "#174d36"
                }
            }
            Repeater {
                model: 51
                Item {
                    required property int index
                    readonly property bool major: index % 5 === 0
                    y: 9 + ruler.travel * index / 50
                    width: ruler.width
                    Rectangle {
                        x: track.x
                        width: index === 25 ? track.width : major ? 19 : 8
                        height: major ? 2 : 1
                        color: "#162125"
                    }
                    Label {
                        visible: major && (ruler.travel >= 160 || index % 10 === 0 || index === 25)
                        width: track.x - 4
                        y: -height / 2
                        horizontalAlignment: Text.AlignRight
                        color: instrument.Material.foreground
                        font.pixelSize: 11
                        font.bold: major
                        text: index === 25 ? "0" : (index < 25 ? "+" : "−") + Math.abs(25 - index) * 10
                    }
                }
            }
            Rectangle {
                objectName: "gradePointer"
                visible: instrument.available
                x: track.x - 3
                width: track.width + 9
                height: 7
                y: ruler.level(Math.max(-instrument.span, Math.min(instrument.span, instrument.deviation))) - height / 2
                color: "white"
                border.width: 2
                border.color: "#101518"
            }
        }
        ColumnLayout {
            id: readout
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 4
            Label {
                color: instrument.signalColor
                font.bold: true
                font.pixelSize: instrument.width >= 370 ? 27 : 22
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: !instrument.grade.gradeMode ? "GRADE OFF" : !instrument.available ? "NO GUIDANCE" : instrument.grade.gradeStatus === "CUT" ? "▼  CUT" : instrument.grade.gradeStatus === "FILL" ? "▲  FILL" : "◆  ON GRADE"
            }
            Label {
                visible: instrument.available
                Layout.fillWidth: true
                minimumPixelSize: 18
                fontSizeMode: Text.Fit
                font.family: "monospace"
                font.bold: true
                font.pixelSize: 34
                color: instrument.Material.foreground
                text: Math.abs(instrument.deviation).toFixed(3) + " m"
            }
            Label {
                visible: instrument.available && Math.abs(instrument.deviation) > instrument.span
                text: "OUT OF SCALE"
                color: instrument.signalColor
                font.bold: true
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: instrument.Material.foreground
                text: instrument.available ? "Ground  " + grade.gradeGroundM.toFixed(3) + " m\nTarget    " + grade.gradeTargetM.toFixed(3) + " m" : grade.gradeStatus || "Choose Design to set a target."
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: instrument.muted
                visible: !!grade.gradeMode
                font.pixelSize: 13
                text: "± " + (grade.gradeToleranceM || 0).toFixed(3) + " m   ·   " + (grade.gradeDatum || "")
            }
            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                color: instrument.muted
                visible: !!grade.gradeMode
                text: grade.gradeSurface || "Manual target"
            }
            Loader {
                Layout.fillWidth: true
                visible: !!instrument.levelGauge
                sourceComponent: instrument.levelGauge
            }
        }
    }
}
