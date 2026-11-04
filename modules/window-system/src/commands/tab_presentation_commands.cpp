// SPDX-License-Identifier: MIT
#include <QCoreApplication>
#include "tab_presentation_commands.hpp"
#include "command_menus.hpp"
#include "menu_description.hpp"
#include "style/tab_appearance_p.hpp"
#include <window_system/window_manager.hpp>
#include <QMenu>
#include <QPointer>

namespace window_system {
void registerTabPresentationCommands(WindowManager* manager)
{
    const QPointer<WindowManager> retained(manager);
    const auto registerFlag = [manager, retained](const char* id, const QString& title, bool TabAppearance::*field) {
        manager->commands().add({QString::fromLatin1(id), title, QStringLiteral("view"), {},
            [retained, field](const CommandContext&) {
                if (!retained) return false;
                const auto appearance = retained->tabAppearance();
                if (field == &TabAppearance::showAllRows) return appearance.multiline;
                if (field == &TabAppearance::stickyMultiline) return appearance.stickyTabsOnSeparateRow;
                if (field == &TabAppearance::stickyShowAllRows) return appearance.stickyTabsOnSeparateRow && appearance.stickyMultiline;
                if (field == &TabAppearance::wheelExpandRows) return (appearance.multiline && !appearance.showAllRows)
                    || (appearance.stickyTabsOnSeparateRow && appearance.stickyMultiline && !appearance.stickyShowAllRows);
                return true;
            }, [retained, field](const CommandContext& context) {
                if (!retained) return false;
                auto next = retained->tabAppearance();
                if (context.args.contains("visible") && context.args.value("visible").metaType().id() != QMetaType::Bool) return false;
                // Wheel expansion only applies to several rows but keeps its
                // value while they are off, so turning them on restores it.
                next.*field = context.args.contains("visible") ? context.args.value("visible").toBool() : !(next.*field);
                return retained->setTabAppearance(next);
            }, {QStringLiteral("view"), QStringLiteral("tabBar")}});
    };
    registerFlag("presentation.toggleIcons", QCoreApplication::translate("WindowSystem", "Show tab icons"), &TabAppearance::showIcons);
    registerFlag("presentation.toggleNewTabButton", QCoreApplication::translate("WindowSystem", "Show new tab button"), &TabAppearance::showNewTabButton);
    registerFlag("presentation.toggleNumbers", QCoreApplication::translate("WindowSystem", "Show tab numbers"), &TabAppearance::showNumbers);
    registerFlag("presentation.toggleSidebarIcons", QCoreApplication::translate("WindowSystem", "Show sidebar tab icons"), &TabAppearance::showSidebarIcons);
    registerFlag("presentation.toggleSidebarNumbers", QCoreApplication::translate("WindowSystem", "Show sidebar tab numbers"), &TabAppearance::showSidebarNumbers);
    registerFlag("presentation.toggleTitleClipping", QCoreApplication::translate("WindowSystem", "Clip long regular and pinned tab titles"), &TabAppearance::clipTitles);
    registerFlag("presentation.toggleSidebarTitleClipping", QCoreApplication::translate("WindowSystem", "Clip long auto-hide sidebar tab titles"), &TabAppearance::clipSidebarTitles);
    registerFlag("presentation.toggleMultiline", QCoreApplication::translate("WindowSystem", "Show regular tabs in multiple rows"), &TabAppearance::multiline);
    registerFlag("presentation.toggleAllRows", QCoreApplication::translate("WindowSystem", "Always show all regular tab rows"), &TabAppearance::showAllRows);
    registerFlag("presentation.toggleStickyMultiline", QCoreApplication::translate("WindowSystem", "Show sticky tabs in multiple rows"), &TabAppearance::stickyMultiline);
    registerFlag("presentation.toggleStickyAllRows", QCoreApplication::translate("WindowSystem", "Always show all sticky tab rows"), &TabAppearance::stickyShowAllRows);
    registerFlag("presentation.toggleWheelExpandRows", QCoreApplication::translate("WindowSystem", "Expand tab rows with mouse wheel"), &TabAppearance::wheelExpandRows);
    for (const bool pinned : {false, true}) manager->commands().add({
        pinned ? QStringLiteral("presentation.setPinnedPlacement") : QStringLiteral("presentation.setCentralPlacement"),
        pinned ? QCoreApplication::translate("WindowSystem", "Pinned sidebar tab position") : QCoreApplication::translate("WindowSystem", "Central tab bar position"), QStringLiteral("view"), {}, {},
        [retained, pinned](const CommandContext& context) {
            if (!retained || context.args.value("position").metaType().id() != QMetaType::QString) return false;
            const auto position = tabBarPlacementNames().indexOf(context.args.value("position").toString());
            if (position < 0) return false;
            auto next = retained->tabAppearance();
            (pinned ? next.pinnedPlacement : next.centralPlacement) = TabBarPlacement(position);
            return retained->setTabAppearance(next);
        }});
    manager->commands().add({QStringLiteral("presentation.setRowExpansion"), QCoreApplication::translate("WindowSystem", "Expanded tab rows"),
        QStringLiteral("view"), {}, [retained](const CommandContext&) {
            if (!retained) return false;
            const auto appearance = retained->tabAppearance();
            const auto horizontal = [](TabBarPlacement place) { return place == TabBarPlacement::Top || place == TabBarPlacement::Bottom; };
            return (horizontal(appearance.centralPlacement) || horizontal(appearance.pinnedPlacement))
                && (appearance.multiline || (appearance.stickyTabsOnSeparateRow && appearance.stickyMultiline));
        },
        [retained](const CommandContext& context) {
            if (!retained || context.args.value("mode").metaType().id() != QMetaType::QString) return false;
            const auto mode = tabRowExpansionNames().indexOf(context.args.value("mode").toString());
            if (mode < 0) return false;
            auto next = retained->tabAppearance(); next.rowExpansion = TabRowExpansion(mode);
            return retained->setTabAppearance(next);
        }});
}

void appendTabPresentationActions(QMenu* menu, WindowManager* manager, const CommandContext& captured, bool autoHide)
{
    if (!menu || !manager) return;
    const QPointer<QMenu> retainedMenu(menu);
    const QPointer<WindowManager> owner(manager);
    const auto description = describeTabPresentationMenu(manager, captured, autoHide);
    if (retainedMenu && owner) appendDescribedMenuItems(menu, description, manager, captured, nullptr, true);
}

} // namespace window_system
