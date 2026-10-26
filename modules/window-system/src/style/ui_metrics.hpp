// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/interface_appearance.hpp>
#include <QLayout>
#include <QPointer>
#include <QWidget>
#include <functional>

namespace window_system {

// Authored sizes retain their unscaled values. Reapply only when the user
// changes zoom, rather than accumulating rounding errors on every resize.
inline void bindUiMetrics(QObject* owner, std::function<void()> apply)
{
    apply();
    if (auto* controller = InterfaceAppearanceController::applicationInstance())
        QObject::connect(controller, &InterfaceAppearanceController::appearanceChanged, owner, std::move(apply));
}
inline QSize uiSize(QSize size) { return {uiPixels(size.width()), uiPixels(size.height())}; }
inline void uiInterfaceFont(QWidget* widget)
{
    // Some native widget classes choose a platform font when first created,
    // after the application font was already installed at startup.
    bindUiMetrics(widget, [widget] {
        if (auto* controller = InterfaceAppearanceController::applicationInstance())
            widget->setFont(controller->font());
    });
}
template<class T> inline void uiMargins(T* layout, int left, int top, int right, int bottom)
{
    bindUiMetrics(layout, [=] { layout->setContentsMargins(uiPixels(left), uiPixels(top), uiPixels(right), uiPixels(bottom)); });
}
inline void uiSpacing(QLayout* layout, int value)
{
    bindUiMetrics(layout, [=] { layout->setSpacing(value < 0 ? value : uiPixels(value)); });
}
inline void uiMinimumWidth(QWidget* widget, int value)
{
    bindUiMetrics(widget, [=] { widget->setMinimumWidth(uiPixels(value)); });
}
inline void uiMinimumHeight(QWidget* widget, int value)
{
    bindUiMetrics(widget, [=] { widget->setMinimumHeight(uiPixels(value)); });
}
inline void uiMaximumWidth(QWidget* widget, int value)
{
    bindUiMetrics(widget, [=] { widget->setMaximumWidth(uiPixels(value)); });
}
inline void uiFixedWidth(QWidget* widget, int value)
{
    bindUiMetrics(widget, [=] { widget->setFixedWidth(uiPixels(value)); });
}
inline void uiFixedHeight(QWidget* widget, int value)
{
    bindUiMetrics(widget, [=] { widget->setFixedHeight(uiPixels(value)); });
}
inline void uiMinimumSize(QWidget* widget, int width, int height)
{
    bindUiMetrics(widget, [=] { widget->setMinimumSize(uiPixels(width), uiPixels(height)); });
}
inline void uiFontDelta(QWidget* widget, double pixels)
{
    bindUiMetrics(widget, [=] {
        auto font = widget->font();
        const auto parentFont = widget->parentWidget() ? widget->parentWidget()->font() : font;
        // Point sizes retain fractional logical pixels at fractional zoom.
        font.setFamilies(parentFont.families());
        font.setPointSizeF(parentFont.pointSizeF() + uiPixelsF(pixels) * 72.0 / 96.0);
        widget->setFont(font);
    });
}
} // namespace window_system
