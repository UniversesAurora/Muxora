// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Basic as Basic

// A number box as the Widgets style draws it, for whole or decimal numbers
// with an optional unit. The unit is only shown: typing reads the leading
// number and finishing restores the unit; text without a number keeps the
// last valid value. Its arrows show while hovered or focused.
Basic.SpinBox {
    id: control
    required property SettingsStyle style
    readonly property real unit: style.unit
    property int decimals: 0
    property string suffix: ""
    // Shown for the minimum, which then means none (QSpinBox's special value).
    property string emptyText: ""
    property real minimum: 0
    property real maximum: 100
    property real step: 1
    readonly property real factor: Math.pow(10, decimals)
    // The value in its own unit; `value` holds it scaled to an integer.
    property real number: 0
    signal edited(real number)
    font: style.font
    editable: true
    from: Math.round(minimum * factor)
    to: Math.round(maximum * factor)
    stepSize: Math.max(1, Math.round(step * factor))
    value: Math.round(number * factor)
    implicitHeight: style.controlHeight
    implicitWidth: Math.max(96 * unit, contentItem.implicitWidth + 2 * up.indicator.width)
    leftPadding: 5 * unit
    rightPadding: up.indicator.width + 2 * unit
    validator: null
    textFromValue: (value, locale) => emptyText && value === from ? emptyText : (value / factor).toFixed(decimals) + suffix
    valueFromText: (text, locale) => {
        if (emptyText && text === emptyText) return from
        const parsed = parseFloat(text)
        return isNaN(parsed) ? value : Math.round(Math.min(maximum, Math.max(minimum, parsed)) * factor)
    }
    onValueModified: edited(value / factor)
    function selectAll() { contentItem.selectAll() }
    contentItem: TextInput {
        text: control.displayText
        font: control.font
        color: control.enabled ? control.style.colors.text : control.style.colors.muted
        selectionColor: control.style.colors.selection
        selectedTextColor: control.style.colors.selectionText
        selectByMouse: true
        readOnly: !control.editable
        verticalAlignment: TextInput.AlignVCenter
        inputMethodHints: Qt.ImhFormattedNumbersOnly
    }
    readonly property bool arrowsShown: (hovered || activeFocus) && enabled
    up.indicator: Rectangle {
        x: control.width - width - control.unit
        y: control.unit
        width: 16 * control.unit
        height: control.height / 2 - control.unit
        visible: control.arrowsShown
        color: control.up.pressed ? control.style.colors.selection : control.up.hovered ? control.style.colors.buttonHover : "transparent"
        Text { anchors.centerIn: parent; text: "▴"; font: control.font; color: control.style.colors.text }
    }
    down.indicator: Rectangle {
        x: control.width - width - control.unit
        y: control.height / 2
        width: 16 * control.unit
        height: control.height / 2 - control.unit
        visible: control.arrowsShown
        color: control.down.pressed ? control.style.colors.selection : control.down.hovered ? control.style.colors.buttonHover : "transparent"
        Text { anchors.centerIn: parent; text: "▾"; font: control.font; color: control.style.colors.text }
    }
    background: Rectangle {
        color: control.enabled ? control.style.colors.surface : control.style.colors.raised
        border.width: control.unit
        border.color: control.activeFocus ? control.style.colors.focus : control.style.colors.border
    }
}
