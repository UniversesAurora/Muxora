// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Basic as Basic

// Rows under column headers, as the Widgets item views draw them:
// alternating row colors, a hover tint, the current row selected, arrow keys
// to move and Enter or a double click to activate. `columns` are
// {title, role, fit}: a fit column is as wide as its widest text, the others
// share the rest. `rows` are objects with those roles; `toolTipRole` names
// the text shown while a row is hovered.
Rectangle {
    id: control
    required property SettingsStyle style
    readonly property real unit: style.unit
    property var columns: []
    property var rows: []
    property string toolTipRole
    property alias currentIndex: list.currentIndex
    readonly property alias count: list.count
    signal activated(int index)
    readonly property real rowHeight: metrics.height + 8 * unit
    readonly property var widths: {
        const padding = 12 * unit
        let fixed = 0, shared = 0
        const result = columns.map(column => {
            if (!column.fit) { ++shared; return -1 }
            let widest = metrics.advanceWidth(column.title)
            for (const row of rows) widest = Math.max(widest, metrics.advanceWidth(String(row[column.role] ?? "")))
            fixed += widest + padding
            return widest + padding
        })
        const rest = Math.max(0, (width - 2 * unit - fixed) / Math.max(1, shared))
        return result.map(width => width < 0 ? rest : width)
    }
    implicitHeight: 200 * unit
    color: style.colors.surface
    border.width: unit
    border.color: list.activeFocus ? style.colors.focus : style.colors.border

    FontMetrics { id: metrics; font: control.style.font }

    Row {
        id: header
        x: control.unit
        y: control.unit
        Repeater {
            model: control.columns
            delegate: Rectangle {
                required property int index
                required property var modelData
                width: control.widths[index] ?? 0
                height: control.rowHeight
                color: control.style.colors.raised
                Text {
                    x: 6 * control.unit
                    width: parent.width - 12 * control.unit
                    height: parent.height
                    text: parent.modelData.title
                    font: control.style.font
                    color: control.style.colors.text
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
                Rectangle { anchors.right: parent.right; width: control.unit; height: parent.height; color: control.style.colors.border }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: control.unit; color: control.style.colors.border }
            }
        }
    }

    ListView {
        id: list
        objectName: "SettingTableRows"
        x: control.unit
        y: header.y + control.rowHeight
        width: control.width - 2 * control.unit
        height: control.height - y - control.unit
        clip: true
        model: control.rows
        activeFocusOnTab: true
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        highlightMoveDuration: 0
        Basic.ScrollBar.vertical: Basic.ScrollBar {}
        Keys.onReturnPressed: if (currentIndex >= 0) control.activated(currentIndex)
        Keys.onEnterPressed: if (currentIndex >= 0) control.activated(currentIndex)
        delegate: Rectangle {
            id: row
            required property int index
            required property var modelData
            readonly property bool current: ListView.isCurrentItem
            width: list.width
            height: control.rowHeight
            color: current ? control.style.colors.selection : hover.hovered ? control.style.colors.soft
                 : index % 2 ? control.style.colors.raised : control.style.colors.surface
            HoverHandler {
                id: hover
                onHoveredChanged: hovered && control.toolTipRole
                    ? control.style.showToolTip(row, String(row.modelData[control.toolTipRole] ?? ""))
                    : control.style.hideToolTip(row)
            }
            TapHandler {
                onTapped: { list.currentIndex = row.index; list.forceActiveFocus(Qt.MouseFocusReason) }
                onDoubleTapped: control.activated(row.index)
            }
            Row {
                Repeater {
                    model: control.columns
                    delegate: Text {
                        required property int index
                        required property var modelData
                        width: control.widths[index] ?? 0
                        height: control.rowHeight
                        leftPadding: 6 * control.unit
                        rightPadding: 6 * control.unit
                        text: String(row.modelData[modelData.role] ?? "")
                        font: control.style.font
                        color: row.current ? control.style.colors.selectionText : control.style.colors.text
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }
    }
}
