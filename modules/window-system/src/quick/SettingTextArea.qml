// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Basic as Basic

// Several lines of text, as the Widgets plain text editor draws them: the
// text field's look, wrapping, scrolling once the text is taller.
Basic.ScrollView {
    id: control
    required property SettingsStyle style
    readonly property real unit: style.unit
    property alias text: area.text
    property alias placeholderText: area.placeholderText
    readonly property alias editor: area
    property real minimumHeight: 54 * unit
    property real maximumHeight: 100 * unit
    implicitWidth: 320 * unit
    implicitHeight: Math.min(maximumHeight, Math.max(minimumHeight, area.implicitHeight + 2 * unit))
    padding: control.unit
    background: Rectangle {
        color: control.enabled ? control.style.colors.surface : control.style.colors.raised
        border.width: control.unit
        border.color: area.activeFocus ? control.style.colors.focus : control.style.colors.border
    }
    Basic.TextArea {
        id: area
        font: control.style.font
        color: control.enabled ? control.style.colors.text : control.style.colors.muted
        placeholderTextColor: control.style.colors.muted
        selectionColor: control.style.colors.selection
        selectedTextColor: control.style.colors.selectionText
        wrapMode: TextEdit.Wrap
        selectByMouse: true
        padding: 4 * control.unit
        background: null
    }
}
