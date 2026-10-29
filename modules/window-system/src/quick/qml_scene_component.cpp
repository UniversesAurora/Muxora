// SPDX-License-Identifier: MIT
#include "qml_scene_component.hpp"

#include <QCoreApplication>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <array>

static void initializeSceneResources()
{
    Q_INIT_RESOURCE(window_system_masks);
    Q_INIT_RESOURCE(window_system_mask_shaders);
}

namespace window_system::quick {

QQmlComponent& sceneComponent(SceneComponent kind)
{
    static QPointer<QQmlEngine> engine;
    static std::array<QPointer<QQmlComponent>, 2> components;
    if (!engine) {
        initializeSceneResources();
        engine = new QQmlEngine(qApp);
    }
    const auto index = size_t(kind);
    Q_ASSERT(index < components.size());
    if (!components[index]) {
        const auto name = kind == SceneComponent::Mask ? QStringLiteral("MaskedLayer.qml")
                                                       : QStringLiteral("PathStroke.qml");
        components[index] = new QQmlComponent(engine,
            QUrl(QStringLiteral("qrc:/window-system/") + name), engine);
        if (components[index]->isError())
            qFatal("Cannot load Quick scene component: %s", qPrintable(components[index]->errorString()));
    }
    return *components[index];
}

} // namespace window_system::quick
