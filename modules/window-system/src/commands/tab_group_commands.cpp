// SPDX-License-Identifier: MIT
#include "window_manager_p.hpp"
#include "commands/popup_menu.hpp"
#include "commands/command_menus.hpp"
#include <QCursor>
#include <QMenu>
#include <QPointer>

namespace window_system {
void WindowManager::registerTabGroupCommands()
{
    const auto add = [this](QString id, QString title, QStringList path,
        std::function<bool(const CommandContext&)> run, std::function<bool(const CommandContext&)> enabled = {}) {
        d->commands.add({std::move(id), std::move(title), QStringLiteral("布局"), {}, std::move(enabled), std::move(run), std::move(path)});
    };
    const auto central = [this](const CommandContext& c) { const auto* group = d->model.group(c.group); return group && group->location == GroupLocation::Central; };
    const auto sortable = [this](const CommandContext& c) {
        const auto* group = d->model.group(c.group);
        return group && group->location != GroupLocation::AutoHide;
    };
    const auto chooseColor = [this](const CommandContext& c) -> std::optional<QString> {
        if (c.args.contains("color")) return c.args.value("color").toString();
        DismissOnlyMenu menu; menu.setObjectName(QStringLiteral("TabColorPicker")); applyTheme(&menu, theme());
        auto* reset = menu.addAction(QStringLiteral("默认颜色")); reset->setData(QString());
        for (const auto& preset : tabAppearance().colors) { auto* action = menu.addAction(preset.name); action->setData(preset.id); }
        const QPointer<WindowManager> self(this);
        const auto* selected = menu.exec(QCursor::pos());
        return self && selected ? std::optional<QString>(selected->data().toString()) : std::nullopt;
    };
    add("tab.color.set", QStringLiteral("设置标签颜色…"), {"布局", "标签外观"}, [this, chooseColor](const CommandContext& c) {
        const QPointer<WindowManager> self(this); const auto color = chooseColor(c);
        return self && color && setTabColor(c.tab, *color);
    }, [this](const CommandContext& c) { return d->model.tab(c.tab); });
    const char* sortIds[] = {"alphabeticalAscending", "alphabeticalDescending", "openedAscending", "openedDescending", "visitedAscending", "visitedDescending"};
    const char* sortTitles[] = {"名称：升序", "名称：降序", "打开时间：最早优先", "打开时间：最近优先", "访问时间：最早优先", "访问时间：最近优先"};
    for (int i = 0; i < 6; ++i)
        add(QStringLiteral("group.sort.") + sortIds[i], QString::fromUtf8(sortTitles[i]), {"布局", "排列标签"},
            [this, i](const CommandContext& c) { return sortTabs(c.group, TabSortOrder(i)); }, sortable);
    add("group.close", QStringLiteral("关闭此标签组"), {"布局", "标签组"}, [this](const CommandContext& c) { return closeGroup(c.group); },
        [this](const CommandContext& c) { return d->model.group(c.group); });
    struct Direction { const char* id; const char* title; Placement direction; };
    for (const auto& direction : {Direction{"left", "向左移动标签组", Placement::Left}, Direction{"right", "向右移动标签组", Placement::Right},
        Direction{"up", "向上移动标签组", Placement::Top}, Direction{"down", "向下移动标签组", Placement::Bottom}})
        add(QStringLiteral("group.move.") + direction.id, QString::fromUtf8(direction.title), {"布局", "移动标签组"},
            [this, direction](const CommandContext& c) { return moveGroupToAdjacentGroup(c.group, direction.direction); }, central);
    for (bool forward : {false, true})
        add(forward ? "group.moveWindow.next" : "group.moveWindow.previous", forward ? QStringLiteral("标签组移到下一个窗口") : QStringLiteral("标签组移到上一个窗口"),
            {"布局", "移动标签组"}, [this, forward](const CommandContext& c) { return moveGroupToWindowCycle(c.group, forward); }, central);
    add("group.moveWindow", QStringLiteral("标签组移到指定窗口…"), {"布局", "移动标签组"}, [this](const CommandContext& c) {
        if (c.args.contains("targetWindow")) return moveGroupToWindow(c.group, c.args.value("targetWindow").toUuid());
        DismissOnlyMenu menu; applyTheme(&menu, theme());
        for (const auto id : d->model.windowOrder()) if (id != c.window) {
            auto* action = menu.addAction(windowWidget(id)->windowTitle()); action->setData(id);
        }
        const QPointer<WindowManager> self(this); const auto* action = menu.exec(QCursor::pos());
        return self && action && moveGroupToWindow(c.group, action->data().toUuid());
    }, central);
}
void WindowManager::showGroupMenu(GroupId group, QPoint position)
{
    const auto* record = d->model.group(group); if (!record) return;
    auto captured = context(record->windowId); captured.group = group; captured.tab = record->activeTab;
    captured.pane = d->model.tab(record->activeTab)->activePane;
    auto* menu = new DismissOnlyMenu(windowWidget(record->windowId)); menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setObjectName(QStringLiteral("TabBarContextMenu")); applyTheme(menu, theme());
    for (const auto command : {"presentation.toggleIcons", "presentation.toggleNewTabButton", "presentation.toggleNumbers"}) {
        const auto* definition = d->commands.find(command); if (!definition) continue;
        auto* action = menu->addAction(definition->title); action->setObjectName(QString::fromLatin1(command)); action->setCheckable(true);
        action->setChecked(command == QLatin1String("presentation.toggleIcons") ? tabAppearance().showIcons
            : command == QLatin1String("presentation.toggleNewTabButton") ? tabAppearance().showNewTabButton : tabAppearance().showNumbers);
        connect(action, &QAction::triggered, this, [this, command = QString::fromLatin1(command), captured] { executeInContext(command, captured); });
    }
    populateContextMenu(menu, CommandMenuSurface::Tab, this, captured); menu->popup(position);
}
} // namespace window_system
