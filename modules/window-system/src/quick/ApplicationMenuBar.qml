// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.MenuBar {
    id: control
    required property PopupMenu session
    objectName: "MenuStrip"
    padding: 0
    spacing: 0
    font: session.menuFont
    readonly property bool popupOpen: {
        for (let index = 0; index < count; ++index) {
            const branch = menuAt(index)
            if (branch && branch.visible) return true
        }
        return false
    }
    function insertBranch(index: int, child: QtObject): Item {
        insertMenu(index, child as PopupMenuNode)
        return itemAt(index)
    }
    function removeBranch(child: QtObject): void { removeMenu(child as PopupMenuNode) }
    function placeItem(index: int, item: Item): void {
        if (itemAt(index) === item) return
        for (let from = 0; from < count; ++from) {
            if (itemAt(from) === item) { moveItem(from, index); return }
        }
    }
    function dismiss(): void {
        for (let index = 0; index < count; ++index) {
            const branch = menuAt(index)
            if (branch) branch.dismiss()
        }
    }
    delegate: Basic.MenuBarItem {
        id: button
        readonly property MenuEntry entry: menu ? (menu as PopupMenuNode).entry : null
        objectName: entry ? entry.id : ""
        text: entry ? entry.title : ""
        visible: entry ? entry.visible : false
        enabled: entry ? entry.enabled && entry.visible : false
        padding: 0
        implicitWidth: contentItem ? contentItem.implicitWidth : 0
        implicitHeight: contentItem ? contentItem.implicitHeight : 0
        width: entry && entry.visible ? implicitWidth : 0
        contentItem: entry ? entry.content : null
        onHighlightedChanged: if (entry) entry.setHighlighted(highlighted)
        background: Rectangle {
            color: button.down ? control.session.menuPalette.highlight
                 : button.highlighted ? control.session.hoverFill : "transparent"
        }
    }
    background: null
}
