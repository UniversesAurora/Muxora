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

void registerTabPresentationCommands(WindowManager* manager)
{
    const QPointer<WindowManager> retained(manager);
    const auto registerFlag = [manager, retained](const char* id, const QString& title, bool TabAppearance::*field) {
        manager->commands().add({QString::fromLatin1(id), title, QStringLiteral("视图"), {},
            [retained, field](const CommandContext&) {
                return retained && (field != &TabAppearance::wheelExpandRows || retained->tabAppearance().multiline);
            }, [retained, field](const CommandContext& context) {
                if (!retained) return false;
                auto next = retained->tabAppearance();
                if (context.args.contains("visible") && context.args.value("visible").metaType().id() != QMetaType::Bool) return false;
                next.*field = context.args.contains("visible") ? context.args.value("visible").toBool() : !(next.*field);
                if (!next.multiline) next.wheelExpandRows = false;
                return retained->setTabAppearance(next);
            }, {QStringLiteral("视图"), QStringLiteral("标签栏")}});
    };
    registerFlag("presentation.toggleIcons", QCoreApplication::translate("WindowSystem", "显示标签图标"), &TabAppearance::showIcons);
    registerFlag("presentation.toggleNewTabButton", QCoreApplication::translate("WindowSystem", "显示新建标签按钮"), &TabAppearance::showNewTabButton);
    registerFlag("presentation.toggleNumbers", QCoreApplication::translate("WindowSystem", "显示标签编号"), &TabAppearance::showNumbers);
    registerFlag("presentation.toggleSidebarIcons", QCoreApplication::translate("WindowSystem", "显示侧边栏标签图标"), &TabAppearance::showSidebarIcons);
    registerFlag("presentation.toggleSidebarNumbers", QCoreApplication::translate("WindowSystem", "显示侧边栏标签编号"), &TabAppearance::showSidebarNumbers);
    registerFlag("presentation.toggleTitleClipping", QCoreApplication::translate("WindowSystem", "Clip long regular and pinned tab titles"), &TabAppearance::clipTitles);
    registerFlag("presentation.toggleSidebarTitleClipping", QCoreApplication::translate("WindowSystem", "Clip long auto-hide sidebar tab titles"), &TabAppearance::clipSidebarTitles);
    registerFlag("presentation.toggleMultiline", QCoreApplication::translate("WindowSystem", "多行显示标签"), &TabAppearance::multiline);
    registerFlag("presentation.toggleWheelExpandRows", QCoreApplication::translate("WindowSystem", "滚轮展开标签行"), &TabAppearance::wheelExpandRows);
    for (const bool pinned : {false, true}) manager->commands().add({
        pinned ? QStringLiteral("presentation.setPinnedPlacement") : QStringLiteral("presentation.setCentralPlacement"),
        pinned ? QCoreApplication::translate("WindowSystem", "固定侧栏标签位置") : QCoreApplication::translate("WindowSystem", "普通标签栏位置"), QStringLiteral("视图"), {}, {},
        [retained, pinned](const CommandContext& context) {
            if (!retained || context.args.value("position").metaType().id() != QMetaType::QString) return false;
            const auto position = context.args.value("position").toString();
            if (position != "top" && position != "bottom") return false;
            auto next = retained->tabAppearance();
            (pinned ? next.pinnedPlacement : next.centralPlacement) = position == "top" ? TabBarPlacement::Top : TabBarPlacement::Bottom;
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
    menu->addSeparator(); addFlag("presentation.toggleMultiline"); addFlag("presentation.toggleWheelExpandRows");
    const auto id = pinned ? QStringLiteral("presentation.setPinnedPlacement") : QStringLiteral("presentation.setCentralPlacement");
    const auto appearance = manager->tabAppearance();
    const auto current = pinned ? appearance.pinnedPlacement : appearance.centralPlacement;
    auto* placements = new DismissOnlyMenu(QCoreApplication::translate("WindowSystem", "标签栏位置"), menu);
    placements->setObjectName(QStringLiteral("TabBarPlacementMenu"));
    menu->addMenu(placements);
    for (const auto placement : {TabBarPlacement::Top, TabBarPlacement::Bottom}) {
        const auto position = placement == TabBarPlacement::Top ? QStringLiteral("top") : QStringLiteral("bottom");
        auto* action = placements->addAction(placement == TabBarPlacement::Top ? QCoreApplication::translate("WindowSystem", "顶部") : QCoreApplication::translate("WindowSystem", "底部"));
        action->setObjectName(id + "." + position); action->setData(id); action->setCheckable(true);
        action->setChecked(current == placement); action->setProperty("windowSystemHandledAction", true);
        QObject::connect(action, &QAction::triggered, menu, [owner = QPointer<WindowManager>(manager), captured, id, position] {
            if (!owner) return;
            auto context = captured; context.args.insert("position", position);
            QTimer::singleShot(0, owner, [owner, id, context] { if (owner) owner->executeInContext(id, context); });
        });
    }
    menu->addSeparator();
}

} // namespace window_system
