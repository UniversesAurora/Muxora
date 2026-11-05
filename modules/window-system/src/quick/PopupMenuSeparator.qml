// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.MenuSeparator {
    id: control
    required property PopupMenu session
    required property MenuEntry entry
    visible: entry.visible
    height: entry.visible ? implicitHeight : 0
    padding: 0
    verticalPadding: 3 * session.scale
    horizontalPadding: 5 * session.scale
    contentItem: Rectangle {
        implicitWidth: 1
        implicitHeight: 1
        color: control.session.menuPalette.mid
    }
}
