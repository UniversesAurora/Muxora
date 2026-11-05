// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Basic as Basic

// A whole number set by a slider over its common part next to a number box
// for its full range (ui design §4.7). A value beyond the slider's range is
// kept; the slider only shows its end until moved.
Row {
    id: control
    required property SettingsStyle style
    readonly property real unit: style.unit
    property int sliderMaximum: 1000
    property alias minimum: box.minimum
    property alias maximum: box.maximum
    property alias step: box.step
    property alias suffix: box.suffix
    property real number: 0
    signal edited(real number)
    spacing: 8 * unit
    Basic.Slider {
        id: slider
        anchors.verticalCenter: parent.verticalCenter
        width: 200 * control.unit
        from: Math.max(0, box.minimum)
        to: control.sliderMaximum
        stepSize: 1
        value: control.number
        enabled: control.enabled
        onMoved: control.edited(Math.round(value))
        background: Rectangle {
            x: slider.leftPadding
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth
            height: 4 * control.unit
            color: control.style.colors.raised
            border.width: control.unit
            border.color: control.style.colors.border
            Rectangle {
                width: slider.visualPosition * parent.width
                height: parent.height
                color: slider.enabled ? control.style.colors.accent : control.style.colors.muted
            }
        }
        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            implicitWidth: 8 * control.unit
            implicitHeight: 16 * control.unit
            color: control.style.colors.surface
            border.width: control.unit
            border.color: slider.enabled ? control.style.colors.accent : control.style.colors.border
        }
    }
    SettingSpinBox {
        id: box
        anchors.verticalCenter: parent.verticalCenter
        style: control.style
        enabled: control.enabled
        number: control.number
        onEdited: (number) => control.edited(number)
    }
}
