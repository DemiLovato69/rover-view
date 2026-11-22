import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore

ApplicationWindow {
    id: win
    visible: true
    width: 1050
    height: 760
    title: "Field Rover · Survey collector"
    Settings {
        id: prefs
        objectName: "displayPreferences"
        category: "FieldDisplay"
        property string appearance: "Auto"
        property bool showGrade: true
        property bool showLevel: true
        property string groundDatum: "CGVD2013"
        property bool showSurface: false
    }
    readonly property bool daylight: prefs.appearance === "Daylight" || (prefs.appearance === "Auto" && Qt.styleHints.colorScheme !== Qt.ColorScheme.Dark)
    readonly property color ink: daylight ? "#13272e" : "#eef3f5"
    readonly property color subtle: daylight ? "#415a63" : "#becbd1"
    readonly property color panel: daylight ? "#ffffff" : "#20292d"
    readonly property color band: daylight ? "#e1ecee" : "#29363c"
    readonly property color line: daylight ? "#849a9e" : "#61757d"
    readonly property color accent: daylight ? "#085e74" : "#a2d7de"
    readonly property color warning: daylight ? "#8b3e06" : "#ffcb95"
    Material.theme: daylight ? Material.Light : Material.Dark
    Material.accent: accent
    Material.primary: band
    Material.background: panel
    Material.foreground: ink
    Material.roundedScale: Material.ExtraSmallScale
    Material.containerStyle: Material.Outlined
    font.pixelSize: 16
    color: daylight ? "#eff3f3" : "#161c20"
    property var s: collector.state
    property var live: s.live
    property var occ: s.occupation
    property bool occupying: occ.id !== undefined
    property bool wide: width >= 790 && width > height
    readonly property bool grading: prefs.showGrade
    readonly property bool floatingSurface: Qt.platform.os !== "android" && Qt.platform.os !== "ios"
    readonly property bool positioned: Number.isFinite(live.latitudeDeg) && Number.isFinite(live.longitudeDeg)
    readonly property var ground: prefs.groundDatum === "CGVD2013" ? live.groundCgvd2013M : live.groundEllipsoidM
    readonly property bool showBubble: prefs.showLevel && deviceLevel.supported
    property int page: 0
    property var titles: ["Connect", "Job", "Work View", "Points List", "Settings & Logs"]
    property string pointName: "P001"
    property string pointCode: ""
    property bool editingSetup: false
    property string activeJob: s.job
    onPageChanged: {
        if (page === 1 && !s.job && collector.jobs.length)
            collector.openJob(collector.jobs[0]); // The backend restores the last job; recover if that job no longer exists.
    }
    onActiveJobChanged: Qt.callLater(suggestId)
    Component.onCompleted: suggestId()
    function suggestId() {
        let last = collector.points.length ? collector.points[0] : null;
        pointName = last ? last.id : "P001";
        pointCode = last ? last.code : "";
        if (last)
            nextId();
    }
    function fmt(v, n) {
        return Number.isFinite(v) ? v.toFixed(n) : "—";
    }
    function nextId() {
        let m = pointName.match(/^(.*?)(\d+)$/);
        pointName = m ? m[1] + String(Number(m[2]) + 1).padStart(m[2].length, "0") : pointName + "1";
    }
    component Title: Label {
        font.pixelSize: 24
        font.bold: true
        color: win.ink
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
    component Caption: Label {
        color: win.subtle
        font.pixelSize: 15
        textFormat: Text.PlainText
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
    function showFocusedField() {
        let item = win.activeFocusItem;
        if (!item || !item.activeFocus)
            return;
        for (let p = item.parent; p; p = p.parent) {
            if (p.contentY === undefined || !p.contentItem)
                continue;
            let y = item.mapToItem(p.contentItem, 0, 0).y;
            let offset = p.contentY;
            if (y + item.height > offset + p.height)
                offset = y + item.height - p.height + 8;
            else if (y < offset)
                offset = y - 8;
            p.contentY = Math.max(0, Math.min(offset, p.contentHeight - p.height));
            break;
        }
    }
    onHeightChanged: Qt.callLater(showFocusedField)
    component Field: TextField {
        implicitHeight: 56
        Layout.fillWidth: true
        selectByMouse: true
        Accessible.name: placeholderText
        onActiveFocusChanged: if (activeFocus)
            Qt.callLater(win.showFocusedField)
        onAccepted: Qt.inputMethod.hide()
    }
    component Action: FieldButton {
        Layout.fillWidth: true
    }
    component Primary: Action {
        highlighted: true
        implicitHeight: 64
        font.bold: true
    }
    component Sheet: ScrollView {
        clip: true
        contentWidth: availableWidth
        padding: 12
        background: Rectangle {
            color: win.panel
            border.color: win.line
            radius: 3
        }
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    }
    Binding {
        target: deviceLevel
        property: "active"
        value: deviceLevel.supported && (win.page === 4 || (win.showBubble && win.page === 2)) && Qt.application.state === Qt.ApplicationActive
    }
    Component {
        id: bubbleGauge
        RowLayout {
            objectName: "deviceBubble"
            spacing: 8
            Rectangle {
                Layout.preferredWidth: 50
                Layout.preferredHeight: 50
                radius: 25
                color: win.band
                border.color: win.line
                Rectangle {
                    anchors.centerIn: parent
                    width: 1
                    height: parent.height
                    color: win.line
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width
                    height: 1
                    color: win.line
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: 15
                    height: 15
                    radius: 8
                    color: "transparent"
                    border.color: win.line
                }
                Rectangle {
                    visible: deviceLevel.fresh
                    width: 10
                    height: 10
                    radius: 5
                    color: win.accent
                    readonly property real limiter: Math.max(10, Math.hypot(deviceLevel.roll, deviceLevel.pitch))
                    x: 20 + 20 * deviceLevel.roll / limiter
                    y: 20 - 20 * deviceLevel.pitch / limiter
                }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                color: win.ink
                text: "DEVICE LEVEL\n" + (!deviceLevel.fresh ? "Awaiting sensor" : "R " + fmt(deviceLevel.roll, 1) + "°   P " + fmt(deviceLevel.pitch, 1) + "°\n" + (deviceLevel.calibrated ? "Plumb reference" : "Not zeroed"))
            }
        }
    }
    component PreviewPanel: Rectangle {
        id: previewPanel
        color: win.panel
        border.color: win.line
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "3D SURFACE   /   " + (s.surfaceName || "No design loaded")
                    color: win.ink
                    font.bold: true
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                FieldButton {
                    text: "×"
                    Accessible.name: "Close surface preview"
                    onClicked: prefs.showSurface = false
                }
            }
            Loader {
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: previewPanel.visible && win.page === 2 && prefs.showSurface && !!s.hasSurface
                sourceComponent: SurfacePreview {
                    mesh: collector.surfaceMesh
                    rover: s.surfacePosition || ({})
                    daylight: win.daylight
                }
            }
            Caption {
                visible: !s.hasSurface
                text: "Open Grade & surface setup to import a LandXML design."
            }
        }
    }
    Window {
        id: surfaceWindow
        objectName: "surfaceWindow"
        title: "Field Rover · Surface preview"
        transientParent: win
        width: 420
        height: 300
        flags: Qt.Tool | Qt.WindowTitleHint | Qt.WindowCloseButtonHint
        visible: win.visible && win.page === 2 && prefs.showSurface && win.floatingSurface
        onClosing: prefs.showSurface = false
        PreviewPanel {
            anchors.fill: parent
            visible: surfaceWindow.visible
            Material.theme: win.Material.theme
            Material.accent: win.accent
            Material.foreground: win.ink
            Material.background: win.panel
        }
    }
    header: ColumnLayout {
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.preferredHeight: 56
            Column {
                Layout.fillWidth: true
                Label {
                    text: "FIELD ROVER   /   GNSS"
                    color: win.subtle
                    font.letterSpacing: 2
                    font.pixelSize: 11
                    font.bold: true
                }
                Label {
                    text: titles[page]
                    color: win.ink
                    font.bold: true
                    font.pixelSize: 23
                }
            }
            Label {
                Layout.maximumWidth: win.width * .26
                elide: Text.ElideRight
                text: s.job ? (win.width >= 560 ? "JOB   /   " : "") + s.job : win.width >= 560 ? "NO ACTIVE JOB" : "No job"
                color: win.subtle
                font.pixelSize: 16
            }
            FieldButton {
                objectName: "themeSelector"
                text: prefs.appearance
                implicitHeight: 56
                Accessible.name: "Appearance: " + text + ". Change theme"
                onClicked: themeMenu.open()
                Menu {
                    id: themeMenu
                    y: parent.height
                    Repeater {
                        model: ["Auto", "Daylight", "Dark"]
                        MenuItem {
                            required property string modelData
                            text: modelData
                            implicitHeight: 56
                            checkable: true
                            checked: prefs.appearance === modelData
                            onTriggered: prefs.appearance = modelData
                        }
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: positionStrip.implicitHeight + 12
            color: win.band
            GridLayout {
                id: positionStrip
                anchors.centerIn: parent
                width: parent.width - 24
                columns: win.width >= 760 ? 4 : 2
                columnSpacing: 10
                rowSpacing: 2
                Label {
                    objectName: "positionStatus"
                    font.bold: true
                    color: s.fresh ? win.accent : win.warning
                    text: s.fresh ? String(live.quality || "Fix").toUpperCase() : win.positioned ? "LAST KNOWN · " + fmt(s.ageSeconds, 0) + " s" : "NO POSITION"
                }
                Label {
                    objectName: "groundElevation"
                    font.bold: true
                    Layout.fillWidth: true
                    horizontalAlignment: positionStrip.columns === 2 ? Text.AlignRight : Text.AlignLeft
                    text: "GROUND · " + prefs.groundDatum + "\n" + (s.job && win.positioned && Number.isFinite(win.ground) ? fmt(win.ground, 3) + " m" : "Unavailable")
                    font.pixelSize: win.wide ? 17 : 15
                }
                Label {
                    objectName: "positionCoordinates"
                    Layout.fillWidth: true
                    font.family: "monospace"
                    font.pixelSize: win.wide ? 18 : 15
                    text: "LAT\n" + fmt(live.latitudeDeg, 8) + "°"
                }
                Label {
                    Layout.fillWidth: true
                    font.family: "monospace"
                    font.pixelSize: win.wide ? 18 : 15
                    horizontalAlignment: positionStrip.columns===2?Text.AlignRight:Text.AlignLeft
                    text:"LON\n"+fmt(live.longitudeDeg,8)+"°"
                }
            }
        }
    }
    footer: ColumnLayout {
        spacing: 0
        Label {
            Layout.fillWidth: true
            leftPadding: 12
            rightPadding: 12
            topPadding: 4
            bottomPadding: 4
            wrapMode: Text.WordWrap
            text: win.page === 4 && basemap.message ? basemap.message : s.notice
            textFormat: Text.PlainText
            visible: text.length > 0
            maximumLineCount: 2
            elide: Text.ElideRight
            color: win.ink
            background: Rectangle {
                color: win.band
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 0
            Repeater {
                model: titles
                Button {
                    required property int index
                    required property string modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    implicitHeight: 58
                    text: win.width >= 620 ? modelData : ["Connect", "Job", "Work", "Points", "Settings"][index]
                    Accessible.name: modelData
                    leftPadding: 1
                    rightPadding: 1
                    font.pixelSize: win.width >= 620 ? 16 : 13
                    flat: true
                    Material.foreground: page === index ? win.ink : win.subtle
                    background: Rectangle {
                        color: parent.down || page === index ? win.band : win.panel
                        border.color: win.line
                        Rectangle {
                            visible: page === index
                            width: parent.width
                            height: 3
                            color: win.accent
                        }
                    }
                    font.bold: page === index
                    onClicked: {
                        Qt.inputMethod.hide();
                        page = index;
                    }
                }
            }
        }
    }
    StackLayout {
        anchors.fill: parent
        anchors.margins: 12
        currentIndex: [1, 2, 0, 3, 4][page]
        ColumnLayout {
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Caption {
                    font.pixelSize: 14
                    text: (live.used ?? "—") + " SV used   ·   Corrections " + fmt(live.ageS, 1) + " s   ·   Pole " + (s.job ? fmt(s.pole, 3) + " m" : "—") + (live.source ? "   /   " + live.source : "")
                }
                FieldButton {
                    text: "Instruments ▾"
                    implicitHeight: 56
                    onClicked: instruments.open()
                    Menu {
                        id: instruments
                        x: parent.width - width
                        y: parent.height
                        MenuItem {
                            objectName: "toggleGrade"
                            implicitHeight: 56
                            text: "Grade bar"
                            checkable: true
                            checked: prefs.showGrade
                            onTriggered: prefs.showGrade = checked
                        }
                        MenuItem {
                            objectName: "toggleBubble"
                            implicitHeight: 56
                            text: deviceLevel.supported ? "Bubble level" : "No level sensor"
                            enabled: deviceLevel.supported
                            checkable: true
                            checked: win.showBubble
                            onTriggered: prefs.showLevel = checked
                        }
                        MenuItem {
                            objectName: "toggleSurface"
                            implicitHeight: 56
                            text: "3D surface preview"
                            checkable: true
                            checked: prefs.showSurface
                            onTriggered: prefs.showSurface = checked
                        }
                        MenuSeparator {}
                        MenuItem {
                            text: "Grade & surface setup…"
                            implicitHeight: 56
                            enabled: !!s.job && !win.occupying
                            onTriggered: gradeSetup.open()
                        }
                    }
                }
            }
            ScrollView {
                id: workScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 0
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                GridLayout {
                    id: instrumentsGrid
                    width: workScroll.availableWidth
                    columns: wide ? 2 : 1
                    readonly property real mapHeight: workScroll.availableHeight - (win.showBubble && !grading ? levelCard.implicitHeight + rowSpacing : 0) - (!wide && grading ? gradeDisplay.implicitHeight + rowSpacing : 0)
                    FieldPlot {
                        id: fieldMap
                        readonly property bool insetFits: width >= 420 && height >= 265
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredHeight: Math.max(155, instrumentsGrid.mapHeight)
                        Layout.preferredWidth: 560
                        Layout.columnSpan: wide && !grading ? 2 : 1
                        PreviewPanel {
                            objectName: "surfaceInset"
                            visible: prefs.showSurface && !win.floatingSurface && fieldMap.insetFits
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 6
                            anchors.topMargin: 34
                            width: Math.min(300, parent.width * .48)
                            height: Math.min(215, parent.height - 122)
                        }
                    }
                    GradeBar {
                        id: gradeDisplay
                        visible: grading
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 400
                        Layout.preferredHeight: implicitHeight
                        grade: s.grade
                        levelGauge: win.showBubble ? bubbleGauge : null
                    }
                    Rectangle {
                        id: levelCard
                        visible: win.showBubble && !grading
                        Layout.fillWidth: true
                        implicitHeight: 78
                        color: win.panel
                        border.color: win.line
                        radius: 3
                        Loader {
                            anchors.fill: parent
                            anchors.margins: 12
                            active: levelCard.visible
                            sourceComponent: bubbleGauge
                        }
                    }
                    PreviewPanel {
                        visible: prefs.showSurface && !win.floatingSurface && !fieldMap.insetFits
                        Layout.fillWidth: true
                        Layout.preferredHeight: 246
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: dock.implicitHeight + 16
                color: win.band
                border.color: win.line
                radius: 3
                ColumnLayout {
                    id: dock
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 3
                    Caption {
                        font.bold: true
                        text: occupying ? "BENCHMARK   /   " + occ.id + "   ·   " + occ.status : !s.job ? "COLLECT   /   Open or create a job" : !s.fresh ? "COLLECT   /   Waiting for a fresh position" : !s.canStore ? "COLLECT   /   Waiting for next observation" : "COLLECT   /   Ready"
                    }
                    GridLayout {
                        visible: !occupying
                        columns: wide ? 4 : 2
                        Layout.fillWidth: true
                        columnSpacing: 8
                        rowSpacing: 0
                        Field {
                            placeholderText: "Point ID"
                            Layout.preferredWidth: 120
                            text: win.pointName
                            onTextEdited: win.pointName = text
                        }
                        Field {
                            placeholderText: "Code"
                            Layout.preferredWidth: 120
                            text: win.pointCode
                            onTextEdited: win.pointCode = text
                        }
                        Primary {
                            objectName: "storePoint"
                            text: "Store point"
                            enabled: s.canStore && pointName.trim().length > 0
                            onClicked: if (collector.store(pointName, pointCode))
                                nextId()
                        }
                        Action {
                            text: "Benchmark…"
                            enabled: s.connected && s.job.length > 0
                            onClicked: setup.open()
                        }
                    }
                    ProgressBar {
                        visible: occupying
                        Layout.fillWidth: true
                        from: 0
                        to: occ.targetS || 1
                        value: occ.elapsedS || 0
                    }
                    Caption {
                        visible: occupying
                        text: fmt(occ.elapsedS, 1) + " / " + (occ.targetS || 0) + " s   ·   " + (occ.epochs || 0) + " accepted   ·   " + (occ.excluded || 0) + " excluded   ·   " + (occ.quality || "—") + "\nRMS H " + fmt(occ.horizontalRmsM, 3) + " m   ·   V " + fmt(occ.verticalRmsM, 3) + " m" + (occ.review ? "\nMean: " + fmt(occ.latitudeDeg, 8) + "°, " + fmt(occ.longitudeDeg, 8) + "°   ·   Ellipsoid " + fmt(occ.ellipsoidM, 3) + " m" : "")
                    }
                    RowLayout {
                        visible: occupying
                        Layout.fillWidth: true
                        Primary {
                            visible: !!occ.review
                            text: "Save benchmark"
                            onClicked: if (collector.finish())
                                nextId()
                        }
                        Action {
                            text: "Discard occupation…"
                            onClicked: confirmDiscard.open()
                        }
                    }
                }
            }
        }
        Sheet {
            ColumnLayout {
                width: parent.width
                spacing: 6
                Title {
                    text: "Receiver"
                }
                Caption {
                    text: s.receiver
                }
                RowLayout {
                    Layout.fillWidth: true
                    FieldCombo {
                        id: devices
                        Layout.fillWidth: true
                        implicitHeight: 56
                        model: collector.ports
                        displayText: currentText || "Select a serial device"
                        onActivated: device.text = currentText
                    }
                    FieldButton {
                        text: "Scan"
                        implicitHeight: 56
                        onClicked: collector.scan()
                    }
                }
                Field {
                    id: device
                    placeholderText: "Serial device or mock /dev/ttys… path"
                    text: devices.currentText
                }
                Title {
                    text: "Base corrections"
                }
                Caption {
                    text: s.base
                }
                FieldCombo {
                    id: profile
                    Layout.fillWidth: true
                    implicitHeight: 56
                    model: ["RTK2go · OsgoodeFarms", "Local simulator"]
                    onActivated: {
                        address.text = currentIndex ? "http://127.0.0.1:2102/MOCK" : "http://rtk2go.com:2101/OsgoodeFarms";
                        email.text = "";
                        password.text = currentIndex ? "" : "none";
                    }
                }
                Field {
                    id: address
                    placeholderText: "Caster URL / mountpoint"
                    text: "http://rtk2go.com:2101/OsgoodeFarms"
                    inputMethodHints: Qt.ImhUrlCharactersOnly
                }
                Caption {
                    text: address.text.startsWith("http://") ? "HTTP · unencrypted correction stream" : "HTTPS correction stream"
                }
                Field {
                    id: email
                    placeholderText: "Caster username / rover email"
                    inputMethodHints: Qt.ImhEmailCharactersOnly
                }
                Field {
                    id: password
                    placeholderText: "Password"
                    text: "none"
                    echoMode: TextInput.Password
                }
                Primary {
                    text: s.connected ? "Disconnect" : "Connect receiver and base"
                    onClicked: {
                        Qt.inputMethod.hide();
                        if (s.connected)
                            collector.stop();
                        else
                            collector.start(device.text, address.text, email.text, password.text);
                    }
                }
                Action {
                    text: "Start offline practice (90 s)"
                    enabled: !s.connected
                    onClicked: if (collector.practice())
                        page = s.job ? 2 : 1
                }
                Action {
                    text: "Start grading walk (36 s)"
                    enabled: !s.connected
                    onClicked: if (collector.gradingWalk())
                        page = s.job ? 2 : 1
                }
                Action {
                    text: "Continue to Job →"
                    onClicked: page = 1
                }
            }
        }
        Sheet {
            ColumnLayout {
                width: parent.width
                spacing: 6
                Title {
                    text: "Job readiness"
                }
                Caption {
                    text: s.job ? "Active: " + s.job + "   ·   " + s.antenna + "   ·   " + fmt(s.pole, 3) + " m   ·   " + s.frame : "No active job"
                }
                Caption {
                    text: (s.connected ? "Receiver session open" : "Receiver disconnected") + "   /   " + (s.fresh ? "Current " + live.quality + " solution" : "Waiting for fresh position") + "\nDesign: " + (s.surfaceName || "No imported surface") + "   /   Grade: " + (s.gradePrefs.mode || "Off") + (s.gradePrefs.datum ? " · " + s.gradePrefs.datum : "")
                }
                Caption {
                    text: {
                        const m = basemap.offlineInfo, b = m.bounds;
                        if (!m.name)
                            return "Offline map: none attached";
                        let coverage = "Coverage bounds not supplied";
                        if (b && b.length === 4) {
                            const inside = live.longitudeDeg >= b[0] && live.latitudeDeg >= b[1] && live.longitudeDeg <= b[2] && live.latitudeDeg <= b[3];
                            coverage = !s.fresh ? "Waiting for fix to check coverage" : inside ? "Fix inside map bounds" : "Fix outside map bounds";
                        }
                        return "Offline map: " + m.name + "   /   z" + m.minZoom + "–" + m.maxZoom + "   ·   " + coverage;
                    }
                }
                Action {
                    text: "Grade & surface setup…"
                    enabled: !!s.job && !occupying
                    onClicked: gradeSetup.open()
                }
                Primary {
                    text: s.job ? "Resume " + s.job + " →" : "Continue to Work View →"
                    onClicked: page = 2
                }
                Title {
                    text: "Saved jobs"
                }
                Repeater {
                    model: collector.jobs
                    Action {
                        required property string modelData
                        text: modelData
                        enabled: !occupying
                        onClicked: {
                            collector.openJob(modelData);
                            if (s.job === modelData)
                                page = 2;
                        }
                    }
                }
                Title {
                    text: editingSetup ? "Active job setup" : "New job"
                }
                Action {
                    visible: !!s.job
                    enabled: !occupying
                    text: editingSetup ? "Cancel setup edit" : "Edit active setup…"
                    onClicked: {
                        editingSetup = !editingSetup;
                        jobName.text = editingSetup ? s.job : "";
                        antenna.text = editingSetup ? s.antenna : "";
                        pole.text = editingSetup ? String(s.pole) : "";
                        frame.text = editingSetup ? s.frame : "";
                    }
                }
                Field {
                    id: jobName
                    readOnly: editingSetup
                    placeholderText: "Job name"
                }
                Field {
                    id: antenna
                    placeholderText: "Antenna model / reference point"
                }
                Caption {
                    text: "Measured vertical height to antenna reference (metres)"
                }
                Field {
                    id: pole
                    placeholderText: "Pole height (m)"
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    validator: DoubleValidator {
                        bottom: 0
                        top: 20
                        locale: "C"
                        notation: DoubleValidator.StandardNotation
                    }
                }
                Field {
                    id: frame
                    placeholderText: "Base coordinate frame and epoch (if known)"
                }
                Primary {
                    text: editingSetup ? "Save setup for future captures" : "Create job"
                    enabled: !occupying && jobName.text.trim().length > 0 && antenna.text.trim().length > 0 && pole.acceptableInput && pole.text.length > 0
                    onClicked: if (collector.createJob(jobName.text, antenna.text, Number(pole.text), frame.text, editingSetup)) {
                        editingSetup = false;
                        Qt.inputMethod.hide();
                        page = 2;
                    }
                }
            }
        }
        ColumnLayout {
            Title {
                text: "Saved points · " + collector.points.length
            }
            Field {
                id: search
                placeholderText: "Search ID or code"
            }
            ListView {
                objectName: "pointList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: collector.points
                Label {
                    anchors.centerIn: parent
                    width: parent.width - 16
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: win.subtle
                    visible: !collector.points.some(p => (p.id + " " + p.code).toLowerCase().includes(search.text.toLowerCase()))
                    text: collector.points.length ? "No matching points. Try another ID or code." : "No points yet. Use Store point in Work View."
                }
                spacing: 2
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    property bool match: (modelData.id + " " + modelData.code).toLowerCase().includes(search.text.toLowerCase())
                    width: ListView.view.width
                    height: match ? 60 : 0
                    visible: match
                    Accessible.name: modelData.kind + " " + modelData.id + ", " + modelData.quality
                    contentItem: RowLayout {
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                font.bold: true
                                textFormat: Text.PlainText
                                text: modelData.id + (modelData.code ? "   /   " + modelData.code : "")
                            }
                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                color: win.subtle
                                font.pixelSize: 13
                                text: modelData.kind + "   ·   " + modelData.gpst + " GPST"
                            }
                        }
                        Label {
                            font.bold: true
                            text: String(modelData.quality).toUpperCase()
                        }
                        Label {
                            text: "›"
                            font.pixelSize: 24
                            color: win.accent
                        }
                    }
                    background: Rectangle {
                        color: parent.down || index % 2 ? win.band : win.panel
                        border.color: win.line
                    }
                    onClicked: {
                        detail.record = modelData;
                        detail.open();
                    }
                }
            }
            Action {
                text: "Export points as CSV…"
                enabled: collector.points.length > 0
                onClicked: fileDialog.open()
            }
        }
        Sheet {
            ColumnLayout {
                width: parent.width
                spacing: 8
                Title {
                    text: "Display settings"
                }
                Caption {
                    text: "Appearance"
                }
                FieldCombo {
                    objectName: "appearanceSelector"
                    Layout.fillWidth: true
                    model: ["Auto", "Daylight", "Dark"]
                    currentIndex: model.indexOf(prefs.appearance)
                    Accessible.name: "Appearance"
                    onActivated: prefs.appearance = currentText
                }
                Caption {
                    text: "Ground elevation datum · status strip only"
                }
                FieldCombo {
                    objectName: "groundDatumSelector"
                    Layout.fillWidth: true
                    model: ["CGVD2013", "Ellipsoid"]
                    currentIndex: model.indexOf(prefs.groundDatum)
                    Accessible.name: "Ground elevation datum"
                    onActivated: prefs.groundDatum = currentText
                }
                Title {
                    text: "Device level calibration"
                }
                Caption {
                    text: deviceLevel.status
                    font.bold: true
                }
                Caption {
                    visible: deviceLevel.supported
                    text: "This measures the device, not the pole directly. Secure the device to the pole, plumb the pole independently, hold still for one second, then zero. Recheck after rotating or moving the mount. It does not correct coordinates."
                }
                RowLayout {
                    visible: deviceLevel.supported
                    Layout.fillWidth: true
                    Action {
                        text: "Zero to plumb"
                        enabled: deviceLevel.fresh
                        onClicked: calibrationNote.text = deviceLevel.zero() ? "Plumb reference saved." : "Not zeroed. Keep the device stationary for one second and retry."
                    }
                    Action {
                        text: "Reset reference"
                        enabled: deviceLevel.calibrated
                        onClicked: {
                            deviceLevel.reset();
                            calibrationNote.text = "Reference cleared.";
                        }
                    }
                }
                Caption {
                    id: calibrationNote
                    visible: !!text
                }
                Title {
                    text: "Receiver diagnostics"
                }
                Caption {
                    text: "Receiver   /   " + s.receiver + "\nBase   /   " + s.base
                }
                Caption {
                    text: (live.source || "No source") + "   /   " + (live.gpst ? live.gpst + " GPST" : "No epoch") + "\nSV used  " + (live.used ?? "—") + "   ·   Base SV  " + (live.baseSats ?? "—") + "   ·   Correction age  " + fmt(live.ageS, 1) + " s   ·   Ratio  " + fmt(live.ratio, 1)
                }
                Caption {
                    text: "Antenna   /   Ellipsoid " + fmt(live.ellipsoidM, 3) + " m   ·   CGVD2013 " + fmt(live.antennaCgvd2013M, 3) + " m\nGround   /   Ellipsoid " + fmt(s.job ? live.groundEllipsoidM : undefined, 3) + " m   ·   CGVD2013 " + fmt(s.job ? live.groundCgvd2013M : undefined, 3) + " m"
                }
                Title {
                    text: "Known-point check"
                }
                Caption {
                    text: "Stand on a saved point with the same ground reference. Compare the rover position without storing or adjusting it."
                }
                FieldCombo {
                    id: controlPoint
                    Layout.fillWidth: true
                    model: collector.points
                    textRole: "id"
                    displayText: currentText || "No saved points in this job"
                    Accessible.name: "Known point"
                }
                Action {
                    text: "Check against " + (controlPoint.currentText || "known point")
                    enabled: !!controlPoint.currentText
                    onClicked: win.checkPoint(controlPoint.currentText)
                }
                Title {
                    text: "Offline imagery"
                }
                Caption {
                    text: basemap.offlineInfo.name ? basemap.offlineInfo.name + "   /   z" + basemap.offlineInfo.minZoom + "–" + basemap.offlineInfo.maxZoom + (basemap.offlineInfo.date ? "   ·   " + basemap.offlineInfo.date : "") + "\n" + (basemap.offlineInfo.attribution || "") + (basemap.offlineInfo.bounds?.length === 4 ? "\nBounds (W, S, E, N): " + basemap.offlineInfo.bounds.join(", ") : "") : !s.job ? "Open a job to attach an offline map." : "No raster MBTiles installed. Import a licensed site map, then select Offline on the plot."
                }
                Action {
                    text: basemap.busy ? "Checking map…" : "Import raster MBTiles…"
                    enabled: !!s.job && !basemap.busy
                    onClicked: tileFile.open()
                }
                Action {
                    text: "Use practice-pad orthophoto (offline)"
                    enabled: !!s.job && !basemap.busy
                    onClicked: basemap.importTiles("practice:ortho")
                }
                ProgressBar {
                    Layout.fillWidth: true
                    indeterminate: true
                    visible: basemap.busy
                }
                Title {
                    text: "Online satellite imagery"
                }
                Caption {
                    text: "Default: Geospatial Ontario aerial imagery in the provincial coverage area; historical NASA imagery elsewhere. Override with a licensed HTTPS XYZ service and its attribution."
                }
                Field {
                    id: tileUrl
                    placeholderText: "https://…/{z}/{x}/{y}.jpg"
                    text: basemap.onlineInfo.url || ""
                    inputMethodHints: Qt.ImhUrlCharactersOnly
                }
                Field {
                    id: tileCredit
                    placeholderText: "Provider attribution"
                    text: basemap.onlineInfo.attribution || ""
                }
                Field {
                    id: tileMax
                    placeholderText: "Maximum native zoom (0–24)"
                    text: String(basemap.onlineInfo.maxZoom ?? 19)
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator {
                        bottom: 0
                        top: 24
                    }
                }
                Action {
                    text: "Save imagery settings"
                    enabled: !tileUrl.text.trim() || (tileCredit.text.trim().length > 0 && tileMax.acceptableInput)
                    onClicked: basemap.setOnline(tileUrl.text.trim(), tileCredit.text.trim(), Number(tileMax.text))
                }
                Title {
                    text: "Session log"
                }
                FieldCombo {
                    id: logSource
                    Layout.fillWidth: true
                    model: ["Survey events", "Map events"]
                    Accessible.name: "Log source"
                }
                Caption {
                    text: (logSource.currentIndex ? basemap.events : collector.events).join("\n") || "No events recorded this session."
                    textFormat: Text.PlainText
                }
            }
        }
    }
    function checkPoint(id) {
        checkResult.pointId = id;
        checkResult.comparison = collector.controlCheck(id);
        checkResult.open();
    }
    Dialog {
        id: checkResult
        property string pointId: ""
        property var comparison: ({})
        title: "Check · " + pointId
        anchors.centerIn: parent
        modal: true
        width: Math.min(win.width - 24, 460)
        standardButtons: Dialog.Close
        footer: TouchFooter {}
        ColumnLayout {
            width: parent.width
            Caption {
                text: checkResult.comparison.status || "Unavailable"
                font.bold: true
            }
            Caption {
                text: "Horizontal distance   " + fmt(checkResult.comparison.horizontalM, 3) + " m\nVertical difference   " + fmt(checkResult.comparison.verticalM, 3) + " m\nDatum   " + (checkResult.comparison.datum || "—")
            }
        }
    }
    FileDialog {
        id: tileFile
        title: "Import licensed raster MBTiles"
        nameFilters: ["Raster MBTiles (*.mbtiles *.MBTILES)", "All files (*)"]
        onAccepted: basemap.importTiles(selectedFile)
    }
    Dialog {
        id: setup
        anchors.centerIn: parent
        width: Math.min(win.width - 24, 420)
        modal: true
        title: "Occupy benchmark"
        standardButtons: Dialog.Cancel
        footer: TouchFooter {}
        ColumnLayout {
            width: parent.width
            Caption {
                text: "Keep the pole stationary. Only qualifying epochs count."
            }
            Field {
                id: benchId
                text: win.pointName
                placeholderText: "Benchmark ID"
            }
            Caption {
                text: "Qualifying duration (seconds)"
            }
            Field {
                id: duration
                text: "30"
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator {
                    bottom: 1
                    top: 3600
                }
            }
            CheckBox {
                id: allowFloat
                text: "Accept float (bench testing)"
                implicitHeight: 56
            }
            Primary {
                text: "Start occupation"
                enabled: duration.acceptableInput
                onClicked: if (collector.occupy(benchId.text, pointCode, Number(duration.text), allowFloat.checked)) {
                    win.pointName = benchId.text;
                    setup.close();
                }
            }
        }
        onOpened: benchId.text = pointName
    }
    Dialog {
        id: confirmDiscard
        anchors.centerIn: parent
        modal: true
        title: "Discard this occupation?"
        standardButtons: Dialog.Discard | Dialog.Cancel
        footer: TouchFooter {}
        onDiscarded: collector.discard()
    }
    Dialog {
        id: detail
        objectName: "pointDetail"
        property var record: ({})
        property var epochs: []
        property bool showEpochs: false
        anchors.centerIn: parent
        modal: true
        width: Math.min(win.width - 24, 520)
        height: Math.min(win.height - 48, 620)
        title: (record.kind || "") + " · " + (record.id || "")
        standardButtons: Dialog.Close
        footer: TouchFooter {}
        onOpened: {
            showEpochs = false;
            epochs = collector.epochsFor(record.id);
        }
        ColumnLayout {
            anchors.fill: parent
            Caption {
                text: (detail.record.quality || "") + "   ·   " + (detail.record.gpst || "") + " GPST"
            }
            Action {
                visible: detail.epochs.length > 0
                text: detail.showEpochs ? "Show summary" : "Inspect " + detail.epochs.length + " accepted epochs"
                onClicked: detail.showEpochs = !detail.showEpochs
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: detail.showEpochs ? detail.epochs : Object.keys(detail.record)
                delegate: Label {
                    required property var modelData
                    width: ListView.view.width
                    padding: 6
                    wrapMode: Text.WordWrap
                    text: typeof modelData === "string" ? modelData + ":  " + detail.record[modelData] : modelData.gpst + " GPST · " + modelData.quality + "\n" + fmt(modelData.latitudeDeg, 8) + "°, " + fmt(modelData.longitudeDeg, 8) + "° · h " + fmt(modelData.ellipsoidM, 3) + " m"
                }
            }
            Action {
                objectName: "requestDelete"
                text: "Delete " + (detail.record.kind || "point").toLowerCase() + "…"
                danger: true
                onClicked: removeRecord.open()
            }
            Action {
                text: "Check rover against this point"
                onClicked: win.checkPoint(detail.record.id)
            }
        }
    }
    Dialog {
        id: removeRecord
        objectName: "deleteDialog"
        modal: true
        anchors.centerIn: parent
        width: Math.min(win.width - 24, 460)
        title: "Delete " + (detail.record.kind || "point").toLowerCase() + "?"
        property string failure: ""
        onOpened: failure = ""
        ColumnLayout {
            width: parent.width
            Caption {
                textFormat: Text.PlainText
                text: "Permanently delete “" + (detail.record.id || "") + "” from job “" + (detail.record.job || "") + "”?" + (detail.record.kind === "Benchmark" ? " This also deletes its " + detail.epochs.length + " accepted epochs." : "")
            }
            Caption {
                visible: !!removeRecord.failure
                color: win.warning
                text: removeRecord.failure
            }
            RowLayout {
                Layout.fillWidth: true
                Action {
                    objectName: "cancelDelete"
                    text: "Cancel"
                    onClicked: removeRecord.reject()
                }
                Action {
                    objectName: "confirmDelete"
                    text: "Delete"
                    danger: true
                    onClicked: {
                        let r = detail.record;
                        if (collector.deletePoint(r.job, r.id, r.gpst)) {
                            removeRecord.close();
                            detail.close();
                        } else
                            removeRecord.failure = collector.state.notice;
                    }
                }
            }
        }
    }
    FileDialog {
        id: fileDialog
        fileMode: FileDialog.SaveFile
        nameFilters: ["CSV files (*.csv)"]
        defaultSuffix: "csv"
        onAccepted: collector.exportCsv(selectedFile)
    }
    GradeSetup {
        id: gradeSetup
    }
}
