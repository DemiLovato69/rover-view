import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Rectangle {
    id: sketch
    property var paths: []
    property bool triangles: false
    property bool numbered: false
    property bool guide: false
    color: Material.theme === Material.Light ? "#e4eeed" : "#182c33"
    radius: 3
    clip: true
    onPathsChanged: canvas.requestPaint()
    Canvas {
        id: canvas
        anchors.fill: parent
        onPaint: {
            const c = getContext("2d");
            c.clearRect(0, 0, width, height);
            let xmin = Infinity, ymin = Infinity, xmax = -Infinity, ymax = -Infinity, k = 1, first = true;
            for (const path of sketch.paths)
                for (const p of path) {
                    if (first) {
                        k = Math.cos((p.latitudeDeg || 0) * Math.PI / 180);
                        first = false;
                    }
                    let x = Number.isFinite(p.longitudeDeg) ? p.longitudeDeg * k : p.x, y = p.latitudeDeg ?? p.y;
                    xmin = Math.min(xmin, x);
                    xmax = Math.max(xmax, x);
                    ymin = Math.min(ymin, y);
                    ymax = Math.max(ymax, y);
                }
            if (first)
                return;
            let s = Math.min((width - 40) / Math.max(1e-10, xmax - xmin), (height - 36) / Math.max(1e-10, ymax - ymin));
            let i = 0;
            for (const path of sketch.paths) {
                c.beginPath();
                let j = 0;
                for (const p of path) {
                    let x = width / 2 + ((Number.isFinite(p.longitudeDeg) ? p.longitudeDeg * k : p.x) - (xmin + xmax) / 2) * s, y = height / 2 - ((p.latitudeDeg ?? p.y) - (ymin + ymax) / 2) * s;
                    if (j === 0)
                        c.moveTo(x, y);
                    else
                        c.lineTo(x, y);
                    j++;
                    if (sketch.guide && i === 0 && j === 1) {
                        c.fillStyle = sketch.Material.foreground;
                        c.fillText("START", x + 4, y - 5);
                    }
                    if (sketch.numbered) {
                        c.fillStyle = sketch.Material.foreground;
                        c.fillText(String(j), x + 3, y - 5);
                    }
                }
                if (sketch.triangles)
                    c.closePath();
                c.strokeStyle = sketch.guide ? ["#ce832b", "#139dba", "#648c70", "#648c70"][i] : sketch.Material.accent;
                c.setLineDash(sketch.guide && i === 1 ? [6, 4] : []);
                c.lineWidth = 1.6;
                c.stroke();
                i++;
            }
        }
    }
}
