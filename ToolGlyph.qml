import QtQuick
import QtQuick.Shapes

Item {
    id: glyph
    required property string symbol
    required property color ink
    implicitWidth: 24
    implicitHeight: 24
    readonly property var paths: ({
            line: "M3 17h5v5H3z M16 3h5v5h-5z M7 17l10-10",
            follow: "M4 21L15 3 M12 4l4-2 1 5 M10 15h11 M17 11l4 4-4 4",
            coverage: "M3 5l14-2 4 14-15 4z M5 12l9-8 M6 18L18 7 M12 19l8-8",
            design: "M2 18l5-12 8 3 7 11z M7 6l5 13 3-10 M12 19l10 1",
            grade: "M6 2h11v20H6z M6 6h5 M6 10h3 M6 14h5 M6 18h3 M20 9l-3 3 3 3",
            surface: "M12 2l9 5v10l-9 5-9-5V7z M3 7l9 5 9-5 M12 12v10",
            level: "M21 12a9 9 0 1 1-18 0 9 9 0 1 1 18 0z M14 10a3 3 0 1 1-6 0 3 3 0 1 1 6 0z M12 3v3 M3 12h3 M18 12h3 M12 18v3"
        })
    Shape {
        width: 24
        height: 24
        anchors.centerIn: parent
        ShapePath {
            fillColor: "transparent"
            strokeColor: glyph.ink
            strokeWidth: 1.8
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg {
                path: glyph.paths[glyph.symbol] || ""
            }
        }
    }
}
