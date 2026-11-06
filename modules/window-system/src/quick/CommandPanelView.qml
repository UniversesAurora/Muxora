// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Basic as Basic

// The command palette and window selector, drawn with the Widgets rules:
// the panel's accent border, a search field, a list with column headers
// and alternating rows, a muted hint and two buttons.
Rectangle {
    id: root
    required property CommandPanel panel
    readonly property real unit: panel.unit
    readonly property var colors: panel.colors
    color: colors.surface
    border.color: colors.accent
    border.width: unit

    component PanelButton: Basic.Button {
        id: button
        font: root.panel.font
        implicitHeight: root.panel.controlHeight
        leftPadding: 7 * root.unit
        rightPadding: 7 * root.unit
        contentItem: Text {
            text: button.text
            font: button.font
            color: button.enabled ? root.colors.text : root.colors.muted
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: button.down ? root.colors.selection : button.hovered && button.enabled ? root.colors.buttonHover : root.colors.raised
            border.width: root.unit
            border.color: !button.enabled ? root.colors.border
                : button.activeFocus ? root.colors.focus
                : button.hovered || button.down || button.highlighted ? root.colors.accent : root.colors.border
        }
    }

    Basic.TextField {
        id: search
        objectName: "PanelSearch"
        x: 12 * root.unit
        y: 12 * root.unit
        width: root.width - 24 * root.unit
        height: root.panel.controlHeight
        focus: true
        font: root.panel.font
        color: root.colors.text
        placeholderText: root.panel.placeholder
        placeholderTextColor: root.colors.muted
        selectionColor: root.colors.selection
        selectedTextColor: root.colors.selectionText
        leftPadding: 5 * root.unit
        rightPadding: 5 * root.unit
        background: Rectangle {
            color: root.colors.surface
            border.width: root.unit
            border.color: search.activeFocus ? root.colors.focus : root.colors.border
        }
        onTextChanged: root.panel.search(text)
        onAccepted: root.panel.accept()
    }

    Rectangle {
        id: frame
        x: search.x
        y: search.y + search.height + 8 * root.unit
        width: search.width
        height: hint.y - 8 * root.unit - y
        color: root.colors.surface
        border.width: root.unit
        border.color: root.colors.border
        clip: true

        Row {
            id: header
            x: root.unit
            y: root.unit
            Repeater {
                model: root.panel.columns
                Rectangle {
                    required property int index
                    required property string modelData
                    width: index === 0 ? list.firstColumn : root.panel.columnWidths[index]
                    height: root.panel.rowHeight + 2 * root.unit
                    color: root.colors.raised
                    Rectangle { anchors.right: parent.right; width: root.unit; height: parent.height; color: root.colors.border }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: root.unit; color: root.colors.border }
                    Text {
                        x: 6 * root.unit
                        width: parent.width - 12 * root.unit
                        height: parent.height
                        text: modelData
                        font: root.panel.font
                        color: root.colors.text
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        ListView {
            id: list
            objectName: "PanelList"
            x: root.unit
            y: header.y + header.height
            width: frame.width - 2 * root.unit
            height: frame.height - y - root.unit
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.panel.rows
            currentIndex: root.panel.current
            highlightMoveDuration: 0
            readonly property real firstColumn: {
                let rest = 0
                for (let column = 1; column < root.panel.columnWidths.length; ++column) rest += root.panel.columnWidths[column]
                return Math.max(0, width - rest)
            }
            onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
            Basic.ScrollBar.vertical: Basic.ScrollBar {}
            delegate: Rectangle {
                id: row
                required property int index
                required property var modelData
                objectName: "PanelRow/" + modelData.id
                readonly property bool selected: index === root.panel.current
                width: list.width
                height: root.panel.rowHeight
                color: selected ? root.colors.selection : index % 2 ? root.colors.raised : root.colors.surface
                PanelIcon {
                    id: icon
                    objectName: "PanelRowIcon"
                    x: 4 * root.unit
                    anchors.verticalCenter: parent.verticalCenter
                    visible: row.modelData.icon.length > 0
                    panel: root.panel
                    name: row.modelData.icon
                    selected: row.selected
                }
                Row {
                    Repeater {
                        model: row.modelData.cells
                        Text {
                            required property int index
                            required property string modelData
                            width: index === 0 ? list.firstColumn : root.panel.columnWidths[index]
                            height: row.height
                            leftPadding: index === 0 && icon.visible ? icon.x + icon.width + 4 * root.unit : 6 * root.unit
                            rightPadding: 6 * root.unit
                            text: modelData
                            font: root.panel.font
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                            color: !row.modelData.enabled ? root.colors.muted
                                : row.selected ? root.colors.selectionText : root.colors.text
                        }
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.panel.select(row.index)
                    onDoubleClicked: { root.panel.select(row.index); root.panel.accept() }
                }
            }
        }
    }

    Text {
        id: hint
        objectName: "PanelHint"
        x: search.x
        y: buttons.y - 8 * root.unit - height
        width: search.width
        text: root.panel.hint
        font: root.panel.font
        color: root.colors.muted
        wrapMode: Text.Wrap
    }

    Row {
        id: buttons
        anchors.right: parent.right
        anchors.rightMargin: 12 * root.unit
        y: root.height - 12 * root.unit - height
        spacing: 6 * root.unit
        PanelButton {
            objectName: "PanelAccept"
            text: root.panel.acceptText
            enabled: root.panel.canAccept
            highlighted: true
            onClicked: root.panel.accept()
        }
        PanelButton {
            objectName: "PanelReject"
            text: root.panel.rejectText
            onClicked: root.panel.reject()
        }
    }
}
