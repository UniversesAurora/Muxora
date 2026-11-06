// SPDX-License-Identifier: MIT
#include "setting_description.hpp"
#include <window_system/window_manager.hpp>
#include <QCoreApplication>
#include <QPointer>
#include <type_traits>

namespace window_system {
namespace {

using Kind = SettingDescription::Kind;

template <class T>
SettingDescription tabSetting(QPointer<WindowManager> owner, const char* id,
    const QString& title, const QString& description, T TabAppearance::*field)
{
    constexpr auto kind = std::is_enum_v<T> ? Kind::Choice : (std::is_same_v<T, bool> ? Kind::Toggle : Kind::Integer);
    SettingDescription setting{QStringLiteral("window/tabs"), QString::fromLatin1(id), title, description, kind};
    setting.read = [owner, field]() -> QVariant {
        if (!owner) return {};
        if constexpr (std::is_enum_v<T>) return int(owner->tabAppearance().*field);
        else return owner->tabAppearance().*field;
    };
    setting.write = [owner, field](const QVariant& value) {
        if (!owner) return false;
        auto next = owner->tabAppearance();
        if constexpr (std::is_enum_v<T>) next.*field = T(value.toInt());
        else next.*field = value.value<T>();
        return owner->setTabAppearance(next);
    };
    if constexpr (std::is_enum_v<T>) setting.defaultValue = int(TabAppearance{}.*field);
    if constexpr (std::is_same_v<T, bool>) setting.checkLabel = title;
    return setting;
}

bool rowsAvailable(const TabAppearance& appearance)
{
    const auto horizontal = [](TabBarPlacement value) { return value == TabBarPlacement::Top || value == TabBarPlacement::Bottom; };
    return horizontal(appearance.centralPlacement) || horizontal(appearance.pinnedPlacement);
}

void appendPlacements(QVector<SettingDescription>& settings, QPointer<WindowManager> owner)
{
    auto central = tabSetting(owner, "tabs.centralPlacement", QCoreApplication::translate("WindowSystem", "Central tab bar position"),
        QCoreApplication::translate("WindowSystem", "Place central tab bars above, below or beside the content. Left and right show the tabs as a vertical list."), &TabAppearance::centralPlacement);
    auto pinned = tabSetting(owner, "tabs.pinnedPlacement", QCoreApplication::translate("WindowSystem", "Pinned sidebar tab position"),
        QCoreApplication::translate("WindowSystem", "Choose the tab position for pinned sidebar panels independently."), &TabAppearance::pinnedPlacement);
    central.choices = pinned.choices = {{QCoreApplication::translate("WindowSystem", "Top"), int(TabBarPlacement::Top)},
        {QCoreApplication::translate("WindowSystem", "Bottom"), int(TabBarPlacement::Bottom)},
        {QCoreApplication::translate("WindowSystem", "Left"), int(TabBarPlacement::Left)},
        {QCoreApplication::translate("WindowSystem", "Right"), int(TabBarPlacement::Right)}};
    settings.append(std::move(central));
    settings.append(std::move(pinned));
    auto width = tabSetting(owner, "tabs.verticalTabWidth", QCoreApplication::translate("WindowSystem", "Vertical tab bar width"),
        QCoreApplication::translate("WindowSystem", "The width of new vertical tab bars. Drag a vertical tab bar's edge to resize it on its own."), &TabAppearance::verticalTabWidth);
    width.minimum = minimumVerticalTabWidth;
    width.maximum = maximumVerticalTabWidth;
    width.suffix = QStringLiteral(" px");
    settings.append(std::move(width));
}

void appendRows(QVector<SettingDescription>& settings, QPointer<WindowManager> owner)
{
    auto multiline = tabSetting(owner, "tabs.multiline", QCoreApplication::translate("WindowSystem", "Show regular tabs in multiple rows"),
        QCoreApplication::translate("WindowSystem", "Wrap tabs to additional rows instead of using a horizontal scrollbar."), &TabAppearance::multiline);
    multiline.enabled = [owner] { return owner && rowsAvailable(owner->tabAppearance()); };
    settings.append(std::move(multiline));
    auto all = tabSetting(owner, "tabs.showAllRows", QCoreApplication::translate("WindowSystem", "Always show all regular tab rows"),
        QCoreApplication::translate("WindowSystem", "When off, show the active row until the shared tab panel is expanded."), &TabAppearance::showAllRows);
    all.enabled = [owner] { return owner && rowsAvailable(owner->tabAppearance()) && owner->tabAppearance().multiline; };
    settings.append(std::move(all));
    auto sticky = tabSetting(owner, "tabs.stickyMultiline", QCoreApplication::translate("WindowSystem", "Show sticky tabs in multiple rows"),
        QCoreApplication::translate("WindowSystem", "Use a separate row layout for sticky tabs when they have their own section."), &TabAppearance::stickyMultiline);
    sticky.enabled = [owner] { return owner && rowsAvailable(owner->tabAppearance()) && owner->tabAppearance().stickyTabsOnSeparateRow; };
    settings.append(std::move(sticky));
    auto stickyAll = tabSetting(owner, "tabs.stickyShowAllRows", QCoreApplication::translate("WindowSystem", "Always show all sticky tab rows"),
        QCoreApplication::translate("WindowSystem", "When off, sticky rows expand and collapse together with regular rows."), &TabAppearance::stickyShowAllRows);
    stickyAll.enabled = [owner] {
        if (!owner) return false;
        const auto& appearance = owner->tabAppearance();
        return rowsAvailable(appearance) && appearance.stickyTabsOnSeparateRow && appearance.stickyMultiline;
    };
    settings.append(std::move(stickyAll));
    auto expansion = tabSetting(owner, "tabs.rowExpansion", QCoreApplication::translate("WindowSystem", "Expanded tab rows"),
        QCoreApplication::translate("WindowSystem", "Sticky and regular tabs share one expanded panel and the same expansion behavior."), &TabAppearance::rowExpansion);
    expansion.choices = {{QCoreApplication::translate("WindowSystem", "Push the content"), int(TabRowExpansion::Push)},
        {QCoreApplication::translate("WindowSystem", "Overlay the content"), int(TabRowExpansion::Overlay)}};
    expansion.enabled = [owner] {
        if (!owner) return false;
        const auto& appearance = owner->tabAppearance();
        return rowsAvailable(appearance) && (appearance.multiline || (appearance.stickyTabsOnSeparateRow && appearance.stickyMultiline));
    };
    settings.append(std::move(expansion));
    auto wheel = tabSetting(owner, "tabs.wheelExpandRows", QCoreApplication::translate("WindowSystem", "Expand tab rows with mouse wheel"),
        QCoreApplication::translate("WindowSystem", "Expand toward the content and collapse away from it. Horizontal tab scrolling takes priority."), &TabAppearance::wheelExpandRows);
    wheel.enabled = [owner] {
        if (!owner) return false;
        const auto& appearance = owner->tabAppearance();
        return rowsAvailable(appearance) && ((appearance.multiline && !appearance.showAllRows)
            || (appearance.stickyTabsOnSeparateRow && appearance.stickyMultiline && !appearance.stickyShowAllRows));
    };
    settings.append(std::move(wheel));
}

void appendFlags(QVector<SettingDescription>& settings, QPointer<WindowManager> owner)
{
    settings.append(tabSetting(owner, "tabs.showIcons", QCoreApplication::translate("WindowSystem", "Show tab icons"),
        QCoreApplication::translate("WindowSystem", "Applies to central tab bars independently of sidebars. Per-tab icon visibility set by the host still applies."), &TabAppearance::showIcons));
    settings.append(tabSetting(owner, "tabs.showNewTabButton", QCoreApplication::translate("WindowSystem", "Show new tab button"),
        QCoreApplication::translate("WindowSystem", "Show the plus button and type menu after the last tab. When hidden, menus, commands, and shortcuts can still create tabs."), &TabAppearance::showNewTabButton));
    settings.append(tabSetting(owner, "tabs.showNumbers", QCoreApplication::translate("WindowSystem", "Show tab numbers"),
        QCoreApplication::translate("WindowSystem", "Show central tab numbers before the icon, starting at 1 in each tab bar."), &TabAppearance::showNumbers));
    settings.append(tabSetting(owner, "tabs.showGroupNames", QCoreApplication::translate("WindowSystem", "Show tab group names"),
        QCoreApplication::translate("WindowSystem", "Show a named group's name at the end of its tab bar."), &TabAppearance::showGroupNames));
    settings.append(tabSetting(owner, "tabs.showSidebarIcons", QCoreApplication::translate("WindowSystem", "Show sidebar tab icons"),
        QCoreApplication::translate("WindowSystem", "Show icons in pinned and auto-hide sidebar bars independently. Hidden by default; per-tab icon visibility still applies."), &TabAppearance::showSidebarIcons));
    settings.append(tabSetting(owner, "tabs.showSidebarNumbers", QCoreApplication::translate("WindowSystem", "Show sidebar tab numbers"),
        QCoreApplication::translate("WindowSystem", "Show numbers in pinned and auto-hide sidebar bars independently. Hidden by default. Number pinned bars separately and each auto-hide side continuously, starting at 1."), &TabAppearance::showSidebarNumbers));
}

SettingDescription titleWidth(QPointer<WindowManager> owner, const char* id, const QString& title,
    const QString& description, int TabAppearance::*field)
{
    auto setting = tabSetting(owner, id, title, description, field);
    setting.kind = Kind::IntegerSlider;
    setting.minimum = minimumTabTitleWidth;
    setting.maximum = maximumTabTitleWidth;
    setting.sliderMaximum = 1000;
    setting.suffix = QStringLiteral(" px");
    return setting;
}

void appendTitles(QVector<SettingDescription>& settings, QPointer<WindowManager> owner)
{
    settings.append(tabSetting(owner, "tabs.clipTitles", QCoreApplication::translate("WindowSystem", "Clip long regular and pinned tab titles"),
        QCoreApplication::translate("WindowSystem", "Clip titles that exceed the display limit without adding an ellipsis. Applies to regular tabs and pinned sidebar tab bars."), &TabAppearance::clipTitles));
    auto width = titleWidth(owner, "tabs.maximumTitleWidth", QCoreApplication::translate("WindowSystem", "Maximum regular and pinned tab title width"),
        QCoreApplication::translate("WindowSystem", "Clip title text that exceeds this length."), &TabAppearance::maximumTitleWidth);
    width.enabled = [owner] { return owner && owner->tabAppearance().clipTitles; };
    settings.append(std::move(width));
    settings.append(tabSetting(owner, "tabs.clipSidebarTitles", QCoreApplication::translate("WindowSystem", "Clip long auto-hide sidebar tab titles"),
        QCoreApplication::translate("WindowSystem", "Clip titles in auto-hide sidebar rails without adding an ellipsis. Regular and pinned tabs use their own settings."), &TabAppearance::clipSidebarTitles));
    auto sidebar = titleWidth(owner, "tabs.maximumSidebarTitleWidth", QCoreApplication::translate("WindowSystem", "Maximum auto-hide sidebar tab title length"),
        QCoreApplication::translate("WindowSystem", "Clip title text that exceeds this length."), &TabAppearance::maximumSidebarTitleWidth);
    sidebar.enabled = [owner] { return owner && owner->tabAppearance().clipSidebarTitles; };
    settings.append(std::move(sidebar));
}

QVector<SettingDescription> stickySettings(QPointer<WindowManager> owner)
{
    QVector<SettingDescription> settings;
    auto sizing = tabSetting(owner, "tabs.stickySizing", QCoreApplication::translate("WindowSystem", "Sticky tab size"),
        QCoreApplication::translate("WindowSystem", "Compact and shrunk sticky tabs stay at the start of a single-row tab bar while the other tabs scroll."), &TabAppearance::stickySizing);
    sizing.choices = {{QCoreApplication::translate("WindowSystem", "Normal"), int(StickyTabSizing::Normal)},
        {QCoreApplication::translate("WindowSystem", "Compact: icon or first letter only"), int(StickyTabSizing::Compact)},
        {QCoreApplication::translate("WindowSystem", "Shrink: the start of the title"), int(StickyTabSizing::Shrink)}};
    settings.append(std::move(sizing));
    auto width = titleWidth(owner, "tabs.shrinkStickyTitleWidth", QCoreApplication::translate("WindowSystem", "Shrunk sticky tab title width"),
        QCoreApplication::translate("WindowSystem", "Clip the title of a shrunk sticky tab that exceeds this length. Separate from the regular title clipping."), &TabAppearance::shrinkStickyTitleWidth);
    width.enabled = [owner] { return owner && owner->tabAppearance().stickySizing == StickyTabSizing::Shrink; };
    settings.append(std::move(width));
    settings.append(tabSetting(owner, "tabs.stickyTabsOnSeparateRow", QCoreApplication::translate("WindowSystem", "Show sticky tabs on a separate row"),
        QCoreApplication::translate("WindowSystem", "Sticky tabs take their own row above the other tabs and stay in place while those scroll. Drag a tab between the rows to stick or unstick it."), &TabAppearance::stickyTabsOnSeparateRow));
    auto close = tabSetting(owner, "tabs.preventStickyClose", QCoreApplication::translate("WindowSystem", "Keep sticky tabs open"),
        QCoreApplication::translate("WindowSystem", "A kept sticky tab stays open; the close shortcut then switches to the first tab that is not sticky. Menus and commands still close it."), &TabAppearance::preventStickyClose);
    close.choices = {{QCoreApplication::translate("WindowSystem", "On the close shortcut and middle click"), int(StickyTabCloseGuard::KeyboardAndMouse)},
        {QCoreApplication::translate("WindowSystem", "On the close shortcut"), int(StickyTabCloseGuard::Keyboard)},
        {QCoreApplication::translate("WindowSystem", "On middle click"), int(StickyTabCloseGuard::Mouse)},
        {QCoreApplication::translate("WindowSystem", "Never"), int(StickyTabCloseGuard::Never)}};
    settings.append(std::move(close));
    for (auto& setting : settings) setting.category = QStringLiteral("window/tabs/sticky");
    return settings;
}

} // namespace

QVector<SettingDescription> describeTabSettings(WindowManager* manager)
{
    const QPointer<WindowManager> owner(manager);
    QVector<SettingDescription> result;
    appendPlacements(result, owner);
    appendRows(result, owner);
    appendFlags(result, owner);
    appendTitles(result, owner);
    result += stickySettings(owner);
    return result;
}

} // namespace window_system
