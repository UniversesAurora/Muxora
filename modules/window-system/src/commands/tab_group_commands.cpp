// SPDX-License-Identifier: MIT
#include <QCoreApplication>
#include "window_manager_p.hpp"
#include "commands/popup_menu.hpp"
#include "commands/command_menus.hpp"
#include "commands/tab_presentation_commands.hpp"
#include "style/tab_appearance_p.hpp"
#include <QCursor>
#include <QMenu>
#include <QPointer>
#include <cmath>
#include <limits>

namespace window_system {
namespace {
std::optional<int> integerArgument(const QVariant& value, int minimum, int maximum)
{
    switch (value.metaType().id()) {
    case QMetaType::Int: case QMetaType::UInt: case QMetaType::LongLong: case QMetaType::ULongLong:
    case QMetaType::Long: case QMetaType::ULong: case QMetaType::Short: case QMetaType::UShort:
    case QMetaType::Float: case QMetaType::Double: break;
    default: return std::nullopt;
    }
    bool converted = false;
    const auto number = value.toDouble(&converted);
    if (!converted || !std::isfinite(number) || std::trunc(number) != number || number < minimum || number > maximum)
        return std::nullopt;
    return int(number);
}
}
void WindowManager::registerTabGroupCommands()
{
    const auto add = [this](QString id, QString title, QStringList path,
        std::function<bool(const CommandContext&)> run, std::function<bool(const CommandContext&)> enabled = {}) {
        d->commands.add({std::move(id), std::move(title), QStringLiteral("布局"), {}, std::move(enabled), std::move(run), std::move(path)});
    };
    const auto visible = [this](const CommandContext& c) {
        const auto* group = d->model.group(c.group); return group && group->location != GroupLocation::AutoHide;
    };
    const auto hasTarget = [this, visible](const CommandContext& c) {
        if (!visible(c)) return false;
        for (const auto& group : d->model.groups())
            if (group.id != c.group && group.location != GroupLocation::AutoHide) return true;
        return false;
    };
    const auto hasWindow = [this, visible](const CommandContext& c) {
        return visible(c) && d->model.windowOrder().size() > 1;
    };
    const auto sortable = [this](const CommandContext& c) {
        const auto* group = d->model.group(c.group);
        return group && group->location != GroupLocation::AutoHide;
    };
    const auto chooseColor = [this](const CommandContext& c) -> std::optional<QString> {
        if (c.args.contains("color")) return c.args.value("color").toString();
        DismissOnlyMenu menu; menu.setObjectName(QStringLiteral("TabColorPicker")); applyTheme(&menu, theme());
        auto* reset = menu.addAction(QCoreApplication::translate("WindowSystem", "默认颜色")); reset->setData(QString());
        for (const auto& preset : tabAppearance().colors) { auto* action = menu.addAction(tabColorDisplayName(preset)); action->setData(preset.id); }
        const QPointer<WindowManager> self(this);
        const auto* selected = menu.exec(QCursor::pos());
        return self && selected ? std::optional<QString>(selected->data().toString()) : std::nullopt;
    };
    add("tab.color.set", QCoreApplication::translate("WindowSystem", "设置标签颜色…"), {"布局", "标签外观"}, [this, chooseColor](const CommandContext& c) {
        const QPointer<WindowManager> self(this); const auto color = chooseColor(c);
        return self && color && setTabColor(c.tab, *color);
    }, [this](const CommandContext& c) { return d->model.tab(c.tab); });
    const char* sortIds[] = {"alphabeticalAscending", "alphabeticalDescending", "openedAscending", "openedDescending", "visitedAscending", "visitedDescending"};
    const char* sortTitles[] = {QT_TRANSLATE_NOOP("WindowSystem", "名称：升序"), QT_TRANSLATE_NOOP("WindowSystem", "名称：降序"),
        QT_TRANSLATE_NOOP("WindowSystem", "打开时间：最早优先"), QT_TRANSLATE_NOOP("WindowSystem", "打开时间：最近优先"),
        QT_TRANSLATE_NOOP("WindowSystem", "访问时间：最早优先"), QT_TRANSLATE_NOOP("WindowSystem", "访问时间：最近优先")};
    for (int i = 0; i < 6; ++i)
        add(QStringLiteral("group.sort.") + sortIds[i], QCoreApplication::translate("WindowSystem", sortTitles[i]), {"布局", "排列标签"},
            [this, i](const CommandContext& c) { return sortTabs(c.group, TabSortOrder(i)); }, sortable);
    add("group.close", QCoreApplication::translate("WindowSystem", "关闭此标签组"), {"布局", "标签组"}, [this](const CommandContext& c) { return closeGroup(c.group); },
        [this](const CommandContext& c) { return d->model.group(c.group); });
    // The picker captures stable IDs, never widget pointers. Closing the menu
    // or deleting its source during its nested event loop changes no layout.
    const auto chooseTarget = [this](const CommandContext& c, bool choosePlacement) -> std::optional<QPair<GroupId, Placement>> {
        Placement placement = choosePlacement ? Placement::Right : Placement::Center;
        if (choosePlacement && c.args.contains("placement")) {
            const auto parsed = integerArgument(c.args.value("placement"), int(Placement::Left), int(Placement::Bottom));
            if (!parsed) return std::nullopt;
            placement = Placement(*parsed);
        }
        if (c.args.contains("targetGroup")) {
            const auto target = c.args.value("targetGroup").toUuid();
            return QPair{target, placement};
        }
        DismissOnlyMenu menu; menu.setObjectName(QStringLiteral("GroupTargetPicker")); applyTheme(&menu, theme());
        for (const auto windowId : d->model.windowOrder()) {
            const auto* window = d->model.window(windowId);
            if (!window) continue;
            auto order = d->model.centralGroups(windowId); order += window->edgeGroups;
            const auto* widget = windowWidget(windowId);
            auto* windowMenu = new DismissOnlyMenu(widget ? widget->windowTitle() : QCoreApplication::translate("WindowSystem", "窗口"), &menu);
            menu.addMenu(windowMenu);
            for (const auto id : order) {
                const auto* group = d->model.group(id);
                if (!group || id == c.group || group->location == GroupLocation::AutoHide) continue;
                const auto* active = d->model.tab(group->activeTab);
                const QString label = (active ? active->title : QCoreApplication::translate("WindowSystem", "标签组"))
                    + QCoreApplication::translate("WindowSystem", "（%1 个标签）").arg(group->tabs.size());
                if (!choosePlacement) {
                    auto* action = windowMenu->addAction(label); action->setData(id);
                    continue;
                }
                auto* positions = new DismissOnlyMenu(label, windowMenu); windowMenu->addMenu(positions);
                const QStringList names{QCoreApplication::translate("WindowSystem", "左侧"), QCoreApplication::translate("WindowSystem", "右侧"), QCoreApplication::translate("WindowSystem", "上方"), QCoreApplication::translate("WindowSystem", "下方")};
                for (int i = 0; i < 4; ++i) {
                    auto* action = positions->addAction(names[i]); action->setData(id);
                    action->setProperty("groupPlacement", i + int(Placement::Left));
                }
            }
            if (windowMenu->isEmpty()) menu.removeAction(windowMenu->menuAction());
        }
        const QPointer<WindowManager> self(this);
        const auto* selected = menu.exec(QCursor::pos());
        if (!self || !selected) return std::nullopt;
        return QPair{selected->data().toUuid(), choosePlacement
            ? Placement(selected->property("groupPlacement").toInt()) : Placement::Center};
    };
    struct Direction { const char* id; const char* title; Placement direction; };
    for (const auto& direction : {Direction{"left", QT_TRANSLATE_NOOP("WindowSystem", "左"), Placement::Left}, Direction{"right", QT_TRANSLATE_NOOP("WindowSystem", "右"), Placement::Right},
        Direction{"up", QT_TRANSLATE_NOOP("WindowSystem", "上"), Placement::Top}, Direction{"down", QT_TRANSLATE_NOOP("WindowSystem", "下"), Placement::Bottom}}) {
        add(QStringLiteral("group.swap.") + direction.id, QCoreApplication::translate("WindowSystem", "向%1交换标签组").arg(QCoreApplication::translate("WindowSystem", direction.title)), {"布局", "交换标签组"},
            [this, direction](const CommandContext& c) { return swapGroupDirection(c.group, direction.direction); },
            [this, direction](const CommandContext& c) { return !adjacentVisibleGroup(c.group, direction.direction).isNull(); });
        add(QStringLiteral("group.merge.") + direction.id, QCoreApplication::translate("WindowSystem", "合并到%1方标签组").arg(QCoreApplication::translate("WindowSystem", direction.title)), {"布局", "合并标签组"},
            [this, direction](const CommandContext& c) { return mergeGroupDirection(c.group, direction.direction); },
            [this, direction](const CommandContext& c) { return !adjacentVisibleGroup(c.group, direction.direction).isNull(); });
        add(QStringLiteral("group.move.") + direction.id, QCoreApplication::translate("WindowSystem", "将当前标签组移到%1侧相邻组的外侧").arg(QCoreApplication::translate("WindowSystem", direction.title)), {"布局", "移动标签组"},
            [this, direction](const CommandContext& c) { return moveGroupToAdjacentGroup(c.group, direction.direction); },
            [this, direction](const CommandContext& c) { return !adjacentVisibleGroup(c.group, direction.direction, true).isNull(); });
        const Edge edge = direction.direction == Placement::Left ? Edge::Left : direction.direction == Placement::Right ? Edge::Right
            : direction.direction == Placement::Top ? Edge::Top : Edge::Bottom;
        add(QStringLiteral("group.moveEdge.") + direction.id, QCoreApplication::translate("WindowSystem", "标签组固定到%1侧边栏").arg(QCoreApplication::translate("WindowSystem", direction.title)), {"布局", "移动标签组"},
            [this, edge](const CommandContext& c) { return moveGroupToEdge(c.group, c.window, edge, true); }, visible);
        add(QStringLiteral("group.hideEdge.") + direction.id, QCoreApplication::translate("WindowSystem", "标签组收纳到%1侧边栏").arg(QCoreApplication::translate("WindowSystem", direction.title)), {"布局", "移动标签组"},
            [this, edge](const CommandContext& c) { return moveGroupToEdge(c.group, c.window, edge, false); }, visible);
    }
    add("group.swap", QCoreApplication::translate("WindowSystem", "与指定标签组交换…"), {"布局", "交换标签组"}, [this, chooseTarget](const CommandContext& c) {
        const QPointer<WindowManager> self(this); const auto target = chooseTarget(c, false);
        return self && target && swapGroups(c.group, target->first);
    }, hasTarget);
    add("group.merge", QCoreApplication::translate("WindowSystem", "合并到指定标签组…"), {"布局", "合并标签组"}, [this, chooseTarget](const CommandContext& c) {
        int insertionIndex = -1;
        if (c.args.contains("insertionIndex")) {
            const auto parsed = integerArgument(c.args.value("insertionIndex"), -1, std::numeric_limits<int>::max());
            if (!parsed) return false;
            insertionIndex = *parsed;
        }
        const QPointer<WindowManager> self(this); const auto target = chooseTarget(c, false);
        return self && target && mergeGroups(c.group, target->first, insertionIndex);
    }, hasTarget);
    add("group.moveToGroup", QCoreApplication::translate("WindowSystem", "移到指定标签组旁…"), {"布局", "移动标签组"}, [this, chooseTarget](const CommandContext& c) {
        const QPointer<WindowManager> self(this); const auto target = chooseTarget(c, true);
        return self && target && moveGroup(c.group, target->first, target->second);
    }, hasTarget);
    for (bool forward : {false, true})
        add(forward ? "group.moveWindow.next" : "group.moveWindow.previous", forward ? QCoreApplication::translate("WindowSystem", "标签组移到下一个窗口") : QCoreApplication::translate("WindowSystem", "标签组移到上一个窗口"),
            {"布局", "移动标签组"}, [this, forward](const CommandContext& c) { return moveGroupToWindowCycle(c.group, forward); }, hasWindow);
    add("group.moveWindow", QCoreApplication::translate("WindowSystem", "标签组移到指定窗口…"), {"布局", "移动标签组"}, [this](const CommandContext& c) {
        if (c.args.contains("targetWindow")) return moveGroupToWindow(c.group, c.args.value("targetWindow").toUuid());
        DismissOnlyMenu menu; applyTheme(&menu, theme());
        for (const auto id : d->model.windowOrder()) if (id != c.window) {
            auto* action = menu.addAction(windowWidget(id)->windowTitle()); action->setData(id);
        }
        const QPointer<WindowManager> self(this); const auto* action = menu.exec(QCursor::pos());
        return self && action && moveGroupToWindow(c.group, action->data().toUuid());
    }, hasWindow);
    for (const bool floating : {true, false})
        add(floating ? "group.detachFloating" : "group.detachWorkbench", floating ? QCoreApplication::translate("WindowSystem", "标签组移到新轻量浮窗") : QCoreApplication::translate("WindowSystem", "标签组移到新完整窗口"),
            {"布局", "移动标签组"}, [this, floating](const CommandContext& c) {
                return !detachGroup(c.group, floating ? WindowKind::Floating : WindowKind::Workbench).isNull();
            }, visible);
}
void WindowManager::showGroupMenu(GroupId group, QPoint position)
{
    const auto* record = d->model.group(group); if (!record) return;
    auto captured = context(record->windowId); captured.group = group; captured.tab = record->activeTab;
    const auto* active = d->model.tab(record->activeTab);
    captured.pane = active ? active->activePane : PaneId{};
    auto* menu = new DismissOnlyMenu(windowWidget(record->windowId)); menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setObjectName(QStringLiteral("TabBarContextMenu")); applyTheme(menu, theme());
    appendTabPresentationActions(menu, this, captured);
    populateContextMenu(menu, CommandMenuSurface::Group, this, captured); menu->popup(position);
}
} // namespace window_system
