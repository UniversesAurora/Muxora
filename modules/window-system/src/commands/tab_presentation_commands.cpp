// SPDX-License-Identifier: MIT
#include <QCoreApplication>
#include "tab_presentation_commands.hpp"
#include "command_menus.hpp"
#include "popup_menu.hpp"
#include <window_system/window_manager.hpp>
#include <QAction>
#include <QMenu>
#include <QPointer>
#include <QTimer>

namespace window_system {
namespace {
// Indexed by TabBarPlacement and TabRowExpansion, as in the settings file.
const QStringList placementNames{QStringLiteral("top"), QStringLiteral("bottom"), QStringLiteral("left"), QStringLiteral("right")};
const QStringList expansionNames{QStringLiteral("push"), QStringLiteral("overlay")};
bool vertical(TabBarPlacement placement) { return placement == TabBarPlacement::Left || placement == TabBarPlacement::Right; }
} // namespace

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
            const auto position = placementNames.indexOf(context.args.value("position").toString());
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
            const auto mode = expansionNames.indexOf(context.args.value("mode").toString());
            if (mode < 0) return false;
            auto next = retained->tabAppearance(); next.rowExpansion = TabRowExpansion(mode);
            return retained->setTabAppearance(next);
        }});
}

void appendTabPresentationActions(QMenu* menu, WindowManager* manager, const CommandContext& captured, bool autoHide)
{
    if (!menu || !manager) return;
    const auto* group = manager->model().group(captured.group);
    autoHide = autoHide || (group && group->location == GroupLocation::AutoHide);
    const bool pinned = group && group->location == GroupLocation::Pinned;
    const auto addFlag = [menu, owner = QPointer<WindowManager>(manager), captured](const char* command) {
        if (!owner) return;
        const auto id = QString::fromLatin1(command);
        auto* action = menu->addAction(QString()); action->setObjectName(id); action->setData(id);
        action->setProperty("windowSystemCommand", id);
        // Synchronous menus may otherwise execute the returned action a second
        // time. This action owns its dispatch, like the popup group menu.
        action->setProperty("windowSystemHandledAction", true);
        refreshCommandAction(owner, action, captured);
        QObject::connect(action, &QAction::triggered, menu, [owner, captured, id] {
            if (owner) QTimer::singleShot(0, owner, [owner, captured, id] {
                if (owner) owner->executeInContext(id, captured);
            });
        });
    };
    if (autoHide) {
        addFlag("presentation.toggleSidebarIcons"); addFlag("presentation.toggleSidebarNumbers");
        addFlag("presentation.toggleSidebarTitleClipping");
        return;
    }
    addFlag(pinned ? "presentation.toggleSidebarIcons" : "presentation.toggleIcons");
    addFlag("presentation.toggleNewTabButton");
    addFlag(pinned ? "presentation.toggleSidebarNumbers" : "presentation.toggleNumbers");
    addFlag("presentation.toggleTitleClipping");
    const auto id = pinned ? QStringLiteral("presentation.setPinnedPlacement") : QStringLiteral("presentation.setCentralPlacement");
    const auto appearance = manager->tabAppearance();
    const auto current = pinned ? appearance.pinnedPlacement : appearance.centralPlacement;
    // A choice submenu whose actions run `command` with one string argument.
    const auto addChoices = [menu, manager, captured](const QString& title, const char* name, const QString& command,
                                                      const QString& argument, const QStringList& values,
                                                      const QStringList& labels, int checked) {
        auto* choices = new DismissOnlyMenu(title, menu);
        choices->setObjectName(QString::fromLatin1(name));
        menu->addMenu(choices);
        for (int index = 0; index < values.size(); ++index) {
            const auto value = values.at(index);
            auto* action = choices->addAction(labels.at(index));
            action->setObjectName(command + "." + value); action->setData(command); action->setCheckable(true);
            action->setChecked(index == checked); action->setProperty("windowSystemHandledAction", true);
            QObject::connect(action, &QAction::triggered, menu, [owner = QPointer<WindowManager>(manager), captured, command, argument, value] {
                if (!owner) return;
                auto context = captured; context.args.insert(argument, value);
                QTimer::singleShot(0, owner, [owner, command, context] { if (owner) owner->executeInContext(command, context); });
            });
        }
    };
    menu->addSeparator();
    if (!vertical(current)) {
        addFlag("presentation.toggleMultiline");
        if (appearance.multiline) addFlag("presentation.toggleAllRows");
        if (appearance.stickyTabsOnSeparateRow) {
            addFlag("presentation.toggleStickyMultiline");
            if (appearance.stickyMultiline) addFlag("presentation.toggleStickyAllRows");
        }
        if (appearance.multiline || appearance.stickyMultiline) {
            addChoices(QCoreApplication::translate("WindowSystem", "Expanded tab rows"), "TabRowExpansionMenu",
                QStringLiteral("presentation.setRowExpansion"), QStringLiteral("mode"), expansionNames,
                {QCoreApplication::translate("WindowSystem", "Push the content"),
                 QCoreApplication::translate("WindowSystem", "Overlay the content")}, int(appearance.rowExpansion));
            addFlag("presentation.toggleWheelExpandRows");
        }
    }
    addChoices(QCoreApplication::translate("WindowSystem", "Tab bar position"), "TabBarPlacementMenu", id, QStringLiteral("position"),
        placementNames, {QCoreApplication::translate("WindowSystem", "Top"), QCoreApplication::translate("WindowSystem", "Bottom"),
                         QCoreApplication::translate("WindowSystem", "Left"), QCoreApplication::translate("WindowSystem", "Right")},
        int(current));
    menu->addSeparator();
}

} // namespace window_system
