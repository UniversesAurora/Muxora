import QtQuick

// The file page's address bar above its panes: the path as a read-only
// field across the pane, as the Widgets preview showed it. The text can be
// selected and copied.
Rectangle {
    id: root
    required property string text
    required property var colors
    required property font bodyFont
    required property real scale
    implicitHeight: Math.round(Math.max(metrics.height + 6 * scale, 24 * scale))
    color: colors.surface
    border.width: Math.max(1, Math.round(scale))
    border.color: colors.border

    FontMetrics { id: metrics; font: root.bodyFont }

    TextInput {
        x: 5 * root.scale
        width: parent.width - 10 * root.scale
        height: parent.height
        verticalAlignment: TextInput.AlignVCenter
        clip: true
        readOnly: true
        selectByMouse: true
        text: root.text
        font: root.bodyFont
        color: root.colors.text
        selectionColor: root.colors.selection
        selectedTextColor: root.colors.selectionText
    }
}
