// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Basic as Basic

// The hover tip of Quick controls, drawn as the Widgets tooltip style.
Basic.ToolTip {
    id: control
    required property color fill
    required property color stroke
    required property color ink
    required property real cornerRadius
    required property real unit
    popupType: Basic.Popup.Window
    closePolicy: Basic.Popup.NoAutoClose
    margins: 0
    topPadding: 4 * unit
    bottomPadding: 4 * unit
    leftPadding: 6 * unit
    rightPadding: 6 * unit
    enter: null
    exit: null
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.ink
        textFormat: Text.PlainText
    }
    background: Rectangle {
        color: control.fill
        border.color: control.stroke
        border.width: control.unit
        radius: control.cornerRadius
    }
}
