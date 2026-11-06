// SPDX-License-Identifier: MIT
#pragma once

#include <QAccessible>
#include <QString>

class QObject;
class QQuickItem;

namespace window_system::quick {

// Qt owns the attached metadata with the item. Roles, names and state use
// the same Accessible properties as the QML controls; no private Qt types.
[[nodiscard]] QObject* attachAccessibility(QQuickItem* item, QAccessible::Role role, const QString& name = {});

} // namespace window_system::quick
