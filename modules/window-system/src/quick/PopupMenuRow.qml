// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.MenuItem {
    id: control
    required property PopupMenu session
    required property MenuEntry entry
    objectName: entry ? entry.id : ""
    text: entry ? entry.title : ""
    visible: entry ? entry.visible : false
    // Qt Menu's arrow navigation skips disabled rows, including retained
    // rows that are currently hidden by their command state.
    enabled: entry ? entry.enabled && entry.visible : false
    checkable: entry ? entry.checkable : false
    checked: entry ? entry.checked : false
    font: session.menuFont
    padding: 0
    implicitWidth: contentItem ? contentItem.implicitWidth : 0
    implicitHeight: contentItem ? contentItem.implicitHeight : 0
    height: visible ? implicitHeight : 0
    contentItem: entry ? entry.content : null
    indicator: null
    arrow: null
    onHighlightedChanged: if (entry) entry.setHighlighted(highlighted)
    hoverEnabled: true
    onHoveredChanged: if (entry) entry.setHovered(hovered)
    onTriggered: if (entry) entry.activate()
    Basic.ToolTip.visible: hovered && entry && entry.tooltip.length > 0
    Basic.ToolTip.text: entry ? entry.tooltip : ""
    Basic.ToolTip.delay: 700
    background: Rectangle {
        color: control.highlighted || (control.entry && control.entry.persistentHighlight)
               ? control.session.menuPalette.highlight : "transparent"
    }
}
