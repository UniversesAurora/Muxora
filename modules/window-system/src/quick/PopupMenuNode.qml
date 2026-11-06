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
    // Submenus query current state as they open; a root was described just
    // before open(). A host query can destroy the workspace, and Qt continues
    // using this popup after aboutToShow returns, so query after it unwinds.
    onAboutToShow: {
        // Qt measures and selects a submenu before opening it. Prepare its
        // rows one level ahead, using the retained description only.
        session.prepareSubmenus(control)
        if (entry) session.requestCatalogRefresh()
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
        id: list
        implicitHeight: contentHeight
        model: control.contentModel
        // Menu owns keyboard selection and focus. A second current-item
        // owner feeds focus back during model moves and removals.
        currentIndex: -1
        clip: true
        interactive: contentHeight > height
        function revealCurrent(): void {
            if (control.currentIndex >= 0) positionViewAtIndex(control.currentIndex, ListView.Contain)
        }
        Connections {
            target: control
            function onCurrentIndexChanged(): void { Qt.callLater(list.revealCurrent) }
        }
        Basic.ScrollIndicator.vertical: Basic.ScrollIndicator {}
    }
    background: Rectangle {
        color: control.palette.window
        border.color: control.palette.mid
        border.width: 1
    }
}
