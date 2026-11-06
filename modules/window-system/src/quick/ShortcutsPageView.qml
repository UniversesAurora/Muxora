// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Layouts

// The existing shortcuts page using the shared settings controls and style.
FocusScope {
    id: root
    required property ShortcutsPageModel page
    readonly property SettingsStyle style: page.style
    readonly property real unit: style.unit
    Rectangle { anchors.fill: parent; color: root.style.colors.surface }
    function syncSelection() {
        commands.currentIndex = page.commandIndex
        bindings.currentIndex = page.bindingIndex
    }
    Component.onCompleted: syncSelection()
    Connections { target: root.page; function onChanged() { root.syncSelection() } }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 24 * root.unit
        anchors.rightMargin: 24 * root.unit
        anchors.topMargin: 16 * root.unit
        anchors.bottomMargin: 16 * root.unit
        spacing: 10 * root.unit
        Text {
            text: qsTranslate("WindowSystem", "Keyboard shortcuts")
            font: root.style.titleFont
            color: root.style.colors.accent
        }
        Text {
            Layout.fillWidth: true
            text: qsTranslate("WindowSystem", "Each command can have multiple bindings. A sequence may start with a combination and continue with letters, numbers, or other keys.")
            font: root.style.font
            color: root.style.colors.muted
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            SettingTextField {
                id: search
                objectName: "ShortcutSearch"
                Layout.fillWidth: true
                style: root.style
                focus: true
                placeholderText: qsTranslate("WindowSystem", "Search commands, categories, command IDs, or shortcuts…")
                onTextChanged: root.page.search(text)
            }
            SettingButton {
                id: recordSearch
                objectName: "RecordShortcutSearch"
                style: root.style
                text: qsTranslate("WindowSystem", "Record keys to search")
                toolTip: qsTranslate("WindowSystem", "Record keys to find bindings. Commands do not run while recording.")
                checkable: true
                onToggled: {
                    if (checked) {
                        searchCapture.sequence = ""
                        searchCapture.forceActiveFocus()
                    } else search.forceActiveFocus()
                }
            }
        }
        SettingKeySequenceField {
            id: searchCapture
            objectName: "ShortcutSearchCapture"
            style: root.style
            visible: recordSearch.checked
            Accessible.name: qsTranslate("WindowSystem", "Record shortcut to search")
            onEdited: if (sequence) search.text = '"' + displayText + '"'
        }
        SettingTable {
            id: commands
            objectName: "ShortcutCommands"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 80 * root.unit
            style: root.style
            columns: [
                {title: qsTranslate("WindowSystem", "Command"), role: "title"},
                {title: qsTranslate("WindowSystem", "Shortcut"), role: "shortcut", fit: true},
                {title: qsTranslate("WindowSystem", "Category"), role: "category", fit: true}
            ]
            rows: root.page.commands
            toolTipRole: "id"
            onCurrentIndexChanged: root.page.selectCommand(currentIndex)
            onActivated: capture.forceActiveFocus()
        }
        Text {
            Layout.fillWidth: true
            text: root.page.selectedTitle
            font: root.style.font
            color: root.style.colors.text
            wrapMode: Text.Wrap
        }
        Text {
            objectName: "ShortcutCommandHelp"
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.page.commandHelp
            font: root.style.font
            color: root.style.colors.muted
            wrapMode: Text.Wrap
        }
        SettingTable {
            id: bindings
            objectName: "ShortcutBindings"
            Layout.fillWidth: true
            Layout.preferredHeight: 112 * root.unit
            Layout.minimumHeight: 60 * root.unit
            style: root.style
            columns: [
                {title: qsTranslate("WindowSystem", "Shortcut"), role: "shortcut", fit: true},
                {title: qsTranslate("WindowSystem", "Arguments"), role: "arguments"}
            ]
            rows: root.page.bindings
            onCurrentIndexChanged: root.page.selectBinding(currentIndex)
        }
        SettingKeySequenceField {
            id: capture
            objectName: "ShortcutCapture"
            style: root.style
            enabled: root.page.selected
            sequence: root.page.sequence
            Accessible.name: qsTranslate("WindowSystem", "Record a command shortcut")
            onEdited: root.page.editSequence(sequence)
        }
        SettingTextArea {
            objectName: "ShortcutArguments"
            Layout.fillWidth: true
            style: root.style
            enabled: root.page.selected
            text: root.page.arguments
            placeholderText: root.page.argumentPlaceholder
            Accessible.name: qsTranslate("WindowSystem", "Shortcut arguments JSON")
            onTextChanged: root.page.editArguments(text)
        }
        Text {
            objectName: "ShortcutDiagnostic"
            Layout.fillWidth: true
            text: root.page.diagnostic
            font: root.style.font
            color: root.style.colors.muted
            wrapMode: Text.Wrap
        }
        RowLayout {
            SettingButton {
                objectName: "ApplyShortcut"
                style: root.style
                text: qsTranslate("WindowSystem", "Apply shortcut")
                enabled: root.page.canApply
                onClicked: root.page.apply()
            }
            SettingButton {
                objectName: "AddShortcut"
                style: root.style
                text: qsTranslate("WindowSystem", "Add binding")
                enabled: root.page.selected
                onClicked: { root.page.startAdding(); capture.forceActiveFocus() }
            }
            SettingButton {
                objectName: "ClearShortcut"
                style: root.style
                text: qsTranslate("WindowSystem", "Remove this binding")
                enabled: root.page.canRemove
                onClicked: root.page.remove()
            }
        }
    }
}
