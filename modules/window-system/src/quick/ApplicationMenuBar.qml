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
    readonly property Item overflowButton: more
    function entryAt(index: int): MenuEntry {
        const button = itemAt(index) as Basic.MenuBarItem
        return button && button.menu ? (button.menu as PopupMenuNode).entry : null
    }
    readonly property real naturalWidth: {
        let total = 0
        for (let index = 0; index < count; ++index) {
            const entry = entryAt(index)
            if (entry && entry.visible) total += entry.content.implicitWidth
        }
        return total
    }
    implicitWidth: naturalWidth
    implicitHeight: {
        let result = session.overflow.entry.content.implicitHeight
        for (let index = 0; index < count; ++index) {
            const entry = entryAt(index)
            if (entry && entry.visible) result = Math.max(result, entry.content.implicitHeight)
        }
        return result
    }
    readonly property real minimumWidthHint: {
        for (let index = 0; index < count; ++index) {
            const entry = entryAt(index)
            if (entry && entry.visible) return Math.min(naturalWidth, entry.content.implicitWidth + session.overflow.extent)
        }
        return 0
    }
    readonly property int firstHiddenIndex: {
        if (naturalWidth <= width) return count
        const available = Math.max(0, width - session.overflow.extent)
        let used = 0
        for (let index = 0; index < count; ++index) {
            const entry = entryAt(index)
            if (!entry || !entry.visible) continue
            if (used + entry.content.implicitWidth > available) return index
            used += entry.content.implicitWidth
        }
        return count
    }
    onFirstHiddenIndexChanged: session.overflow.updateShortcuts()
    onVisibleChanged: session.overflow.updateShortcuts()
    readonly property bool popupOpen: {
        if (session.overflow.opened) return true
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
        property int barIndex: -1
        readonly property MenuEntry entry: menu ? (menu as PopupMenuNode).entry : null
        objectName: entry ? entry.id : ""
        text: entry ? entry.title : ""
        visible: entry ? entry.visible && (barIndex < 0 || barIndex < control.firstHiddenIndex) : false
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
    background: Item {
        z: 1
        Basic.ToolButton {
            id: more
            objectName: "MenuOverflowButton"
            x: Math.max(0, control.width - width)
            width: control.session.overflow.extent
            height: control.height
            visible: control.firstHiddenIndex < control.count
            text: control.session.overflow.entry.title
            focusPolicy: Qt.TabFocus
            down: control.session.overflow.opened
            padding: 0
            contentItem: control.session.overflow.entry.content
            onClicked: control.session.overflow.open()
            background: Rectangle {
                color: more.down ? control.session.menuPalette.highlight
                     : more.hovered ? control.session.hoverFill : "transparent"
            }
        }
    }
}
