// SPDX-License-Identifier: MIT
import QtQuick
import QtQuick.Controls.Basic as Basic

// The settings page, laid out as the Widgets one: a heading and note, the
// search field, the category tree beside a divider, and the settings under
// their category headings. Each setting shows its title, description,
// control and ID; the control follows the setting's kind.
Rectangle {
    id: root
    required property SettingsPageModel page
    readonly property SettingsStyle style: page.style
    readonly property real unit: style.unit
    readonly property var colors: style.colors
    color: colors.surface

    function categoryShown(id) {
        for (const category of page.categories) if (category.id === id) return category.shown
        return false
    }
    function scrollTo(id) {
        for (let index = 0; index < entries.count; ++index) {
            const item = entries.itemAt(index)
            if (item && item.entry.type === "heading" && item.entry.category === id && item.visible) {
                flick.contentY = Math.min(item.y, Math.max(0, flick.contentHeight - flick.height))
                page.current = id
                return
            }
        }
    }
    // The category of the last heading at the top of the view; at the end,
    // the last one shown.
    function syncCurrent() {
        let current = ""
        const atEnd = flick.contentY > 0 && flick.contentY >= flick.contentHeight - flick.height - 1
        for (let index = 0; index < entries.count; ++index) {
            const item = entries.itemAt(index)
            if (!item || item.entry.type !== "heading" || !item.visible) continue
            if (!current || atEnd || item.y <= flick.contentY + 4 * unit) current = item.entry.category
        }
        if (current) page.current = current
    }

    Column {
        id: header
        x: 24 * root.unit
        y: 16 * root.unit
        width: root.width - 48 * root.unit
        spacing: 12 * root.unit
        Text {
            text: root.page.pageTitle
            color: root.colors.accent
            font: root.style.titleFont
        }
        Text {
            width: parent.width
            text: root.page.pageDescription
            font: root.style.font
            color: root.colors.muted
            wrapMode: Text.Wrap
        }
        SettingTextField {
            id: search
            objectName: "SettingsSearch"
            width: parent.width
            style: root.style
            placeholderText: root.page.searchPlaceholder
            onTextChanged: { root.page.search(text); Qt.callLater(root.syncCurrent) }
        }
    }

    ListView {
        id: categories
        objectName: "SettingsCategories"
        x: header.x
        y: header.y + header.height + 12 * root.unit
        width: Math.max(90 * root.unit, Math.min(180 * root.unit, root.width / 4))
        height: root.height - y - 16 * root.unit
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.page.categories
        delegate: Item {
            id: category
            required property var modelData
            readonly property bool current: modelData.id === root.page.current
            width: categories.width
            height: modelData.shown ? root.style.controlHeight : 0
            visible: modelData.shown
            Rectangle {
                x: 2 * root.unit
                y: 4 * root.unit
                width: 2 * root.unit
                height: parent.height - 8 * root.unit
                color: root.colors.accent
                visible: category.current
            }
            Text {
                x: (12 + 16 * category.modelData.depth) * root.unit
                width: parent.width - x
                height: parent.height
                text: category.modelData.title
                font: category.current ? root.style.currentFont : root.style.font
                color: root.colors.text
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
            MouseArea { anchors.fill: parent; onClicked: root.scrollTo(category.modelData.id) }
        }
    }

    Rectangle {
        x: categories.x + categories.width + 12 * root.unit
        y: categories.y
        width: root.unit
        height: categories.height
        color: root.colors.border
    }

    Text {
        id: results
        objectName: "SettingsResultCount"
        x: categories.x + categories.width + 25 * root.unit
        y: categories.y
        text: root.page.results
        font: root.style.font
        color: root.colors.muted
    }

    Flickable {
        id: flick
        objectName: "SettingsRows"
        x: results.x
        y: results.y + results.height + 6 * root.unit
        width: root.width - x - 24 * root.unit
        height: root.height - y - 16 * root.unit
        clip: true
        contentHeight: rows.height
        boundsBehavior: Flickable.StopAtBounds
        onContentYChanged: root.syncCurrent()
        Basic.ScrollBar.vertical: Basic.ScrollBar {}

        Column {
            id: rows
            width: flick.width - 12 * root.unit
            spacing: 18 * root.unit
            Repeater {
                id: entries
                model: root.page.entries
                delegate: Item {
                    id: entry
                    required property var modelData
                    readonly property var entry: modelData
                    readonly property bool heading: modelData.type === "heading"
                    readonly property int setting: heading ? -1 : modelData.setting
                    objectName: heading ? "SettingsCategoryHeading/" + modelData.category : "SettingRow/" + modelData.id
                    visible: heading ? root.categoryShown(modelData.category) : root.page.shown[setting] === true
                    width: rows.width
                    height: visible ? (heading ? headingText.height : body.height + 12 * root.unit) : 0

                    Text {
                        id: headingText
                        visible: entry.heading
                        text: entry.modelData.title
                        color: root.colors.accent
                        font: root.style.headingFont
                    }

                    Column {
                        id: body
                        visible: !entry.heading
                        x: 8 * root.unit
                        y: 4 * root.unit
                        width: parent.width - 16 * root.unit
                        spacing: 6 * root.unit
                        Text {
                            text: entry.modelData.title
                            color: root.colors.accent
                            font: root.style.strongFont
                        }
                        Text {
                            width: parent.width
                            text: entry.modelData.description ?? ""
                            color: root.colors.muted
                            font: root.style.font
                            wrapMode: Text.Wrap
                        }
                        Loader {
                            id: control
                            readonly property var value: entry.heading ? undefined : root.page.values[entry.setting]
                            readonly property bool editable: !entry.heading && root.page.editable[entry.setting] === true
                            sourceComponent: entry.heading ? null
                                : entry.modelData.kind === "toggle" ? toggle
                                : entry.modelData.kind === "choice" ? choice
                                : entry.modelData.kind === "text" ? textField
                                : entry.modelData.kind === "slider" ? slider : number
                        }
                        TextEdit {
                            text: entry.modelData.id ?? ""
                            color: root.colors.muted
                            font: root.style.font
                            readOnly: true
                            selectByMouse: true
                            selectionColor: root.colors.selection
                            selectedTextColor: root.colors.selectionText
                        }
                    }

                    Component {
                        id: toggle
                        SettingCheckBox {
                            objectName: "SettingControl/" + entry.modelData.id
                            style: root.style
                            text: entry.modelData.checkLabel
                            enabled: control.editable
                            checked: control.value === true
                            onToggled: root.page.write(entry.setting, checked)
                        }
                    }
                    Component {
                        id: choice
                        SettingComboBox {
                            objectName: "SettingControl/" + entry.modelData.id
                            style: root.style
                            model: entry.modelData.choices
                            currentChoice: control.value
                            enabled: control.editable
                            implicitWidth: Math.min(440 * root.unit, Math.max(160 * root.unit, implicitContentWidth + leftPadding + rightPadding))
                            onActivated: (index) => root.page.write(entry.setting, entry.modelData.choices[index].value)
                        }
                    }
                    Component {
                        id: textField
                        SettingTextField {
                            objectName: "SettingControl/" + entry.modelData.id
                            style: root.style
                            width: Math.min(440 * root.unit, body.width)
                            enabled: control.editable
                            maximumLength: entry.modelData.maximumLength > 0 ? entry.modelData.maximumLength : 32767
                            placeholderText: entry.modelData.placeholder
                            // Typing keeps its text; a change made elsewhere shows once it leaves.
                            Binding on text { value: control.value ?? ""; when: !textFieldItem.activeFocus }
                            id: textFieldItem
                            onEditingFinished: root.page.write(entry.setting, text)
                        }
                    }
                    Component {
                        id: number
                        SettingSpinBox {
                            objectName: "SettingControl/" + entry.modelData.id
                            style: root.style
                            enabled: control.editable
                            minimum: entry.modelData.minimum
                            maximum: entry.modelData.maximum
                            step: entry.modelData.step
                            decimals: entry.modelData.decimals
                            suffix: entry.modelData.suffix
                            number: control.value ?? 0
                            onEdited: (number) => root.page.write(entry.setting, entry.modelData.kind === "integer" ? Math.round(number) : number)
                        }
                    }
                    Component {
                        id: slider
                        SettingSlider {
                            objectName: "SettingControl/" + entry.modelData.id
                            style: root.style
                            enabled: control.editable
                            sliderMaximum: entry.modelData.sliderMaximum
                            minimum: entry.modelData.minimum
                            maximum: entry.modelData.maximum
                            step: entry.modelData.step
                            suffix: entry.modelData.suffix
                            number: control.value ?? 0
                            onEdited: (number) => root.page.write(entry.setting, Math.round(number))
                        }
                    }
                }
            }
        }
    }
}
