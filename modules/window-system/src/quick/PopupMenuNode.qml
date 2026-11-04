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
    function appendMenu(child: QtObject): void { addMenu(child as PopupMenuNode) }
    function showAt(anchor: Item, point: point): void { popup(anchor, point) }
    function selectBoundary(last: bool): void {
        for (let offset = 0; offset < count; ++offset) {
            const index = last ? count - 1 - offset : offset
            const item = itemAt(index)
            if (item && item.enabled && item.activeFocusOnTab) {
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
