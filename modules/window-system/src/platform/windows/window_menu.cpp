// SPDX-License-Identifier: MIT
#include "window_menu.hpp"

#include <QCoreApplication>
#include <QPointer>
#include <QScopeGuard>
#include <QString>
#include <QWidget>

#include <qt_windows.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string>

namespace window_system {
namespace {

constexpr auto menuStoreName = "_windowSystemNativeMenu";
constexpr std::array<UINT, 6> commands{SC_RESTORE, SC_MOVE, SC_SIZE, SC_MINIMIZE, SC_MAXIMIZE, SC_CLOSE};

const std::array<std::wstring, 6>& systemCommandLabels()
{
    static const auto labels = [] {
        std::array<std::wstring, 6> result{
            QCoreApplication::translate("WindowSystem", "&Restore").toStdWString(),
            QCoreApplication::translate("WindowSystem", "&Move").toStdWString(),
            QCoreApplication::translate("WindowSystem", "&Size").toStdWString(),
            QCoreApplication::translate("WindowSystem", "Mi&nimize").toStdWString(),
            QCoreApplication::translate("WindowSystem", "Ma&ximize").toStdWString(),
            QCoreApplication::translate("WindowSystem", "&Close\tAlt+F4").toStdWString()};
        // Ask a hidden standard Win32 window for the system's localized labels.
        // This HWND is never shown and is destroyed before returning. Changing
        // the real window's styles to obtain a menu would disrupt Snap/DWM.
        const auto helper = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW,
            0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!helper) return result;
        const auto release = qScopeGuard([helper] { DestroyWindow(helper); });
        const auto source = GetSystemMenu(helper, FALSE);
        if (!source) return result;
        for (size_t index = 0; index < commands.size(); ++index) {
            MENUITEMINFOW item{};
            item.cbSize = sizeof(item);
            item.fMask = MIIM_STRING;
            if (!GetMenuItemInfoW(source, commands[index], FALSE, &item) || !item.cch || item.cch > 1024) continue;
            std::wstring text(item.cch + 1, L'\0');
            item.dwTypeData = text.data();
            item.cch = UINT(text.size());
            if (GetMenuItemInfoW(source, commands[index], FALSE, &item)) {
                text.resize(item.cch);
                result[index] = std::move(text);
            }
        }
        return result;
    }();
    return labels;
}

struct NativeMenu final {
    NativeMenu()
    {
        handle = CreatePopupMenu();
        if (!handle) return;
        const auto& labels = systemCommandLabels();
        for (size_t index = 0; index < commands.size(); ++index) {
            if (index == commands.size() - 1 && !AppendMenuW(handle, MF_SEPARATOR, 0, nullptr)) { reset(); return; }
            if (!AppendMenuW(handle, MF_STRING, commands[index], labels[index].c_str())) { reset(); return; }
        }
        SetMenuDefaultItem(handle, SC_CLOSE, FALSE);
    }
    ~NativeMenu() { reset(); }
    NativeMenu(const NativeMenu&) = delete;
    NativeMenu& operator=(const NativeMenu&) = delete;
    void reset() { if (handle) DestroyMenu(handle); handle = nullptr; }
    HMENU handle = nullptr;
};

class NativeMenuStore final : public QObject {
public:
    explicit NativeMenuStore(QWidget* window) : QObject(window), menu(std::make_shared<NativeMenu>())
    { setObjectName(QString::fromLatin1(menuStoreName)); }
    // TrackPopupMenu runs a nested native loop. The active call retains the
    // resource if the widget is deleted there; there is no ownership cycle.
    std::shared_ptr<NativeMenu> menu;
};

std::shared_ptr<NativeMenu> menuFor(QWidget* window)
{
    for (auto* child : window->children())
        if (child->objectName() == QString::fromLatin1(menuStoreName))
            if (auto* store = dynamic_cast<NativeMenuStore*>(child)) return store->menu;
    return (new NativeMenuStore(window))->menu;
}

void refreshCommands(QWidget* window, HMENU menu, HWND handle)
{
    const bool maximized = IsZoomed(handle), minimized = IsIconic(handle);
    const auto style = GetWindowLongPtrW(handle, GWL_STYLE);
    const bool resizable = window->minimumWidth() < window->maximumWidth()
        || window->minimumHeight() < window->maximumHeight();
    const auto enable = [menu](UINT command, bool allowed) {
        EnableMenuItem(menu, command, MF_BYCOMMAND | (allowed ? MF_ENABLED : MF_GRAYED));
    };
    enable(SC_RESTORE, maximized || minimized);
    enable(SC_MOVE, !maximized && !minimized);
    enable(SC_SIZE, !maximized && !minimized && (style & WS_THICKFRAME) && resizable);
    enable(SC_MINIMIZE, !minimized && (style & WS_MINIMIZEBOX));
    enable(SC_MAXIMIZE, !maximized && (style & WS_MAXIMIZEBOX));
}

} // namespace

quintptr nativeSystemMenuHandle(QWidget* window)
{
    if (!window || !window->isWindow() || !window->effectiveWinId()) return 0;
    const auto menu = menuFor(window);
    if (menu->handle) refreshCommands(window, menu->handle, reinterpret_cast<HWND>(window->effectiveWinId()));
    return quintptr(menu->handle);
}

void showNativeSystemMenu(QWidget* window, quintptr nativeWindow)
{
    const auto handle = reinterpret_cast<HWND>(nativeWindow);
    if (!window || !IsWindow(handle)) return;
    const QPointer<QWidget> retainedWindow(window);
    const auto menu = menuFor(window);
    if (!menu->handle) return;
    refreshCommands(window, menu->handle, handle);
    // The client is the visible window; the sizing border lies outside it.
    POINT origin{};
    if (!ClientToScreen(handle, &origin)) return;
    const int inset = std::max(1, qRound(8 * window->devicePixelRatioF()));
    const int titleHeight = std::max(1, qRound(32 * window->devicePixelRatioF()));
    const UINT command = TrackPopupMenuEx(menu->handle, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_RIGHTBUTTON,
        origin.x + inset, origin.y + titleHeight, handle, nullptr);
    if (!retainedWindow || retainedWindow->effectiveWinId() != WId(nativeWindow) || !IsWindow(handle)) return;
    if (command) PostMessageW(handle, WM_SYSCOMMAND, command, 0);
}

} // namespace window_system
