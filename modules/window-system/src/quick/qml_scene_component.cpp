// SPDX-License-Identifier: MIT
#include "qml_scene_component.hpp"

#include <QCoreApplication>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <array>

void qml_register_types_WindowSystem_Internal();
static void initializeSceneResources()
{
    Q_INIT_RESOURCE(window_system_masks);
    Q_INIT_RESOURCE(window_system_mask_shaders);
    Q_INIT_RESOURCE(qmake_WindowSystem_Internal);
    qml_register_types_WindowSystem_Internal();
}

namespace window_system::quick {

QQmlComponent& sceneComponent(SceneComponent kind)
{
    static QPointer<QQmlEngine> engine;
    static std::array<QPointer<QQmlComponent>, 11> components;
    if (!engine) {
        initializeSceneResources();
        engine = new QQmlEngine(qApp);
    }
    const auto index = size_t(kind);
    Q_ASSERT(index < components.size());
    if (!components[index]) {
        components[index] = new QQmlComponent(engine, engine);
        if (kind == SceneComponent::Mask || kind == SceneComponent::PathStroke) {
            const auto name = kind == SceneComponent::Mask ? QStringLiteral("MaskedLayer.qml") : QStringLiteral("PathStroke.qml");
            components[index]->loadUrl(QUrl(QStringLiteral("qrc:/window-system/") + name));
        } else {
            static const std::array names{"PopupMenuNode", "PopupMenuRow", "PopupMenuSeparator", "ApplicationMenuBar", "HoverToolTip", "CommandPanelView", "SettingsPageView", "DialogView", "ShortcutsPageView"};
            components[index]->loadFromModule("WindowSystem.Internal", QLatin1String(names[index - size_t(SceneComponent::Menu)]));
        }
        if (components[index]->isError())
            qFatal("Cannot load Quick scene component: %s", qPrintable(components[index]->errorString()));
    }
    return *components[index];
}

} // namespace window_system::quick
