// SPDX-License-Identifier: MIT
#pragma once

#include "commands/menu_description.hpp"

class QQuickItem;

namespace window_system::quick {

// Qt's TextEdit/TextInput public QML properties and methods, including Controls
// subclasses. No dependency on their private C++ implementation types.
[[nodiscard]] QQuickItem* textEditorFor(QQuickItem* source);
[[nodiscard]] QVector<MenuItemDescription> describeTextEditMenu(QQuickItem* editor);
void executeTextEditAction(QQuickItem* editor, const QString& action);

} // namespace window_system::quick
