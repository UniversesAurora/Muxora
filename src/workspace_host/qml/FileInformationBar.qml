import QtQuick

// The file page's information line below its panes: the item count and
// state, in one line with the Widgets preview's margins.
Item {
    id: root
    required property string text
    required property var colors
    required property font bodyFont
    required property real scale
    implicitHeight: Math.round(label.implicitHeight + 6 * scale)

    Text {
        id: label
        x: 8 * root.scale
        width: parent.width - 16 * root.scale
        anchors.verticalCenter: parent.verticalCenter
        text: root.text
        font: root.bodyFont
        color: root.colors.text
        elide: Text.ElideRight
    }
}
