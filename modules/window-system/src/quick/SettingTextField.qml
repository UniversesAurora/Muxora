// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Basic as Basic

// A text field as the Widgets style draws it: the surface, a border that
// takes the focus color, muted placeholder text.
Basic.TextField {
    id: control
    required property SettingsStyle style
    readonly property real unit: style.unit
    font: style.font
    implicitHeight: style.controlHeight
    color: enabled ? style.colors.text : style.colors.muted
    placeholderTextColor: style.colors.muted
    selectionColor: style.colors.selection
    selectedTextColor: style.colors.selectionText
    leftPadding: 5 * unit
    rightPadding: 5 * unit
    verticalAlignment: TextInput.AlignVCenter
    background: Rectangle {
        color: control.enabled ? control.style.colors.surface : control.style.colors.raised
        border.width: control.unit
        border.color: control.activeFocus ? control.style.colors.focus : control.style.colors.border
    }
}
