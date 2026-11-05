// SPDX-License-Identifier: MIT
#include "window_drag_area_item.hpp"

#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QStyleHints>
#include <utility>

namespace window_system::quick {

WindowDragAreaItem::WindowDragAreaItem(QQuickItem* parent) : QQuickItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
}

void WindowDragAreaItem::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !window()) return QQuickItem::mousePressEvent(event);
    event->accept();
    if (carriesTabs && carriesTabs()) press_ = event->globalPosition().toPoint();
    else window()->startSystemMove();
}

bool WindowDragAreaItem::event(QEvent* event)
{
    if (event->type() == QEvent::ContextMenu) if (const auto callback = menuRequested) {
        const auto* menu = static_cast<QContextMenuEvent*>(event);
        const auto global = menu->reason() == QContextMenuEvent::Keyboard
            ? mapToGlobal(boundingRect().center()).toPoint() : menu->globalPos();
        event->accept(); callback(global); return true;
    }
    return QQuickItem::event(event);
}

void WindowDragAreaItem::mouseMoveEvent(QMouseEvent* event)
{
    if (!press_) return QQuickItem::mouseMoveEvent(event);
    event->accept();
    if ((event->globalPosition().toPoint() - *press_).manhattanLength() < QGuiApplication::styleHints()->startDragDistance()) return;
    const auto press = *std::exchange(press_, std::nullopt);
    if (carryRequested) carryRequested(press);
}

void WindowDragAreaItem::mouseReleaseEvent(QMouseEvent* event)
{
    press_.reset();
    QQuickItem::mouseReleaseEvent(event);
}

void WindowDragAreaItem::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return QQuickItem::mouseDoubleClickEvent(event);
    event->accept();
    if (maximizeRequested) maximizeRequested();
}

} // namespace window_system::quick
