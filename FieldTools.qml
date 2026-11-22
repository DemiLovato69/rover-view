import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs

FieldDialog {
    id: d
    property string tool: "Coverage"
    property var drawing: ({})
    property var layers: []
    property string errorText: ""
    property bool hasPreview: false
    property bool advanced: false
    readonly property bool split: availableWidth >= 590
    signal followingStarted
    readonly property var selectedFeature: collector.features[lineChoice.currentIndex] || ({})
    readonly property real signedOffset: (side.currentIndex === 0 ? -1 : 1) * Number(offset.text)
    readonly property var linePreview: tool === "Follow" && visible ? collector.previewLine(selectedFeature.name || "", signedOffset, Number(trenchWidth.text)) : ({})
    title: ({
            Coverage: "Survey coverage",
            Surface: "Export surveyed ground",
            DXF: "Import DXF features",
            Follow: "Follow a feature"
        })[tool]
    preferredWidth: tool === "Coverage" ? 460 : 710
    problem: errorText
    primaryText: ({
            Coverage: "Apply settings",
            Surface: hasPreview ? "Export LandXML…" : "Preview surface",
            DXF: "Import selected",
            Follow: "Start following"
        })[tool]
    primaryName: tool === "Follow" ? "commitFollowing" : tool === "DXF" ? "commitDxf" : "applyFieldTool"
    primaryEnabled: tool === "Coverage" ? radius.acceptableInput && Number(radius.text) > 0 : tool === "DXF" ? confirmed.checked && layers.length > 0 && units.currentIndex >= 0 && epsg.acceptableInput && !drawing.error : tool === "Surface" ? epsg.acceptableInput && edge.acceptableInput && Number(edge.text) > 0 : !!selectedFeature.name && offset.acceptableInput && trenchWidth.acceptableInput && segment.acceptableInput && (target.currentIndex === 0 || Number(trenchWidth.text) > 0)
    secondaryText: tool === "Follow" && collector.fieldPrefs.follow ? "Stop" : ""
    onSecondaryAction: done(collector.followLine("", 0, 0, "Centre", 0, false))
    onPrimaryAction: {
        if (tool === "Coverage")
            done(collector.setCoverage(record.checked, overlay.checked, Number(radius.text)));
        else if (tool === "DXF")
            done(collector.importDxf(layers, Number(epsg.text), units.currentText));
        else if (tool === "Surface") {
            if (hasPreview && collector.terrainPreview.triangles?.length)
                saveFile.open();
            else {
                hasPreview = collector.previewTerrain(Number(epsg.text), datum.currentText, Number(edge.text));
                errorText = collector.terrainPreview.error || "";
            }
        } else if (tool === "Follow") {
            let ok = collector.followLine(selectedFeature.name, signedOffset, Number(trenchWidth.text), target.currentText, Number(segment.text), allowFloat.checked);
            done(ok);
            if (ok)
                followingStarted();
        }
    }
    function start(t, name) {
        tool = t;
        errorText = "";
        drawing = {};
        layers = [];
        hasPreview = false;
        confirmed.checked = false;
        const p = collector.fieldPrefs, j = collector.state, previous = collector.features.find(f => f.frame === j.frame && f.epsg > 0), declared = j.frame.match(/\bEPSG\s*:\s*(\d+)\b/i);
        record.checked = !!p.recordCoverage;
        overlay.checked = !!p.showCoverage;
        radius.text = String(p.radiusM ?? .5);
        epsg.text = String((j.gradePrefs.surfaceFrame === j.frame ? j.gradePrefs.epsg : 0) || previous?.epsg || declared?.[1] || "");
        edge.text = "5";
        lineChoice.currentIndex = Math.max(0, collector.features.findIndex(f => f.name === (name || p.follow)));
        side.currentIndex = (p.offsetM || 0) < 0 ? 0 : 1;
        offset.text = String(Math.abs(p.offsetM || 0));
        trenchWidth.text = String(p.widthM || 0);
        target.currentIndex = Math.max(0, target.choices.indexOf(p.target || "Centre"));
        segment.text = String(name && name !== p.follow ? 0 : p.segment || 0);
        allowFloat.checked = !!p.lineAllowFloat;
        advanced = false;
        open();
    }
    function done(ok) {
        if (ok)
            close();
        else
            errorText = collector.state.notice;
    }
    function inspect(url) {
        confirmed.checked = false;
        drawing = collector.inspectDxf(url);
        if (!url)
            epsg.text = "2959";
        layers = drawing.layers || [];
        errorText = drawing.error || "";
        units.currentIndex = units.find(drawing.units || "");
    }
    component Hint: Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        textFormat: Text.PlainText
    }
    component NumberField: TextField {
        implicitHeight: 56
        Layout.fillWidth: true
        Layout.minimumWidth: 60
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        validator: DoubleValidator {
            bottom: 0
            locale: "C"
            notation: DoubleValidator.StandardNotation
        }
    }
    Hint {
        text: "JOB   /   " + collector.state.job
        font.bold: true
        font.pixelSize: 13
    }
    ColumnLayout {
        visible: d.tool === "Coverage"
        Layout.fillWidth: true
        CheckBox {
            id: record
            text: "Include new shots in surface"
            implicitHeight: 56
            onClicked: if (checked)
                overlay.checked = true
        }
        CheckBox {
            id: overlay
            text: "Show footprints on map"
            implicitHeight: 56
        }
        Hint {
            text: "Sampling radius (m)"
        }
        NumberField {
            id: radius
        }
        Hint {
            text: "Each saved shot marks a " + (Number(radius.text) * 2).toFixed(2) + " m diameter footprint. Benchmarks are opt-in from point details."
        }
    }
    RowLayout {
        visible: d.tool === "DXF"
        Layout.fillWidth: true
        FieldButton {
            text: "Open DXF…"
            Layout.fillWidth: true
            onClicked: openFile.open()
        }
        FieldButton {
            text: "Practice DXF"
            Layout.fillWidth: true
            onClicked: d.inspect("")
        }
    }
    GridLayout {
        visible: d.tool === "Surface" || d.tool === "DXF"
        Layout.fillWidth: true
        columns: 2
        Hint {
            text: "Job UTM grid (EPSG)"
        }
        Hint {
            text: d.tool === "DXF" ? "Drawing units" : "Height reference"
        }
        NumberField {
            id: epsg
            placeholderText: "e.g. 2959"
            onTextChanged: {
                d.hasPreview = false;
                confirmed.checked = false;
            }
            validator: IntValidator {
                bottom: 1
                top: 999999
            }
        }
        FieldCombo {
            id: units
            visible: d.tool === "DXF"
            Layout.fillWidth: true
            Layout.preferredWidth: 180
            model: ["Metres", "Millimetres", "Centimetres", "Feet", "US survey feet", "Inches"]
            displayText: currentText || "Choose units"
            onCurrentIndexChanged: confirmed.checked = false
        }
        FieldCombo {
            id: datum
            visible: d.tool === "Surface"
            Layout.fillWidth: true
            Layout.preferredWidth: 180
            model: ["CGVD2013", "Ellipsoid"]
            onCurrentIndexChanged: d.hasPreview = false
        }
    }
    Hint {
        visible: d.tool === "DXF" || d.tool === "Surface"
        text: "NAD83(CSRS) UTM · Job reference: " + collector.state.frame
    }
    ColumnLayout {
        visible: d.tool === "Surface"
        Layout.fillWidth: true
        Hint {
            text: "Maximum triangle edge (m)"
        }
        NumberField {
            id: edge
            text: "5"
            onTextChanged: d.hasPreview = false
        }
        Hint {
            text: collector.points.filter(p => p.surfaceIncluded).length + " included shots · Long triangles are omitted."
        }
        PlanSketch {
            visible: d.hasPreview
            Layout.fillWidth: true
            implicitHeight: 150
            triangles: true
            paths: collector.terrainPreview.triangles || []
        }
        Hint {
            visible: d.hasPreview
            text: (collector.terrainPreview.triangles || []).length + " triangles · " + (collector.terrainPreview.gaps || 0) + " excluded"
        }
    }
    GridLayout {
        visible: d.tool === "DXF"
        Layout.fillWidth: true
        columns: d.split ? 2 : 1
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 300
            Hint {
                text: d.drawing.count === undefined ? "Open a text DXF with straight LINE or POLYLINE features." : d.drawing.count + " supported · " + d.drawing.skipped + " skipped · " + d.drawing.units
            }
            Repeater {
                model: d.drawing.layers || []
                CheckBox {
                    required property string modelData
                    text: modelData
                    implicitHeight: 56
                    checked: d.layers.includes(modelData)
                    onClicked: d.layers = checked ? d.layers.concat([modelData]) : d.layers.filter(l => l !== modelData)
                }
            }
        }
        PlanSketch {
            visible: !!d.drawing.count
            Layout.fillWidth: true
            Layout.preferredWidth: 240
            implicitHeight: 120
            paths: (d.drawing.shapes || []).filter(s => d.layers.includes(s.layer)).map(s => s.path)
        }
    }
    CheckBox {
        id: confirmed
        visible: d.tool === "DXF"
        objectName: "confirmDxfReference"
        text: "Drawing and job references match"
        implicitHeight: 56
    }
    ColumnLayout {
        visible: d.tool === "Follow"
        Layout.fillWidth: true
        spacing: 6
        FieldCombo {
            id: lineChoice
            objectName: "featureChooser"
            model: collector.features
            textRole: "name"
            Layout.fillWidth: true
            displayText: currentText || "No features in job"
            onActivated: segment.text = "0"
        }
        Hint {
            text: d.selectedFeature.name ? d.selectedFeature.source + " · " + (d.selectedFeature.path.length - 1) + " segment(s) · Left/right is relative to first → last." : "Create a line or import a DXF in Points List."
        }
        GridLayout {
            Layout.fillWidth: true
            columns: d.split ? 2 : 1
            columnSpacing: 14
            PlanSketch {
                visible: d.split
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 200
                implicitHeight: 110
                guide: true
                paths: [d.linePreview.source || [], d.linePreview.center || [], d.linePreview.left || [], d.linePreview.right || []]
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 350
                Hint {
                    text: "Offset excavation centre to"
                }
                ChoiceStrip {
                    id: side
                    objectName: "offsetSide"
                    Layout.fillWidth: true
                    choices: ["Left", "Right"]
                }
                GridLayout {
                    columns: 2
                    Layout.fillWidth: true
                    Hint {
                        text: "Offset (m)"
                    }
                    Hint {
                        text: "Full width (m)"
                    }
                    NumberField {
                        id: offset
                        objectName: "offsetDistance"
                        text: "0"
                    }
                    NumberField {
                        id: trenchWidth
                        text: "0"
                    }
                }
            }
        }
        Hint {
            text: "Guide rover to"
        }
        ChoiceStrip {
            id: target
            Layout.fillWidth: true
            choices: ["Centre", "Left edge", "Right edge"]
        }
        Hint {
            visible: target.currentIndex !== 0 && !(Number(trenchWidth.text) > 0)
            text: "Enter the full trench width to follow an edge."
        }
        FieldButton {
            text: d.advanced ? "Hide options ▴" : "Options ▾   ·   " + (Number(segment.text) ? "Segment " + segment.text : "Nearest segment") + "   ·   " + (allowFloat.checked ? "Float allowed" : "Fixed only")
            Layout.fillWidth: true
            onClicked: d.advanced = !d.advanced
        }
        RowLayout {
            visible: d.advanced
            Layout.fillWidth: true
            Hint {
                text: "Segment (0 = nearest)"
            }
            NumberField {
                id: segment
                text: "0"
                Layout.maximumWidth: 82
                validator: IntValidator {
                    bottom: 0
                }
            }
            CheckBox {
                id: allowFloat
                text: "Allow float"
                implicitHeight: 56
            }
        }
    }
    FileDialog {
        id: openFile
        title: "Import DXF"
        nameFilters: ["DXF (*.dxf *.DXF)", "All files (*)"]
        onAccepted: d.inspect(selectedFile)
    }
    FileDialog {
        id: saveFile
        fileMode: FileDialog.SaveFile
        defaultSuffix: "landxml"
        nameFilters: ["LandXML (*.landxml *.xml)"]
        onAccepted: d.done(collector.exportTerrain(selectedFile))
    }
}
