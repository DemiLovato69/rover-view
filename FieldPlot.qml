import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: plot
    property var live: collector.state.live
    property var points: collector.points
    property var outline: collector.surfaceOutline
    property bool fresh: collector.state.fresh
    property real scaleM: 5  // metres per grid square
    property real east: 0
    property real north: 0
    property var origin: {
        if (points.length)
            return points[points.length - 1];
        if (Number.isFinite(live.latitudeDeg))
            return live;
        if (outline.length)
            return outline[0];
        const b = offline.bounds;
        if (layer.currentIndex === 3 && b && b.length === 4)
            return {
                latitudeDeg: (b[1] + b[3]) / 2,
                longitudeDeg: (b[0] + b[2]) / 2
            };
        return {};
    }
    readonly property bool located: Number.isFinite(origin.latitudeDeg) && Number.isFinite(origin.longitudeDeg)
    readonly property bool day: Material.theme === Material.Light
    readonly property var offline: basemap.offlineInfo
    readonly property var online: basemap.onlineInfo
    readonly property bool ontario: layer.currentIndex === 2 && !online.url && located && origin.longitudeDeg >= -95.16 && origin.longitudeDeg <= -74.34 && origin.latitudeDeg >= 41.68 && origin.latitudeDeg <= 56.86
    readonly property bool historical: layer.currentIndex === 2 && !online.url && !ontario
    readonly property int nativeMax: layer.currentIndex === 3 ? offline.maxZoom ?? 0 : layer.currentIndex === 2 ? historical ? 12 : online.maxZoom ?? 19 : 19
    readonly property int nativeMin: layer.currentIndex === 3 ? offline.minZoom ?? 0 : 0
    function worldX(lon) {
        return (lon + 180) / 360;
    }
    function worldY(lat) {
        let r = Math.min(85.05112878, Math.max(-85.05112878, lat)) * Math.PI / 180;
        return (1 - Math.asinh(Math.tan(r)) / Math.PI) / 2;
    }
    readonly property real ox: located ? worldX(origin.longitudeDeg) : 0.5
    readonly property real oy: located ? worldY(origin.latitudeDeg) : 0.5
    readonly property real pixels: 40075016.686 * Math.cos(Math.min(85, Math.abs(origin.latitudeDeg || 0)) * Math.PI / 180) * 64 / scaleM
    readonly property real cx: width / 2 + east
    readonly property real cy: height / 2 + north
    readonly property int requestedZoom: Math.max(0, Math.round(Math.log2(pixels / 256)))
    readonly property int zoom: Math.min(nativeMax, requestedZoom)
    readonly property int tiles: Math.pow(2, zoom)
    readonly property real tileWidth: pixels / tiles
    readonly property int leftTile: Math.floor(ox * tiles - cx / tileWidth)
    readonly property int topTile: Math.floor(oy * tiles - cy / tileWidth)
    readonly property int cols: Math.ceil(width / tileWidth) + 1
    readonly property bool fetchTiles: visible && located && layer.currentIndex > 0 && (layer.currentIndex !== 3 || !!offline.name) && zoom >= nativeMin
    property bool missingTiles: false
    property bool loadingTiles: false
    function inBounds(x, y) {
        let b = offline.bounds;
        if (!b || b.length !== 4)
            return true;
        const west = x / tiles, east = (x + 1) / tiles;
        const inLon = b[0] <= b[2] ? east >= worldX(b[0]) && west <= worldX(b[2]) : east >= worldX(b[0]) || west <= worldX(b[2]);
        return inLon && (y + 1) / tiles >= worldY(b[3]) && y / tiles <= worldY(b[1]);
    }
    function tileUrl(x, y) {
        if (y < 0 || y >= tiles)
            return "";
        if (layer.currentIndex === 3)
            return inBounds(x, y) ? "image://offline/" + offline.revision + "/" + zoom + "/" + x + "/" + y : "";
        if (layer.currentIndex === 1)
            return "https://tile.openstreetmap.org/" + zoom + "/" + x + "/" + y + ".png";
        if (historical)
            return "https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/Landsat_WELD_CorrectedReflectance_TrueColor_Global_Annual/default/2000-12-01/GoogleMapsCompatible_Level12/" + zoom + "/" + y + "/" + x + ".jpeg";
        if (ontario)
            return "https://ws.lioservices.lrc.gov.on.ca/arcgis2/rest/services/LIO_Imagery/Ontario_Imagery_Web_Map_Service/MapServer/tile/" + zoom + "/" + y + "/" + x;
        return "https://raster.fieldrover.invalid/" + online.revision + "/" + zoom + "/" + x + "/" + y;
    }
    function updateTileStatus() {
        let missing = false, loading = false;
        for (let i = 0; i < visibleTiles.count; i++) {
            let item = visibleTiles.itemAt(i);
            if (!item || item.x >= width || item.y >= height || item.x + item.width <= 0 || item.y + item.height <= 0)
                continue;
            missing = missing || item.tileStatus === Image.Error || item.tileStatus === Image.Null;
            loading = loading || item.tileStatus === Image.Loading;
        }
        if (missing && !missingTiles)
            basemap.note(layer.currentText + " tiles missing at z" + zoom + " · grid retained");
        missingTiles = missing;
        loadingTiles = loading;
    }
    function zoomAt(factor, x, y) {
        const previous = scaleM;
        scaleM = Math.max(.1, Math.min(100000, scaleM / factor));
        const ratio = previous / scaleM;
        east = (east + width / 2 - x) * ratio + x - width / 2;
        north = (north + height / 2 - y) * ratio + y - height / 2;
    }
    function xy(p) {
        let dx = worldX(p.longitudeDeg) - ox;
        return Qt.point(cx + (dx - Math.round(dx)) * pixels, cy + (worldY(p.latitudeDeg) - oy) * pixels);
    }
    function center() {
        east = 0;
        north = 0;
        if (located && live.latitudeDeg !== undefined) {
            let p = xy(live);
            east = width / 2 - p.x;
            north = height / 2 - p.y;
        }
    }
    function redraw() {
        canvas.requestPaint();
    }
    onLiveChanged: redraw()
    onPointsChanged: redraw()
    onOutlineChanged: redraw()
    onFreshChanged: redraw()
    onPixelsChanged: redraw()
    onCxChanged: {
        redraw();
        Qt.callLater(updateTileStatus);
    }
    onCyChanged: {
        redraw();
        Qt.callLater(updateTileStatus);
    }
    onOxChanged: redraw()
    onOyChanged: redraw()
    clip: true
    radius: 3
    color: day ? "#e3edeb" : "#152e34"
    onDayChanged: redraw()
    Repeater {
        id: visibleTiles
        model: plot.fetchTiles ? plot.cols * (Math.ceil(plot.height / plot.tileWidth) + 1) : 0
        onCountChanged: Qt.callLater(plot.updateTileStatus)
        Item {
            id: cell
            required property int index
            readonly property int tileStatus: tile.status
            onTileStatusChanged: Qt.callLater(plot.updateTileStatus)
            property int col: plot.leftTile + index % plot.cols
            property int row: plot.topTile + Math.floor(index / plot.cols)
            property int tx: (col % plot.tiles + plot.tiles) % plot.tiles
            x: plot.cx + (col - plot.ox * plot.tiles) * plot.tileWidth
            y: plot.cy + (row - plot.oy * plot.tiles) * plot.tileWidth
            width: plot.tileWidth
            height: plot.tileWidth
            Image {
                id: tile
                anchors.fill: parent
                asynchronous: true
                source: plot.tileUrl(cell.tx, cell.row)
            }
        }
    }
    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            let c = getContext("2d");
            c.clearRect(0, 0, width, height);
            c.strokeStyle = layer.currentIndex ? "rgba(92,122,125,0.35)" : plot.day ? "#adbeba" : "#2c484e";
            c.lineWidth = 1;
            c.beginPath();
            for (let x = plot.cx % 64; x < width; x += 64) {
                c.moveTo(x, 0);
                c.lineTo(x, height);
            }
            for (let y = plot.cy % 64; y < height; y += 64) {
                c.moveTo(0, y);
                c.lineTo(width, y);
            }
            c.stroke();
            if (!plot.located)
                return;
            c.strokeStyle = plot.day && !layer.currentIndex ? "#995000" : "#f4bc54";
            c.lineWidth = 2;
            c.beginPath();
            for (let i = 0; i + 1 < plot.outline.length; i += 2) {
                let a = plot.xy(plot.outline[i]), b = plot.xy(plot.outline[i + 1]);
                c.moveTo(a.x, a.y);
                c.lineTo(b.x, b.y);
            }
            c.stroke();
            function mark(p, rover) {
                if (!Number.isFinite(p.latitudeDeg) || !Number.isFinite(p.longitudeDeg))
                    return;
                let v = plot.xy(p);
                c.fillStyle = rover ? (plot.fresh ? "#74efcf" : "#a7b9bc") : "#ffd88c";
                c.beginPath();
                c.arc(v.x, v.y, rover ? 7 : 4, 0, 2 * Math.PI);
                c.fill();
                c.strokeStyle = "#173034";
                c.stroke();
                if (rover) {
                    c.strokeStyle = "#16746f";
                    c.beginPath();
                    c.arc(v.x, v.y, 12, 0, 2 * Math.PI);
                    c.stroke();
                } else {
                    c.font = "13px sans-serif";
                    c.lineWidth = 3;
                    c.strokeText(p.id, v.x + 8, v.y - 7);
                    c.fillText(p.id, v.x + 8, v.y - 7);
                    c.lineWidth = 2;
                }
            }
            for (let p of plot.points)
                mark(p, false);
            mark(plot.live, true);
        }
    }
    DragHandler {
        target: null
        onTranslationChanged: delta => {
            plot.east += delta.x;
            plot.north += delta.y;
        }
    }
    PinchHandler {
        target: null
        onScaleChanged: delta => plot.zoomAt(delta, centroid.position.x, centroid.position.y)
    }
    FieldCombo {
        id: layer
        objectName: "mapLayer"
        width: 142
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 4
        implicitHeight: 56
        model: ["Local grid", "Street", "Satellite", "Offline"]
        onActivated: {
            // A survey-scale grid can show a single backyard. Open street maps at street scale.
            if (plot.located && currentIndex > 0) {
                const initial = currentIndex === 1 ? 17 : plot.nativeMax;
                plot.zoomAt((256 * Math.pow(2, initial)) / plot.pixels, plot.width / 2, plot.height / 2);
            }
        }
        Accessible.name: "Background layer"
        onCurrentIndexChanged: {
            plot.missingTiles = false;
            Qt.callLater(plot.updateTileStatus);
            plot.redraw();
        }
    }
    Label {
        anchors.top: parent.top
        anchors.right: parent.right
        padding: 6
        color: "white"
        text: "N ↑   " + plot.scaleM.toPrecision(2) + " m / square"
        background: Rectangle {
            color: "#cc152e34"
        }
    }
    Row {
        anchors.right: parent.right
        anchors.bottom: credit.top
        FieldButton {
            text: "−"
            implicitHeight: 56
            implicitWidth: 56
            Accessible.name: "Zoom out"
            onClicked: plot.zoomAt(.5, plot.width / 2, plot.height / 2)
        }
        FieldButton {
            text: "+"
            implicitHeight: 56
            implicitWidth: 56
            Accessible.name: "Zoom in"
            onClicked: plot.zoomAt(2, plot.width / 2, plot.height / 2)
        }
        FieldButton {
            text: "Center"
            implicitHeight: 56
            onClicked: plot.center()
        }
    }
    Label {
        id: credit
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        visible: layer.currentIndex !== 0
        height: visible ? implicitHeight : 0
        padding: 3
        color: "white"
        linkColor: "white"
        width: plot.width
        wrapMode: Text.WordWrap
        textFormat: layer.currentIndex === 1 ? Text.StyledText : Text.PlainText
        font.pixelSize: 12
        text: layer.currentIndex === 1 ? '<a href="https://www.openstreetmap.org/copyright">© OpenStreetMap contributors</a>' : layer.currentIndex === 3 ? plot.offline.attribution || "Local raster tiles" : plot.historical ? 'NASA GIBS · Landsat WELD (historical, 2000)' : plot.ontario ? "Geospatial Ontario · Open Government Licence – Ontario" : plot.online.attribution || ""
        onLinkActivated: link => Qt.openUrlExternally(link)
        background: Rectangle {
            color: "#df102e34"
        }
    }
    component TileNotice: Label {
        anchors.left: plot.left
        anchors.top: plot.top
        anchors.topMargin: 62
        color: "white"
        padding: 4
        font.pixelSize: 12
        background: Rectangle {
            color: "#df102e34"
        }
    }
    TileNotice {
        visible: layer.currentIndex > 0 && !plot.missingTiles
        text: layer.currentIndex === 3 && !plot.offline.name ? "No MBTiles · import in Settings" : !plot.located ? "Waiting for location" : plot.requestedZoom < plot.nativeMin ? "Zoom in · starts at z" + plot.nativeMin : plot.loadingTiles ? "Loading z" + plot.zoom + "…" : "z" + plot.zoom + " / " + plot.nativeMax + (plot.requestedZoom > plot.nativeMax ? " · native limit" : " · ready")
    }
    TileNotice {
        objectName: "offlineTiles"
        visible: layer.currentIndex > 0 && plot.missingTiles
        text: layer.currentIndex === 3 && !plot.inBounds(Math.floor(plot.ox * plot.tiles - plot.east / plot.tileWidth), Math.floor(plot.oy * plot.tiles - plot.north / plot.tileWidth)) ? "Map center outside package coverage" : "Tiles unavailable at z" + plot.zoom + " · grid retained"
    }
}
