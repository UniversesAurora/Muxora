// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Basic as Basic

// A push button as the Widgets style draws it: the raised fill, an accent
// border and tint while hovered, the selection while pressed or checked,
// the focus border while focused. A tip shows while hovered.
Basic.Button {
    id: control
    required property SettingsStyle style
    readonly property real unit: style.unit
    property string toolTip
    font: style.font
    implicitHeight: style.controlHeight
    padding: 0
    leftPadding: 8 * unit
    rightPadding: 8 * unit
    onHoveredChanged: hovered && toolTip ? style.showToolTip(control, toolTip) : style.hideToolTip(control)
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? control.style.colors.text : control.style.colors.muted
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        color: !control.enabled ? control.style.colors.raised
             : control.down || control.checked ? control.style.colors.selection
             : control.hovered ? control.style.colors.buttonHover : control.style.colors.raised
        border.width: control.unit
        border.color: !control.enabled ? control.style.colors.border
                    : control.activeFocus ? control.style.colors.focus
                    : control.down || control.checked || control.hovered ? control.style.colors.accent : control.style.colors.border
    }
}
