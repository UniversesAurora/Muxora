// SPDX-License-Identifier: MIT
import QtQuick

// A key recorder drawn as the text field: the recorded keys, or the
// placeholder while empty. While it has the focus every key is recorded and
// no shortcut runs (KeySequenceCapture).
KeySequenceCapture {
    id: control
    required property SettingsStyle style
    readonly property real unit: style.unit
    property string placeholderText
    implicitWidth: 240 * unit
    implicitHeight: style.controlHeight
    Accessible.role: Accessible.EditableText
    Accessible.description: displayText

    Rectangle {
        anchors.fill: parent
        color: control.enabled ? control.style.colors.surface : control.style.colors.raised
        border.width: control.unit
        border.color: control.activeFocus ? control.style.colors.focus : control.style.colors.border
    }
    Text {
        x: 5 * control.unit
        width: parent.width - 10 * control.unit
        height: parent.height
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        font: control.style.font
        text: control.displayText || control.placeholderText
        color: control.displayText && control.enabled ? control.style.colors.text : control.style.colors.muted
    }
}
