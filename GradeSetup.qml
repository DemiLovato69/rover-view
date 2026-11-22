import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

FieldDialog {
    id: dlg
    title: "Grade setup"
    preferredWidth: 550
    property var info: ({})
    property bool chooseSurface: false
    property string errorText: ""
    readonly property bool design: mode.currentIndex === 1
    problem: errorText
    primaryText: design ? "Use design" : "Use elevation"
    primaryEnabled: tolerance.acceptableInput && (design ? !!chosen.currentText : elevation.acceptableInput)
    onPrimaryAction: apply(design ? "Surface" : "Manual")
    secondaryText: collector.state.gradePrefs.mode && collector.state.gradePrefs.mode !== "Off" ? "Turn off" : ""
    onSecondaryAction: apply("Off")
    function apply(kind) {
        if (collector.setGrade(kind, kind === "Manual" ? Number(elevation.text) : 0, datum.currentText, kind === "Off" || kind === "Preview" ? 0 : Number(tolerance.text), floatFix.checked, chosen.currentText))
            close();
        else
            errorText = collector.state.notice;
    }
    function openForSurface() {
        chooseSurface = true;
        open();
    }
    function inspect(url) {
        info = collector.inspectSurface(url);
        errorText = info.error || "";
        chosen.currentIndex = 0;
    }
    onAboutToShow: {
        let p = collector.state.gradePrefs;
        mode.currentIndex = chooseSurface || p.mode === "Surface" ? 1 : 0;
        chooseSurface = false;
        datum.currentIndex = p.datum === "Ellipsoid" ? 1 : 0;
        elevation.text = p.elevationM === undefined ? "" : String(p.elevationM);
        tolerance.text = String(p.toleranceM ?? .02);
        floatFix.checked = !!p.allowFloat;
        info = {};
        errorText = "";
        if (p.surface) {
            inspect("job:surface");
            chosen.currentIndex = Math.max(0, chosen.find(p.surface));
        }
    }
    ChoiceStrip {
        id: mode
        choices: ["Manual elevation", "Design surface"]
        Layout.fillWidth: true
    }
    GridLayout {
        visible: !dlg.design
        Layout.fillWidth: true
        columns: 2
        Label {
            text: "Ground elevation (m)"
        }
        Label {
            text: "Height reference"
        }
        TextField {
            id: elevation
            Layout.fillWidth: true
            Layout.minimumWidth: 60
            implicitHeight: 56
            placeholderText: "Elevation"
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            validator: DoubleValidator {
                locale: "C"
                notation: DoubleValidator.StandardNotation
            }
        }
        FieldCombo {
            id: datum
            model: ["CGVD2013", "Ellipsoid"]
            Layout.fillWidth: true
        }
    }
    ColumnLayout {
        visible: dlg.design
        Layout.fillWidth: true
        RowLayout {
            Layout.fillWidth: true
            FieldButton {
                text: "Open LandXML…"
                Layout.fillWidth: true
                onClicked: picker.open()
            }
            FieldButton {
                text: "Practice surface"
                Layout.fillWidth: true
                onClicked: dlg.inspect("")
            }
        }
        FieldCombo {
            id: chosen
            model: dlg.info.names || []
            Layout.fillWidth: true
            displayText: currentText || "Select a surface"
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: dlg.info.epsg ? "Confirm the job matches NAD83(CSRS), EPSG:" + dlg.info.epsg + " · " + dlg.info.datum + " · metres" : "Import a metric NAD83(CSRS) UTM surface with a declared height reference."
        }
        FieldButton {
            text: "Load for preview only"
            Layout.fillWidth: true
            enabled: !!chosen.currentText
            onClicked: dlg.apply("Preview")
        }
    }
    RowLayout {
        Layout.fillWidth: true
        ColumnLayout {
            Layout.fillWidth: true
            Label {
                text: "On-grade tolerance ± (m)"
            }
            TextField {
                id: tolerance
                Layout.fillWidth: true
                Layout.minimumWidth: 60
                implicitHeight: 56
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                validator: DoubleValidator {
                    locale: "C"
                    bottom: 0
                    notation: DoubleValidator.StandardNotation
                }
            }
        }
        CheckBox {
            id: floatFix
            text: "Allow float"
            implicitHeight: 56
        }
    }
    FileDialog {
        id: picker
        title: "Open design surface"
        nameFilters: ["LandXML (*.landxml *.xml *.XML *.LandXML)", "All files (*)"]
        onAccepted: dlg.inspect(selectedFile)
    }
}
