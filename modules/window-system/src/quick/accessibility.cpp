// SPDX-License-Identifier: MIT
#include "accessibility.hpp"
#include "qml_scene_component.hpp"
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>

namespace window_system::quick {

QObject* attachAccessibility(QQuickItem* item, QAccessible::Role role, const QString& name)
{
    static QPointer<QObject> adapter;
    if (!adapter) {
        auto& component = sceneComponent(SceneComponent::Accessibility);
        adapter = component.create();
        Q_ASSERT(adapter);
        QQmlEngine::setObjectOwnership(adapter, QQmlEngine::CppOwnership);
        adapter->setParent(component.engine());
    }
    QObject* metadata = nullptr;
    [[maybe_unused]] const bool attached = QMetaObject::invokeMethod(adapter, "attach", Q_RETURN_ARG(QObject*, metadata), Q_ARG(QQuickItem*, item));
    Q_ASSERT(attached && metadata);
    metadata->setProperty("role", int(role));
    metadata->setProperty("name", name);
    return metadata;
}

} // namespace window_system::quick
