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
    // Never create the handle here: winId() on a popup whose parent is an
    // ordinary child widget (a menu bar's menu, a combo box's list) forces
    // native windows on that parent's children and, from there, up and across
    // the whole window, which then breaks painting, hover and the native hit
    // test. Callers apply this again on Show, which Qt sends after it creates
    // the handle and before the native window appears.
    if (!popup->internalWinId()) return;
    const auto handle = reinterpret_cast<HWND>(popup->internalWinId());
    // QSS supplies the single client border. A native edge or Win11's DWM
    // outline would otherwise add a second rim (often white under a different
    // system palette). Keep the compositor's shadow while removing only rims.
    const auto style = GetWindowLongPtrW(handle, GWL_STYLE);
    const auto squareStyle = style & ~(WS_BORDER | WS_DLGFRAME | WS_THICKFRAME);
    const auto extended = GetWindowLongPtrW(handle, GWL_EXSTYLE);
    const auto squareExtended = extended & ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE);
    if (style != squareStyle || extended != squareExtended) {
        SetWindowLongPtrW(handle, GWL_STYLE, squareStyle);
        SetWindowLongPtrW(handle, GWL_EXSTYLE, squareExtended);
        SetWindowPos(handle, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    constexpr DWORD cornerPreferenceAttribute = 33;
    const DWORD squareCorners = 1; // DWMWCP_DONOTROUND
    DwmSetWindowAttribute(handle, cornerPreferenceAttribute, &squareCorners, sizeof(squareCorners));
    constexpr DWORD borderColorAttribute = 34;
    const COLORREF noBorder = 0xfffffffe; // DWMWA_COLOR_NONE
    DwmSetWindowAttribute(handle, borderColorAttribute, &noBorder, sizeof(noBorder));
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
    // A menu bar's menu can also be opened from another menu: a compressed
    // menu strip lists its clipped menus in its overflow menu. Place it by the
    // menu that actually opened it, like any submenu, not under the bar.
    QMenu* opener = nullptr;
    for (auto* object : menuAction()->associatedObjects()) {
        auto* candidate = qobject_cast<QMenu*>(object);
        if (candidate && candidate != this && candidate->isVisible() && candidate->activeAction() == menuAction()) {
            opener = candidate; break;
        }
    }
    auto* bar = opener ? nullptr : qobject_cast<QMenuBar*>(parentWidget());
    if (bar) {
        QRect item = bar->actionGeometry(menuAction());
        // Keyboard navigation can reach an item clipped by a compressed
        // strip: open it at the visible end of the bar, not out of sight.
        const QRect visible = bar->visibleRegion().boundingRect();
        if (!visible.isEmpty() && !visible.intersects(item))
            item.moveLeft(std::max(visible.left(), visible.right() + 1 - item.width()));
        const auto anchor = QRect(bar->mapToGlobal(item.topLeft()), item.size());
        if (const auto* screen = anchorScreen(anchor, this)) move(popupMenuPosition(anchor, size(), screen->availableGeometry()));
    } else if (auto* parent = opener ? opener : qobject_cast<QMenu*>(parentWidget())) {
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
