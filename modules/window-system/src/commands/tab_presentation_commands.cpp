// SPDX-License-Identifier: MIT
#include "tab_presentation_commands.hpp"
#include <window_system/window_manager.hpp>
#include <QPointer>

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
    registerFlag("presentation.toggleIcons", QStringLiteral("显示标签图标"), &TabAppearance::showIcons);
    registerFlag("presentation.toggleNewTabButton", QStringLiteral("显示新建标签按钮"), &TabAppearance::showNewTabButton);
    registerFlag("presentation.toggleNumbers", QStringLiteral("显示标签编号"), &TabAppearance::showNumbers);
    registerFlag("presentation.toggleMultiline", QStringLiteral("多行显示标签"), &TabAppearance::multiline);
    registerFlag("presentation.toggleWheelExpandRows", QStringLiteral("滚轮展开标签行"), &TabAppearance::wheelExpandRows);
    for (const bool pinned : {false, true}) manager->commands().add({
        pinned ? QStringLiteral("presentation.setPinnedPlacement") : QStringLiteral("presentation.setCentralPlacement"),
        pinned ? QStringLiteral("固定侧栏标签位置") : QStringLiteral("普通标签栏位置"), QStringLiteral("视图"), {}, {},
        [retained, pinned](const CommandContext& context) {
            if (!retained || context.args.value("position").metaType().id() != QMetaType::QString) return false;
            const auto position = context.args.value("position").toString();
            if (position != "top" && position != "bottom") return false;
            auto next = retained->tabAppearance();
            (pinned ? next.pinnedPlacement : next.centralPlacement) = position == "top" ? TabBarPlacement::Top : TabBarPlacement::Bottom;
            return retained->setTabAppearance(next);
        }});
}

} // namespace window_system
