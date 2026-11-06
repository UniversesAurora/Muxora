// SPDX-License-Identifier: MIT
import QtQuick

// A dialog's content, as QInputDialog lays it out: the text, each field
// under its label, and the buttons at the bottom right. Fields of several
// lines share the height left over.
Rectangle {
    id: root
    required property DialogWindow dialog
    readonly property SettingsStyle style: dialog.style
    readonly property real unit: style.unit
    readonly property real margin: 12 * unit
    readonly property var singleLine: dialog.fields.filter(field => field.kind !== "lines")
    readonly property var lines: dialog.fields.filter(field => field.kind === "lines")
    color: style.colors.surface
    // The window sets the width; the text wraps to it.
    implicitWidth: buttons.implicitWidth + 2 * margin
    implicitHeight: header.height + (lines.length ? 8 * unit + lines.length * (100 * unit + labelHeight) : 0)
        + buttons.height + 12 * unit + 2 * margin
    readonly property real labelHeight: metrics.height + 4 * unit
    // Typing starts in the first field with its text selected, as in
    // QInputDialog; without fields, on the accept button.
    readonly property string firstId: dialog.fields.length ? dialog.fields[0].id : ""
    function takeFocus(control) {
        if (control.objectName !== firstId) return
        // Several lines of text take the focus in their editor, not its frame.
        const target = control.editor ?? control
        target.forceActiveFocus(Qt.PopupFocusReason)
        target.selectAll()
    }

    FontMetrics { id: metrics; font: root.style.font }

    component Label: Text {
        font: root.style.font
        color: root.style.colors.text
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
    }

    Column {
        id: header
        x: root.margin
        y: root.margin
        width: root.width - 2 * root.margin
        spacing: 8 * root.unit
        Label {
            width: parent.width
            visible: text.length > 0
            text: root.dialog.text
        }
        Repeater {
            model: root.singleLine
            delegate: Column {
                id: field
                required property var modelData
                width: header.width
                spacing: 4 * root.unit
                Label { width: parent.width; visible: text.length > 0; text: field.modelData.label }
                Loader {
                    width: parent.width
                    sourceComponent: field.modelData.kind === "integer" ? numberField : textField
                    Component {
                        id: textField
                        SettingTextField {
                            objectName: field.modelData.id
                            style: root.style
                            text: field.modelData.value ?? ""
                            onTextEdited: root.dialog.setValue(field.modelData.id, text)
                            Component.onCompleted: root.takeFocus(this)
                        }
                    }
                    Component {
                        id: numberField
                        SettingSpinBox {
                            objectName: field.modelData.id
                            style: root.style
                            minimum: field.modelData.minimum
                            maximum: field.modelData.maximum
                            suffix: field.modelData.suffix
                            emptyText: field.modelData.emptyText
                            number: field.modelData.value ?? 0
                            onEdited: (number) => root.dialog.setValue(field.modelData.id, Math.round(number))
                            Component.onCompleted: root.takeFocus(this)
                        }
                    }
                }
            }
        }
    }

    Column {
        id: linesArea
        x: root.margin
        y: header.y + header.height + 8 * root.unit
        width: header.width
        height: buttons.y - 12 * root.unit - y
        visible: root.lines.length > 0
        Repeater {
            model: root.lines
            delegate: Column {
                id: lineField
                required property var modelData
                width: linesArea.width
                height: linesArea.height / root.lines.length
                spacing: 4 * root.unit
                Label { id: lineLabel; width: parent.width; visible: text.length > 0; text: lineField.modelData.label }
                SettingTextArea {
                    objectName: lineField.modelData.id
                    style: root.style
                    width: parent.width
                    height: parent.height - (lineLabel.visible ? lineLabel.height + parent.spacing : 0)
                    text: lineField.modelData.value ?? ""
                    onTextChanged: root.dialog.setValue(lineField.modelData.id, text)
                    Component.onCompleted: root.takeFocus(this)
                }
            }
        }
    }

    Row {
        id: buttons
        anchors.right: parent.right
        anchors.rightMargin: root.margin
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.margin
        spacing: 6 * root.unit
        SettingButton {
            objectName: "DialogAccept"
            style: root.style
            text: root.dialog.acceptText
            enabled: root.dialog.acceptable
            implicitWidth: Math.max(80 * root.unit, implicitContentWidth + leftPadding + rightPadding)
            onClicked: root.dialog.accept()
            Component.onCompleted: if (!root.firstId) forceActiveFocus(Qt.PopupFocusReason)
        }
        SettingButton {
            objectName: "DialogCancel"
            style: root.style
            visible: root.dialog.cancellable
            text: root.dialog.cancelText
            implicitWidth: Math.max(80 * root.unit, implicitContentWidth + leftPadding + rightPadding)
            onClicked: root.dialog.reject()
        }
    }
}
