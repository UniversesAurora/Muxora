// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Basic as Basic

// A drop-down as the Widgets style draws it. Choices are {label, value,
// isDefault}; the list marks the default entry at the row's end, as VS Code
// settings do (ui design §4.7).
Basic.ComboBox {
    id: control
    required property SettingsStyle style
    readonly property real unit: style.unit
    // The value of the chosen entry; activated(index) reports a choice.
    property var currentChoice
    font: style.font
    textRole: "label"
    valueRole: "value"
    currentIndex: {
        for (let index = 0; index < count; ++index)
            if (model[index].value === currentChoice) return index
        return -1
    }
    implicitHeight: style.controlHeight
    leftPadding: 5 * unit
    rightPadding: 20 * unit
    contentItem: Text {
        text: control.displayText
        font: control.font
        color: control.enabled ? control.style.colors.text : control.style.colors.muted
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Text {
        x: control.width - width - 6 * control.unit
        y: (control.height - height) / 2
        text: "▾"
        font: control.font
        color: control.enabled ? control.style.colors.text : control.style.colors.muted
    }
    background: Rectangle {
        color: control.enabled ? control.style.colors.surface : control.style.colors.raised
        border.width: control.unit
        border.color: control.activeFocus || control.popup.visible ? control.style.colors.focus : control.style.colors.border
    }
    delegate: Basic.ItemDelegate {
        id: row
        required property int index
        required property var modelData
        readonly property bool selected: control.highlightedIndex === index
        width: ListView.view ? ListView.view.width : implicitWidth
        height: control.style.controlHeight
        padding: 0
        leftPadding: 6 * control.unit
        rightPadding: 6 * control.unit
        contentItem: Item {
            implicitWidth: label.implicitWidth + (mark.visible ? mark.implicitWidth + 16 * control.unit : 0)
            Text {
                id: label
                width: parent.width - (mark.visible ? mark.width + 16 * control.unit : 0)
                height: parent.height
                text: row.modelData.label
                font: control.font
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
                color: row.selected ? control.style.colors.selectionText : control.style.colors.text
            }
            Text {
                id: mark
                anchors.right: parent.right
                height: parent.height
                visible: row.modelData.isDefault === true
                text: control.style.defaultText
                font: control.font
                verticalAlignment: Text.AlignVCenter
                color: row.selected ? control.style.colors.selectionText : control.style.colors.accent
            }
        }
        background: Rectangle { color: row.selected ? control.style.colors.selection : "transparent" }
    }
    popup: Basic.Popup {
        y: control.height
        width: Math.max(control.width, list.contentItem.childrenRect.width)
        implicitHeight: Math.min(list.contentHeight + 2 * padding, 360 * control.unit)
        padding: 3 * control.unit
        popupType: Basic.Popup.Window
        contentItem: ListView {
            id: list
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
        }
        background: Rectangle {
            color: control.style.colors.surface
            border.width: control.unit
            border.color: control.style.colors.border
        }
    }
}
