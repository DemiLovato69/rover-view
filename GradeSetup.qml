import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Controls.Material

Dialog {
    id: dlg
    title: "Grade setup"
    modal: true
    anchors.centerIn: parent
    width: Math.min(parent.width - 24, 470)
    height: Math.min(parent.height - 24, 610)
    standardButtons: Dialog.Cancel
    footer: TouchFooter {}
    property var info: ({})
    property string errorText: ""
    function inspect(url) {
        info = collector.inspectSurface(url);
        errorText = info.error || "";
        chosen.currentIndex = 0;
    }
    onOpened: {
        let p = collector.state.gradePrefs;
        mode.currentIndex = p.mode === "Surface" ? 1 : 0;
        datum.currentIndex = p.datum === "Ellipsoid" ? 1 : 0;
        elevation.text = p.elevationM === undefined ? "" : String(p.elevationM);
        tolerance.text = String(p.toleranceM ?? 0.02);
        floatFix.checked = !!p.allowFloat;
        info = {};
        errorText = "";
        if (p.surface) {
            inspect("job:surface");
            chosen.currentIndex = Math.max(0, chosen.find(p.surface));
        }
    }
    contentItem: ScrollView {
        clip: true
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            spacing: 6
            FieldCombo {
                id: mode
                model: ["Manual target", "Design surface"]
                Layout.fillWidth: true
                implicitHeight: 56
            }
            ColumnLayout {
                visible: mode.currentIndex === 0
                Layout.fillWidth: true
                Label {
                    text: "Design ground elevation (m)"
                }
                TextField {
                    id: elevation
                    Layout.fillWidth: true
                    implicitHeight: 56
                    placeholderText: "Target elevation"
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
                    implicitHeight: 56
                }
            }
            ColumnLayout {
                visible: mode.currentIndex === 1
                Layout.fillWidth: true
                FieldButton {
                    text: "Open LandXML file…"
                    Layout.fillWidth: true
                    implicitHeight: 56
                    onClicked: picker.open()
                }
                FieldButton {
                    text: "Open practice surface"
                    Layout.fillWidth: true
                    implicitHeight: 56
                    onClicked: dlg.inspect("")
                }
                FieldCombo {
                    id: chosen
                    model: dlg.info.names || []
                    Layout.fillWidth: true
                    implicitHeight: 56
                    displayText: currentText || "Select a surface"
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: dlg.info.epsg ? "Use only with matching rover frame: NAD83(CSRS), EPSG:" + dlg.info.epsg + " · " + dlg.info.datum + " · metres" : "Import a metric NAD83(CSRS) UTM surface with a declared height reference."
                }
            }
            Label {
                text: "On-grade tolerance ± (m)"
            }
            TextField {
                id: tolerance
                Layout.fillWidth: true
                implicitHeight: 56
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                validator: DoubleValidator {
                    locale: "C"
                    bottom: 0
                    notation: DoubleValidator.StandardNotation
                }
            }
            CheckBox {
                id: floatFix
                text: "Include RTK float"
                implicitHeight: 56
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Material.theme === Material.Light ? "#8b360e" : "#ffbe9a"
                visible: dlg.errorText !== ""
                text: dlg.errorText
            }
            FieldButton {
                text: mode.currentIndex === 1 ? "Use surface and references" : "Use manual target"
                highlighted: true
                Layout.fillWidth: true
                implicitHeight: 56
                enabled: tolerance.text.trim().length > 0 && tolerance.acceptableInput && (mode.currentIndex === 1 ? !!chosen.currentText : (elevation.text.trim().length > 0 && elevation.acceptableInput))
                onClicked: {
                    if (collector.setGrade(mode.currentIndex === 1 ? "Surface" : "Manual", mode.currentIndex === 1 ? 0 : Number(elevation.text), datum.currentText, Number(tolerance.text), floatFix.checked, chosen.currentText))
                        dlg.close();
                    else
                        dlg.errorText = collector.state.notice;
                }
            }
            FieldButton {
                visible: mode.currentIndex === 1
                enabled: !!chosen.currentText
                text: "Load design for preview only"
                Layout.fillWidth: true
                onClicked: if (collector.setGrade("Preview", 0, "", 0, false, chosen.currentText))
                    dlg.close()
                else
                    dlg.errorText = collector.state.notice
            }
            FieldButton {
                text: "Turn grading off"
                Layout.fillWidth: true
                implicitHeight: 56
                onClicked: if (collector.setGrade("Off", 0, "", 0, false, ""))
                    dlg.close()
            }
        }
    }
    FileDialog {
        id: picker
        title: "Open design surface"
        nameFilters: ["LandXML (*.landxml *.xml *.XML *.LandXML)", "All files (*)"]
        onAccepted: dlg.inspect(selectedFile)
    }
}
