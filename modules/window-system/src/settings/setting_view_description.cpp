// SPDX-License-Identifier: MIT
#include "setting_description.hpp"
#include <window_system/window_manager.hpp>
#include <QCoreApplication>
#include <QPointer>
#include <type_traits>

namespace window_system {
namespace {

template <class T>
SettingDescription behaviorSetting(QPointer<WindowManager> owner, const char* id,
    const QString& title, const QString& description, SettingDescription::Kind kind, T WindowBehavior::*field)
{
    SettingDescription setting{QStringLiteral("window/display"), QString::fromLatin1(id), title, description, kind};
    setting.read = [owner, field]() -> QVariant {
        if (!owner) return {};
        if constexpr (std::is_enum_v<T>) return int(owner->behavior().*field);
        else return owner->behavior().*field;
    };
    setting.write = [owner, field](const QVariant& value) {
        if (!owner) return false;
        auto next = owner->behavior();
        if constexpr (std::is_enum_v<T>) next.*field = T(value.toInt());
        else next.*field = value.value<T>();
        owner->setBehavior(next);
        return true;
    };
    if (kind == SettingDescription::Kind::Toggle) setting.checkLabel = title;
    return setting;
}

} // namespace

QVector<SettingDescription> describeViewSettings(WindowManager* manager, const std::function<WindowId()>& currentWindow)
{
    const QPointer<WindowManager> owner(manager);
    using Kind = SettingDescription::Kind;
    QVector<SettingDescription> result;
    result.append(behaviorSetting(owner, "window.hideSingleTabWorkbench",
        QCoreApplication::translate("WindowSystem", "Single-tab bar in workbench windows"),
        QCoreApplication::translate("WindowSystem", "Count tabs across the whole window. Pinned panels and auto-hide sidebars remain visible."),
        Kind::Toggle, &WindowBehavior::hideSingleTabWorkbench));
    result.last().checkLabel = QCoreApplication::translate("WindowSystem", "Hide the tab bar when a workbench window has one tab");
    result.append(behaviorSetting(owner, "window.hideSingleTabFloating",
        QCoreApplication::translate("WindowSystem", "Single-tab bar in floating windows"),
        QCoreApplication::translate("WindowSystem", "Enabled by default. The tab bar returns when a second tab is added."),
        Kind::Toggle, &WindowBehavior::hideSingleTabFloating));
    result.last().checkLabel = QCoreApplication::translate("WindowSystem", "Hide the tab bar when a floating window has one tab");
    result.append(behaviorSetting(owner, "window.hideToolbarOptionsWhenLocked",
        QCoreApplication::translate("WindowSystem", "Compact locked toolbars"),
        QCoreApplication::translate("WindowSystem", "Hide the options button when toolbars are locked and all buttons are visible. Keep it visible when buttons overflow."),
        Kind::Toggle, &WindowBehavior::hideToolbarOptionsWhenLocked));
    result.last().checkLabel = QCoreApplication::translate("WindowSystem", "Hide toolbar options when locked with no overflow");

    auto order = behaviorSetting(owner, "window.captionButtonOrder",
        QCoreApplication::translate("WindowSystem", "Window control button order"),
        QCoreApplication::translate("WindowSystem", "The last two align the buttons to the left with Close outermost, for a window controls toolbar in the top-left corner."),
        Kind::Choice, &WindowBehavior::captionButtonOrder);
    order.defaultValue = int(WindowBehavior{}.captionButtonOrder);
    order.choices = {{QCoreApplication::translate("WindowSystem", "Windows: Minimize, Maximize, Close (right)"), int(CaptionButtonOrder::Windows)},
        {QCoreApplication::translate("WindowSystem", "Reversed: Close, Maximize, Minimize (left)"), int(CaptionButtonOrder::Reversed)},
        {QCoreApplication::translate("WindowSystem", "macOS: Close, Minimize, Maximize (left)"), int(CaptionButtonOrder::MacOS)}};
    result.append(std::move(order));
    auto zen = behaviorSetting(owner, "window.zenFullscreenMode",
        QCoreApplication::translate("WindowSystem", "Default fullscreen scope for Zen mode"),
        QCoreApplication::translate("WindowSystem", "Used when entering Zen mode from a normal window. Keep the current scope if already fullscreen. Exiting Zen mode restores the bars and keeps fullscreen."),
        Kind::Choice, &WindowBehavior::zenFullscreenMode);
    zen.defaultValue = int(WindowBehavior{}.zenFullscreenMode);
    zen.choices = {{QCoreApplication::translate("WindowSystem", "Window fullscreen"), int(ViewMode::WindowFullscreen)},
        {QCoreApplication::translate("WindowSystem", "Tab group fullscreen"), int(ViewMode::GroupFullscreen)},
        {QCoreApplication::translate("WindowSystem", "Pane fullscreen"), int(ViewMode::PaneFullscreen)}};
    result.append(std::move(zen));

    struct Bar {
        const char* key;
        const char* name;
        bool BarVisibility::*field;
        bool (WindowManager::*write)(WindowId, bool);
    };
    const Bar bars[] = {
        {"toolbars", QT_TRANSLATE_NOOP("WindowSystem", "Toolbars"), &BarVisibility::toolbars, &WindowManager::setToolbarsVisible},
        {"statusBar", QT_TRANSLATE_NOOP("WindowSystem", "Status bar"), &BarVisibility::statusBar, &WindowManager::setStatusBarVisible},
        {"tabBars", QT_TRANSLATE_NOOP("WindowSystem", "Tab bar"), &BarVisibility::tabBars, &WindowManager::setTabBarsVisible},
        {"sideBars", QT_TRANSLATE_NOOP("WindowSystem", "Auto-hide sidebars"), &BarVisibility::sideBars, &WindowManager::setSideBarsVisible}};
    for (const auto& bar : bars) {
        const auto title = QCoreApplication::translate("WindowSystem", bar.name);
        SettingDescription setting{QStringLiteral("window/display"), QStringLiteral("window.visibility.") + QString::fromLatin1(bar.key), title,
            QCoreApplication::translate("WindowSystem", "Bar visibility is independent of fullscreen, applies to the current window, and is saved with the layout. Zen mode temporarily hides every bar."), Kind::Toggle};
        setting.checkLabel = QCoreApplication::translate("WindowSystem", "Show %1").arg(title);
        setting.read = [owner, currentWindow, bar] { return owner && owner->barVisibility(currentWindow()).*bar.field; };
        setting.write = [owner, currentWindow, bar](const QVariant& value) { return owner && (owner.data()->*bar.write)(currentWindow(), value.toBool()); };
        setting.enabled = [owner, currentWindow] { return owner && owner->model().window(currentWindow()); };
        result.append(std::move(setting));
    }
    return result;
}

} // namespace window_system
