import QtQuick

// The preview's placeholder pages in the Quick workspace. Texts, colors and
// fonts come from the host (quick_placeholder.cpp); this file lays them out.
Item {
    id: root
    required property string kind
    required property var texts
    required property var colors
    required property font bodyFont
    required property font titleFont
    required property font subtitleFont
    required property font monoFont
    required property real scale
    required property QtObject actions

    component Label: Text {
        color: root.colors.text
        font: root.bodyFont
        wrapMode: Text.Wrap
    }

    Loader {
        anchors.fill: parent
        sourceComponent: root.kind === "notes" ? notes
            : root.kind === "files" ? files
            : root.kind === "sessions" ? sessions
            : root.kind === "transfers" ? transfers : welcome
    }

    Component {
        id: notes
        Flickable {
            id: flick
            readonly property real margin: 6 * root.scale
            clip: true
            contentWidth: width
            contentHeight: edit.contentHeight + 2 * margin
            boundsBehavior: Flickable.StopAtBounds
            TextEdit {
                id: edit
                x: flick.margin
                y: flick.margin
                width: flick.width - 2 * flick.margin
                font: root.monoFont
                color: root.colors.text
                selectionColor: root.colors.selection
                selectedTextColor: root.colors.selectionText
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                persistentSelection: true
                Component.onCompleted: text = root.texts.notes
                onCursorRectangleChanged: {
                    const top = y + cursorRectangle.y, bottom = top + cursorRectangle.height
                    if (top < flick.contentY) flick.contentY = top
                    else if (bottom > flick.contentY + flick.height) flick.contentY = bottom - flick.height
                }
            }
        }
    }

    Component {
        id: welcome
        Item {
            Column {
                x: 30 * root.scale
                y: 28 * root.scale
                width: parent.width - 60 * root.scale
                spacing: 14 * root.scale
                Label { width: parent.width; text: "Muxora"; font: root.titleFont }
                Label { width: parent.width; text: root.texts.subtitle; font: root.subtitleFont }
                Label { width: parent.width; text: root.texts.description }
                Repeater {
                    model: root.texts.actions
                    delegate: Rectangle {
                        required property var modelData
                        width: Math.min(260 * root.scale, caption.implicitWidth + 24 * root.scale)
                        height: caption.implicitHeight + 12 * root.scale
                        radius: 4 * root.scale
                        color: hover.hovered ? root.colors.buttonHover : root.colors.raisedSurface
                        border.color: root.colors.border
                        Label {
                            id: caption
                            anchors.centerIn: parent
                            text: modelData.label
                            wrapMode: Text.NoWrap
                        }
                        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: root.actions.run(modelData.command) }
                    }
                }
            }
            Label {
                x: 30 * root.scale
                width: parent.width - 60 * root.scale
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 28 * root.scale
                text: root.texts.tip
                color: root.colors.mutedText
            }
        }
    }

    Component {
        id: files
        Column {
            id: table
            // The header row first; size and type keep a fixed width.
            readonly property real side: 90 * root.scale
            Repeater {
                model: [root.texts.columns].concat(root.texts.rows)
                delegate: Rectangle {
                    id: line
                    required property var modelData
                    required property int index
                    width: table.width
                    height: 26 * root.scale
                    color: index > 0 && index % 2 === 0 ? root.colors.raisedSurface : "transparent"
                    Row {
                        id: cells
                        anchors.fill: parent
                        anchors.leftMargin: 8 * root.scale
                        Repeater {
                            model: line.modelData
                            delegate: Label {
                                required property string modelData
                                required property int index
                                width: index === 0 ? cells.width - 2 * table.side : table.side
                                height: cells.height
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                                wrapMode: Text.NoWrap
                                text: modelData
                                color: line.index === 0 ? root.colors.mutedText : root.colors.text
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: sessions
        Column {
            padding: 8 * root.scale
            spacing: 6 * root.scale
            Label { text: root.texts.heading; color: root.colors.mutedText }
            Repeater {
                model: root.texts.names
                delegate: Label { required property string modelData; leftPadding: 16 * root.scale; text: modelData }
            }
        }
    }

    Component {
        id: transfers
        Item {
            Label {
                anchors.centerIn: parent
                width: parent.width - 32 * root.scale
                horizontalAlignment: Text.AlignHCenter
                text: root.texts.text
            }
        }
    }
}
