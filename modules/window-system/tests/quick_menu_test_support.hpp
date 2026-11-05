// SPDX-License-Identifier: MIT
#pragma once

#include <QGuiApplication>
#include <QQuickWindow>

namespace quick_menu_test {
inline QQuickWindow* popup(QQuickWindow* parent)
{
    for (auto* window : QGuiApplication::topLevelWindows())
        if (window->type() == Qt::Popup && window->isVisible() && window->transientParent() == parent)
            return qobject_cast<QQuickWindow*>(window);
    return nullptr;
}
inline QQuickItem* namedItem(QQuickItem* parent, const QString& name)
{
    if (parent->objectName() == name) return parent;
    for (auto* child : parent->childItems()) if (auto* found = namedItem(child, name)) return found;
    return nullptr;
}
} // namespace quick_menu_test
