// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.Menu {
    id: control
    required property PopupMenu session
    property MenuEntry entry
    property real contentWidthHint: 0
    required property size maximumSize
    implicitWidth: contentWidthHint + leftPadding + rightPadding
    width: Math.min(implicitWidth, maximumSize.width)
    height: Math.min(implicitHeight, maximumSize.height)
    title: entry ? entry.title : ""
    enabled: !entry || (entry.enabled && entry.visible)
    font: session.menuFont
    palette.window: session.menuPalette.window
    palette.windowText: session.menuPalette.windowText
    palette.mid: session.menuPalette.mid
    palette.highlight: session.menuPalette.highlight
    palette.highlightedText: session.menuPalette.highlightedText
    popupType: Basic.Popup.Window
    modal: true
    dim: false
    cascade: true
    margins: 0
    padding: 3 * session.scale
    overlap: 1
    enter: null
    exit: null
    delegate: PopupMenuRow {
        session: control.session
        entry: subMenu ? (subMenu as PopupMenuNode).entry : null
    }
    onClosed: if (!entry) session.finished()
    // A host query can destroy the workspace. Qt continues using this popup
    // after aboutToShow returns, so query after its opening handler unwinds.
    onAboutToShow: session.requestRefresh()
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
    function showAt(anchor: Item, point: point): void { popup(anchor, point) }
    function measureContent(): void {
        let hint = 0
        for (let index = 0; index < count; ++index) {
            // Effective visibility is false while the popup is closed.
            // Measure the logical command state before opening its window.
            const item = itemAt(index) as PopupMenuRow
            if (item && item.entry && item.entry.visible) hint = Math.max(hint, item.implicitWidth)
        }
        contentWidthHint = hint
    }
    function selectBoundary(last: bool): void {
        for (let offset = 0; offset < count; ++offset) {
            const index = last ? count - 1 - offset : offset
            const item = itemAt(index)
            if (item && item.visible && item.enabled && item.activeFocusOnTab) {
                currentIndex = index
                return
            }
        }
    }
    contentItem: ListView {
        implicitHeight: contentHeight
        model: control.contentModel
        currentIndex: control.currentIndex
        clip: true
        interactive: contentHeight > height
        Basic.ScrollIndicator.vertical: Basic.ScrollIndicator {}
    }
    background: Rectangle {
        color: control.palette.window
        border.color: control.palette.mid
        border.width: 1
    }
}
