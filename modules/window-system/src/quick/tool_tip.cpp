// SPDX-License-Identifier: MIT
#include "tool_tip.hpp"
#include "qml_scene_component.hpp"
#include <window_system/ui_scale.hpp>
#include <QCursor>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTimer>

namespace window_system::quick {
namespace {

// QStyle's tooltip delays (SH_ToolTip_WakeUpDelay, SH_ToolTip_FallAsleepDelay).
constexpr int wakeUpDelay = 700;
constexpr int fallAsleepDelay = 2000;

struct Tip {
    QPointer<QQuickItem> anchor;
    QPointer<QObject> popup;
    QString text;
    QRectF area;
    Theme theme;
    QTimer wake;
    QElapsedTimer asleep;
};

Tip& tip()
{
    static auto* state = [] {
        auto* created = new Tip;
        created->wake.setSingleShot(true);
        QObject::connect(&created->wake, &QTimer::timeout, [] {
            auto& current = tip();
            if (!current.anchor || current.text.isEmpty()) return;
            const auto pointer = current.anchor->mapFromGlobal(QPointF(QCursor::pos()));
            if (!current.anchor->contains(pointer) || (!current.area.isEmpty() && !current.area.contains(pointer))) return;
            // Below and right of the pointer, as QToolTip places it.
            const auto at = pointer + QPointF(2, 16);
            const auto& theme = current.theme;
            auto* popup = sceneComponent(SceneComponent::ToolTip).createWithInitialProperties({
                {"parent", QVariant::fromValue(current.anchor.data())}, {"text", current.text},
                {"font", QGuiApplication::font()}, {"x", at.x()}, {"y", at.y()},
                {"fill", theme.surface}, {"stroke", theme.border}, {"ink", theme.text},
                {"cornerRadius", uiPixelsF(std::max(0, theme.metrics.cornerRadius))}, {"unit", uiPixelsF(1)}});
            Q_ASSERT(popup);
            QQmlEngine::setObjectOwnership(popup, QQmlEngine::CppOwnership);
            popup->setParent(current.anchor);
            current.popup = popup;
            QMetaObject::invokeMethod(popup, "open");
        });
        return created;
    }();
    return *state;
}

void close(Tip& current)
{
    current.wake.stop();
    if (!current.popup) return;
    delete current.popup.data();
    current.asleep.start();
}

} // namespace

void showToolTip(QQuickItem* anchor, const Theme& theme, const QString& text, QRectF area)
{
    auto& current = tip();
    if (text.isEmpty()) { hideToolTip(anchor); return; }
    if (current.anchor == anchor && current.text == text && current.area == area && (current.popup || current.wake.isActive())) return;
    const bool awake = current.popup || (current.asleep.isValid() && current.asleep.elapsed() < fallAsleepDelay);
    close(current);
    current.anchor = anchor; current.text = text; current.area = area; current.theme = theme;
    current.wake.start(awake ? 0 : wakeUpDelay);
}

void hideToolTip(QQuickItem* anchor)
{
    auto& current = tip();
    if (current.anchor != anchor) return;
    close(current);
    current.anchor.clear();
}

} // namespace window_system::quick
