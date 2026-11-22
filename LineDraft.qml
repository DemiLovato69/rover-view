import QtQuick

QtObject {
    id: draft
    property bool active: false
    property var vertices: []
    property string name: ""
    property string error: ""
    function begin() {
        if (active || !collector.state.job)
            return;
        let n = 1;
        while (collector.features.some(f => f.name === "Line " + n))
            ++n;
        name = "Line " + n;
        vertices = [];
        error = "";
        active = true;
    }
    function append(p) {
        if (!active)
            return;
        let last = vertices[vertices.length - 1];
        if (last && last.latitudeDeg === p.latitudeDeg && last.longitudeDeg === p.longitudeDeg) {
            error = "Choose a different next point";
            return;
        }
        vertices = vertices.concat([
            {
                id: p.id || "",
                gpst: p.gpst || "",
                latitudeDeg: p.latitudeDeg,
                longitudeDeg: p.longitudeDeg
            }
        ]);
        error = "";
    }
    function label(i) {
        return vertices[i].id || "Map corner " + (i + 1);
    }
    function remove(i) {
        vertices = vertices.filter((_, j) => i !== j);
        error = "";
    }
    function move(i, delta) {
        let a = vertices.slice();
        a.splice(i + delta, 0, a.splice(i, 1)[0]);
        vertices = a;
        error = "";
    }
    function cancel() {
        active = false;
        vertices = [];
        error = "";
    }
    function save() {
        if (!collector.createLine(name, vertices)) {
            error = collector.state.notice;
            return false;
        }
        cancel();
        return true;
    }
}
