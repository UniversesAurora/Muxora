// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Shapes

Shape {
    id: root
    property string svgPath
    property color lineColor: "transparent"
    property real lineWidth: 1
    preferredRendererType: Shape.CurveRenderer

    ShapePath {
        fillColor: "transparent"
        strokeColor: root.lineColor
        strokeWidth: root.lineWidth
        joinStyle: ShapePath.RoundJoin
        capStyle: ShapePath.RoundCap
        PathSvg { path: root.svgPath }
    }
}
