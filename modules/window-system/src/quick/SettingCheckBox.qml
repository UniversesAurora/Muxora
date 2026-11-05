// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Basic as Basic
import QtQuick.Shapes

// A check box as the Widgets style draws it: a square box, the accent fill
// and a light mark when checked, an accent border while hovered.
Basic.CheckBox {
    id: control
    required property SettingsStyle style
    readonly property real unit: style.unit
    font: style.font
    spacing: 5 * unit
    padding: 0
    indicator: Rectangle {
        x: control.leftPadding
        y: (control.height - height) / 2
        implicitWidth: 14 * control.unit
        implicitHeight: 14 * control.unit
        color: !control.enabled ? (control.checked ? control.style.colors.muted : control.style.colors.raised)
             : control.checked ? control.style.colors.accent : control.style.colors.surface
        border.width: control.unit
        border.color: control.checked && control.enabled ? control.style.colors.accent
                    : control.hovered && control.enabled ? control.style.colors.accent : control.style.colors.border
        Shape {
            anchors.fill: parent
            visible: control.checked
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: control.style.colors.check
                strokeWidth: 1.8 * control.unit
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathPolyline {
                    readonly property real s: 14 * control.unit
                    path: [Qt.point(s * 0.22, s * 0.52), Qt.point(s * 0.42, s * 0.72), Qt.point(s * 0.78, s * 0.3)]
                }
            }
        }
    }
    contentItem: Text {
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        font: control.font
        color: control.enabled ? control.style.colors.text : control.style.colors.muted
        verticalAlignment: Text.AlignVCenter
    }
}
