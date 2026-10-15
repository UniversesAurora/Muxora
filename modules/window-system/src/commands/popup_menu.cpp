// SPDX-License-Identifier: MIT
#include "popup_menu.hpp"

#include <QApplication>
#include <QMenuBar>
#include <QScreen>
#include <QShowEvent>
#include <QVariant>
#include <QWindow>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dwmapi.h>
#endif

#include <algorithm>

namespace window_system {
namespace {
constexpr auto anchorProperty = "windowSystemPopupAnchor";

QScreen* anchorScreen(const QRect& anchor, const QWidget* menu)
{
    if (auto* screen = QGuiApplication::screenAt(anchor.center())) return screen;
    return menu->screen();
}
}

QPoint popupMenuPosition(const QRect& anchor, QSize size, const QRect& available)
{
    if (!anchor.isValid() || !available.isValid()) return anchor.bottomLeft() + QPoint(0, 1);
    int x = anchor.left();
    int y = anchor.bottom() + 1;
    if (x + size.width() > available.right() + 1) x = anchor.right() + 1 - size.width();
    if (y + size.height() > available.bottom() + 1) y = anchor.top() - size.height();
    x = std::clamp(x, available.left(), std::max(available.left(), available.right() + 1 - size.width()));
    y = std::clamp(y, available.top(), std::max(available.top(), available.bottom() + 1 - size.height()));
    return {x, y};
}

void setPopupAnchor(QMenu* menu, const QRect& anchor)
{
    if (menu) menu->setProperty(anchorProperty, anchor);
}

DismissOnlyMenu::DismissOnlyMenu(QWidget* parent) : QMenu(parent)
{
    connect(this, &QMenu::aboutToShow, this, [this] { configureNativePopup(); });
}

DismissOnlyMenu::DismissOnlyMenu(const QString& title, QWidget* parent) : DismissOnlyMenu(parent)
{
    setTitle(title);
}

void preparePopupFrame(QWidget* popup)
{
    if (!popup || !popup->isWindow()) return;
    popup->clearMask();
#ifdef Q_OS_WIN
    // A square stylesheet changes client painting, but Win11 can still round
    // the native popup HWND. Apply the policy before first exposure and when
    // Qt replaces a handle. Earlier Windows keeps its ordinary square popup.
    const auto handle = reinterpret_cast<HWND>(popup->winId());
    constexpr DWORD cornerPreferenceAttribute = 33;
    const DWORD squareCorners = 1; // DWMWCP_DONOTROUND
    DwmSetWindowAttribute(handle, cornerPreferenceAttribute, &squareCorners, sizeof(squareCorners));
#endif
}

void DismissOnlyMenu::configureNativePopup()
{
    preparePopupFrame(this);
}

void DismissOnlyMenu::showEvent(QShowEvent* event)
{
    QMenu::showEvent(event);
    // Platform menu styles may provide SH_Menu_Mask despite a square QSS
    // border. Keep the client shape rectangular as well as its DWM corners.
    clearMask();
    configureNativePopup();
    const auto explicitAnchor = property(anchorProperty);
    if (explicitAnchor.canConvert<QRect>()) {
        const auto anchor = explicitAnchor.toRect();
        if (const auto* screen = anchorScreen(anchor, this)) move(popupMenuPosition(anchor, size(), screen->availableGeometry()));
        return;
    }
    if (auto* bar = qobject_cast<QMenuBar*>(parentWidget())) {
        const auto anchor = QRect(bar->mapToGlobal(bar->actionGeometry(menuAction()).topLeft()),
                                  bar->actionGeometry(menuAction()).size());
        if (const auto* screen = anchorScreen(anchor, this)) move(popupMenuPosition(anchor, size(), screen->availableGeometry()));
    } else if (auto* parent = qobject_cast<QMenu*>(parentWidget())) {
        const auto item = parent->actionGeometry(menuAction());
        const auto anchor = QRect(parent->mapToGlobal(item.topLeft()), item.size());
        const auto* screen = anchorScreen(anchor, this);
        if (!screen) return;
        const auto available = screen->availableGeometry();
        int x = parent->frameGeometry().right() + 1;
        if (x + width() > available.right() + 1) x = parent->frameGeometry().left() - width();
        int y = anchor.top();
        if (y + height() > available.bottom() + 1) y = anchor.bottom() + 1 - height();
        x = std::clamp(x, available.left(), std::max(available.left(), available.right() + 1 - width()));
        y = std::clamp(y, available.top(), std::max(available.top(), available.bottom() + 1 - height()));
        move(x, y);
    }
}

} // namespace window_system
