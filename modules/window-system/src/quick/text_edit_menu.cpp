// SPDX-License-Identifier: MIT
#include "text_edit_menu.hpp"
#include <QCoreApplication>
#include <QKeySequence>
#include <QQuickItem>

namespace window_system::quick {

QQuickItem* textEditorFor(QQuickItem* source)
{
    for (auto* item = source; item; item = item->parentItem()) {
        const auto* meta = item->metaObject();
        if (meta->indexOfProperty("selectedText") >= 0 && meta->indexOfProperty("readOnly") >= 0
            && meta->indexOfMethod("selectAll()") >= 0 && meta->indexOfMethod("copy()") >= 0) return item;
    }
    return nullptr;
}

QVector<MenuItemDescription> describeTextEditMenu(QQuickItem* editor)
{
    if (!editor) return {};
    QVector<MenuItemDescription> result;
    const bool editable = !editor->property("readOnly").toBool();
    const bool selected = !editor->property("selectedText").toString().isEmpty();
    const auto echo = editor->property("echoMode");
    const bool copyable = selected && (!echo.isValid() || echo.toInt() == 0);
    const auto add = [&](const char* method, const char* title, QKeySequence::StandardKey key, bool enabled) {
        MenuItemDescription item;
        item.kind = MenuItemDescription::Kind::TextEdit;
        item.commandId = QString::fromLatin1(method);
        item.id = QStringLiteral("edit.") + item.commandId;
        item.action.title = QCoreApplication::translate("QWidgetTextControl", title);
        if (key != QKeySequence::UnknownKey) item.action.shortcut = QKeySequence(key).toString(QKeySequence::NativeText);
        item.action.enabled = enabled;
        result.append(std::move(item));
    };
    const auto separate = [&] {
        MenuItemDescription item; item.kind = MenuItemDescription::Kind::Separator;
        result.append(std::move(item));
    };
    if (editable) {
        add("undo", QT_TRANSLATE_NOOP("QWidgetTextControl", "&Undo"), QKeySequence::Undo, editor->property("canUndo").toBool());
        add("redo", QT_TRANSLATE_NOOP("QWidgetTextControl", "&Redo"), QKeySequence::Redo, editor->property("canRedo").toBool());
        separate();
        add("cut", QT_TRANSLATE_NOOP("QWidgetTextControl", "Cu&t"), QKeySequence::Cut, copyable);
    }
    add("copy", QT_TRANSLATE_NOOP("QWidgetTextControl", "&Copy"), QKeySequence::Copy, copyable);
    if (editable) {
        add("paste", QT_TRANSLATE_NOOP("QWidgetTextControl", "&Paste"), QKeySequence::Paste, editor->property("canPaste").toBool());
        add("delete", QT_TRANSLATE_NOOP("QWidgetTextControl", "Delete"), QKeySequence::UnknownKey, selected);
    }
    separate();
    add("selectAll", QT_TRANSLATE_NOOP("QWidgetTextControl", "Select All"), QKeySequence::SelectAll, !editor->property("text").toString().isEmpty());
    return result;
}

void executeTextEditAction(QQuickItem* editor, const QString& action)
{
    // Recheck capabilities after popup dismissal; clipboard, selection or
    // read-only state may have changed while the menu held native capture.
    const auto items = describeTextEditMenu(editor);
    for (const auto& item : items) if (item.kind == MenuItemDescription::Kind::TextEdit
        && item.commandId == action && item.action.enabled) {
        if (action == QStringLiteral("delete")) {
            QMetaObject::invokeMethod(editor, "remove", Q_ARG(int, editor->property("selectionStart").toInt()),
                Q_ARG(int, editor->property("selectionEnd").toInt()));
        } else QMetaObject::invokeMethod(editor, action.toLatin1().constData());
        return;
    }
}

} // namespace window_system::quick
