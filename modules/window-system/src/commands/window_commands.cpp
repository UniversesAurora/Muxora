#include "window_manager_p.hpp"
#include <window_system/window_manager.hpp>
#include "chrome/workspace_window.hpp"
#include "chrome/command_strip.hpp"
#include "commands/command_dialogs.hpp"
#include "docking/dock_surface.hpp"
#include "docking/drag_controller.hpp"
#include "panes/tab_content.hpp"
#include <QApplication>
#include <QEvent>
#include <QCursor>
#include <QGuiApplication>
#include <QJsonArray>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QScreen>
#include <QSet>
#include <QStyle>
#include <QTimer>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QLineEdit>
#include <algorithm>
#include <limits>

namespace window_system {
void WindowManager::registerWindowCommands()
{
    auto add = [this](QString id, QString title, QString category, QKeySequence shortcut,
                      std::function<bool(const CommandContext&)> handler,
                      std::function<bool(const CommandContext&)> enabled = {}) {
        d->commands.add({std::move(id), std::move(title), std::move(category), std::move(shortcut), std::move(enabled), std::move(handler)});
    };
    auto hasTab = [this](const CommandContext& c) { return d->model.tab(c.tab) != nullptr; };
    auto hasWindow = [this](const CommandContext& c) { return d->model.window(c.window) != nullptr; };
    add("workbench.showCommandPalette", QStringLiteral("显示命令面板"), "视图", QKeySequence("Ctrl+Shift+P"),
        [this](const CommandContext& c) { showCommandPalette(c.window); return true; });
    add("window.select", QStringLiteral("选择窗口…"), "窗口", QKeySequence("Ctrl+Alt+W"),
        [this](const CommandContext& c) { showWindowSelector(c.window); return true; });
    add("window.settings", QStringLiteral("窗口行为设置…"), "工具", QKeySequence("Ctrl+,"),
        [this](const CommandContext& c) { showSettings(c.window); return true; });
    add("preferences.openKeyboardShortcuts", QStringLiteral("编辑快捷键…"), "工具", QKeySequence("Ctrl+K, Ctrl+S"),
        [this](const CommandContext& c) { openShortcutEditor(this, windowWidget(c.window)); return true; });
    add("workspace.closeTab", QStringLiteral("关闭当前标签"), "文件", QKeySequence("Ctrl+W"),
        [this](const CommandContext& c) { return closeTab(c.tab); }, hasTab);
    add("window.close", QStringLiteral("关闭当前窗口"), "窗口", QKeySequence("Alt+F4"),
        [this](const CommandContext& c) { return requestCloseWindow(c.window); }, hasWindow);
    add("window.new", QStringLiteral("新建完整工作窗口"), "文件", QKeySequence("Ctrl+Shift+N"),
        [this](const CommandContext& c) { auto options = d->options.value(c.window); const auto id = createWindow(WindowKind::Workbench, options); showWindow(id); return !id.isNull(); });
    add("workspace.detachFloating", QStringLiteral("当前标签移到轻量浮窗"), "窗口", QKeySequence("Ctrl+Shift+D"),
        [this](const CommandContext& c) { return !detachTab(c.tab, WindowKind::Floating).isNull(); }, hasTab);
    add("workspace.detachWorkbench", QStringLiteral("当前标签移到完整工作窗口"), "窗口", {},
        [this](const CommandContext& c) { return !detachTab(c.tab, WindowKind::Workbench).isNull(); }, hasTab);
    add("window.promote", QStringLiteral("浮窗转换为完整工作窗口"), "窗口", {},
        [this](const CommandContext& c) { return promoteWindow(c.window); },
        [this](const CommandContext& c) { const auto* w = d->model.window(c.window); return w && w->kind == WindowKind::Floating; });
    auto chooseWorkbench = [this](const CommandContext& c) -> WindowId {
        if (const auto target = c.args.value("targetWindow").toUuid(); !target.isNull()) return target;
        QMenu menu(windowWidget(c.window)); applyTheme(&menu, theme());
        for (const auto id : d->model.windowOrder()) {
            if (id == c.window || d->model.window(id)->kind != WindowKind::Workbench) continue;
            auto* action = menu.addAction(windowWidget(id)->windowTitle()); action->setData(id);
        }
        if (auto* action = menu.exec(QCursor::pos())) return action->data().toUuid();
        return {};
    };
    add("window.demote", QStringLiteral("转换为另一窗口的轻量浮窗…"), "窗口", {},
        [this, chooseWorkbench](const CommandContext& c) { const auto target = chooseWorkbench(c); return !target.isNull() && demoteWindow(c.window, target); },
        [this](const CommandContext& c) { const auto* w = d->model.window(c.window); return w && w->kind == WindowKind::Workbench && d->model.windowOrder().size() > 1; });
    add("window.reassignFamily", QStringLiteral("浮窗更改所属工作窗口…"), "窗口", {},
        [this, chooseWorkbench](const CommandContext& c) { const auto target = chooseWorkbench(c); return !target.isNull() && reassignFamily(c.window, target); },
        [this](const CommandContext& c) { const auto* w = d->model.window(c.window); return w && w->kind == WindowKind::Floating; });
    add("window.focusOwner", QStringLiteral("返回所属完整工作窗口"), "窗口", QKeySequence("Ctrl+Alt+Home"),
        [this](const CommandContext& c) { const auto* w = d->model.window(c.window); if (!w) return false; const auto root = d->model.familyRoot(w->familyId); showWindow(root); activateTab(context(root).tab); return true; }, hasWindow);
    const QVector<QPair<QString, QString>> scopes = {{"family", QStringLiteral("所属窗口族")}, {"all", QStringLiteral("全部窗口")}, {"workbenches", QStringLiteral("完整窗口")}};
    for (const auto& scope : scopes) for (const bool forward : {true, false}) {
        QString key;
        if (scope.first == "family") key = forward ? "Ctrl+Alt+PgDown" : "Ctrl+Alt+PgUp";
        if (scope.first == "all") key = forward ? "Ctrl+Alt+Shift+PgDown" : "Ctrl+Alt+Shift+PgUp";
        const auto id = QStringLiteral("window.focus%1.%2").arg(forward ? "Next" : "Previous", scope.first);
        add(id, QStringLiteral("切换%1：%2").arg(scope.second, forward ? QStringLiteral("下一个") : QStringLiteral("上一个")), "窗口", QKeySequence(key),
            [this, forward, scope](const CommandContext&) { focusWindowCycle(forward, scope.first); return true; });
    }
    add("window.redock", QStringLiteral("浮窗整体停靠回所属窗口"), "窗口", {},
        [this](const CommandContext& c) {
            const auto* w = d->model.window(c.window); if (!w) return false;
            const auto root = d->model.familyRoot(w->familyId);
            const auto result = d->model.moveAllTabsToWindow(c.window, root, Placement::Right);
            if (!applyResult(result)) return false;
            showWindow(root);
            return true;
        }, [this](const CommandContext& c) { const auto* w = d->model.window(c.window); return w && w->kind == WindowKind::Floating; });
    add("window.moveAllTabs", QStringLiteral("移动本窗口全部标签…"), "窗口", {},
        [this](const CommandContext& c) {
            const auto source = c.args.value("source", c.window).toUuid();
            auto target = c.args.value("targetWindow").toUuid();
            if (target.isNull()) {
                QMenu menu(windowWidget(c.window)); applyTheme(&menu, theme());
                for (const auto id : d->model.windowOrder()) if (id != source) { auto* action = menu.addAction(windowWidget(id)->windowTitle()); action->setData(id); }
                if (auto* action = menu.exec(QCursor::pos())) target = action->data().toUuid();
            }
            if (target.isNull()) return false;
            const auto group = c.args.value("group").toUuid();
            const auto place = static_cast<Placement>(c.args.value("placement", int(Placement::Center)).toInt());
            const auto result = group.isNull() ? d->model.moveAllTabsToWindow(source, target, place) : d->model.moveAllTabs(source, group, place, c.args.value("index", -1).toInt());
            if (!applyResult(result)) return false;
            showWindow(target);
            return true;
        }, hasWindow);
    add("workspace.moveTabToWindow", QStringLiteral("移动当前标签到另一个窗口…"), "窗口", {},
        [this](const CommandContext& c) {
            const auto tab = c.args.value("tab", c.tab).toUuid();
            auto target = c.args.value("targetWindow").toUuid();
            if (target.isNull()) {
                QMenu menu(windowWidget(c.window)); applyTheme(&menu, theme());
                for (const auto id : d->model.windowOrder()) if (id != c.window) { auto* action = menu.addAction(windowWidget(id)->windowTitle()); action->setData(id); }
                if (auto* action = menu.exec(QCursor::pos())) target = action->data().toUuid();
            }
            if (target.isNull()) return false;
            const auto place = static_cast<Placement>(c.args.value("placement", int(Placement::Center)).toInt());
            return applyResult(d->model.moveTabToWindow(tab, target, place));
        }, hasTab);
    add("workspace.moveTabToEdge", QStringLiteral("当前标签收纳到边栏"), "布局", {},
        [this](const CommandContext& c) {
            const auto tab = c.args.value("tab", c.tab).toUuid();
            const auto target = c.args.value("targetWindow").toUuid();
            auto candidate = d->model;
            const auto* sourceGroup = candidate.group(candidate.groupForTab(tab));
            if (!candidate.tab(tab) || !sourceGroup) return false;
            const auto sourceWindow = sourceGroup->windowId;
            if (!target.isNull() && sourceWindow != target)
                if (!candidate.moveTabToWindow(tab, target)) return false;
            if (!candidate.moveTabToEdge(tab, static_cast<Edge>(c.args.value("edge", int(Edge::Left)).toInt()), c.args.value("pinned").toBool())) return false;
            d->model = std::move(candidate); refresh(); return true;
        }, hasTab);
    add("window.moveAllToEdge", QStringLiteral("整窗标签收纳到目标边栏"), "布局", {},
        [this](const CommandContext& c) {
            const auto source = c.args.value("source", c.window).toUuid();
            const auto target = c.args.value("targetWindow").toUuid(); if (target.isNull()) return false;
            return moveAllTabsToEdge(source, target, static_cast<Edge>(c.args.value("edge", 0).toInt()), c.args.value("pinned").toBool());
        }, hasWindow);
    const QVector<QPair<QString, Placement>> directions = {{"left", Placement::Left}, {"right", Placement::Right}, {"up", Placement::Top}, {"down", Placement::Bottom}};
    for (const auto& direction : directions) {
        const auto keyName = direction.first == "up" ? "Up" : direction.first == "down" ? "Down" : direction.first == "left" ? "Left" : "Right";
        const auto label = direction.first == "up" ? QStringLiteral("上") : direction.first == "down" ? QStringLiteral("下") : direction.first == "left" ? QStringLiteral("左") : QStringLiteral("右");
        add("workspace.focusGroup." + direction.first, QStringLiteral("聚焦%1侧标签组").arg(label), "布局", QKeySequence("Ctrl+Alt+" + QString(keyName)),
            [this, direction](const CommandContext&) { return navigate(direction.second, false); });
        add("pane.focus." + direction.first, QStringLiteral("聚焦%1侧窗格").arg(label), "布局", QKeySequence("Alt+" + QString(keyName)),
            [this, direction](const CommandContext&) { return navigate(direction.second, true); });
        add("workspace.resizeGroup." + direction.first, QStringLiteral("向%1调整标签组边界").arg(label), "布局", QKeySequence("Ctrl+Alt+Shift+" + QString(keyName)),
            [this, direction](const CommandContext&) { resizeActive(direction.second, false); return true; });
        add("pane.resize." + direction.first, QStringLiteral("向%1调整窗格边界").arg(label), "布局", QKeySequence("Alt+Shift+" + QString(keyName)),
            [this, direction](const CommandContext&) { resizeActive(direction.second, true); return true; });
        add("workspace.splitGroup." + direction.first, QStringLiteral("新建%1侧标签组").arg(label), "布局", {},
            [this, direction](const CommandContext& c) {
                if (!d->model.tab(c.tab)) return false;
                const auto* target = d->model.group(c.group);
                if (!target || target->location != GroupLocation::Central) return false;
                auto options = d->descriptors.value(c.pane); options.title += QStringLiteral(" · 新组");
                auto* content = createContent(options, {}); if (!content) return false;
                const auto tab = addTab(c.window, content, options, c.group);
                if (tab.isNull()) { delete content; return false; }
                return moveTab(tab, c.group, direction.second);
            }, [this](const CommandContext& c) { const auto* group = d->model.group(c.group); return d->model.tab(c.tab) && group && group->location == GroupLocation::Central; });
        add("pane.split." + direction.first, QStringLiteral("向%1拆分窗格").arg(label), "布局",
            direction.second == Placement::Right ? QKeySequence("Ctrl+Shift+E") : direction.second == Placement::Bottom ? QKeySequence("Ctrl+Shift+O") : QKeySequence{},
            [this, direction](const CommandContext& c) {
                auto* content = createContent(d->descriptors.value(c.pane), {});
                if (!content) return false;
                const auto pane = splitPane(c.tab, c.pane, content, direction.second);
                if (pane.isNull()) { delete content; return false; } return true;
            }, hasTab);
        const auto edge = direction.second == Placement::Left ? Edge::Left : direction.second == Placement::Right ? Edge::Right : direction.second == Placement::Top ? Edge::Top : Edge::Bottom;
        add("workspace.hideToEdge." + direction.first, QStringLiteral("当前标签收纳到%1边栏").arg(label), "布局", {},
            [this, edge](const CommandContext& c) { return moveTabToEdge(c.tab, edge); }, hasTab);
    }
    for (const bool next : {true, false}) add(next ? "workspace.nextTab" : "workspace.previousTab",
        next ? QStringLiteral("当前组的下一个标签") : QStringLiteral("当前组的上一个标签"), "视图",
        QKeySequence(next ? "Ctrl+PgDown" : "Ctrl+PgUp"), [this, next](const CommandContext& c) {
            const auto* group = d->model.group(c.group); if (!group || group->tabs.isEmpty()) return false;
            const auto index = (group->tabs.indexOf(c.tab) + (next ? 1 : group->tabs.size() - 1)) % group->tabs.size();
            return activateTab(group->tabs[index]);
        }, hasTab);
    add("workspace.pinGroup", QStringLiteral("固定边栏标签组"), "布局", {},
        [this](const CommandContext& c) { return pinGroup(c.group, true); },
        [this](const CommandContext& c) { const auto* g = d->model.group(c.group); return g && g->location == GroupLocation::AutoHide; });
    add("workspace.unpinGroup", QStringLiteral("取消固定并收纳到边栏"), "布局", {},
        [this](const CommandContext& c) { return pinGroup(c.group, false); },
        [this](const CommandContext& c) { const auto* g = d->model.group(c.group); return g && g->location == GroupLocation::Pinned; });
    add("workspace.pinTab", QStringLiteral("固定当前边栏标签"), "布局", {},
        [this](const CommandContext& c) { return pinTab(c.tab, true); },
        [this](const CommandContext& c) { const auto* g = d->model.group(d->model.groupForTab(c.tab)); return g && g->location == GroupLocation::AutoHide; });
    add("workspace.unpinTab", QStringLiteral("取消固定当前标签"), "布局", {},
        [this](const CommandContext& c) { return pinTab(c.tab, false); },
        [this](const CommandContext& c) { const auto* g = d->model.group(d->model.groupForTab(c.tab)); return g && g->location == GroupLocation::Pinned; });
    for (const bool pinned : {true, false}) add(pinned ? "workspace.pinEdge" : "workspace.unpinEdge",
        pinned ? QStringLiteral("固定该侧全部标签") : QStringLiteral("取消固定该侧全部标签"), "布局", {},
        [this, pinned](const CommandContext& c) {
            const auto* group = d->model.group(c.group);
            const auto edge = static_cast<Edge>(c.args.value("edge", group ? int(group->edge) : int(Edge::Left)).toInt());
            return pinEdge(c.window, edge, pinned);
        }, hasWindow);
    for (const auto& scope : {QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("others"), QStringLiteral("all")}) {
        const auto title = scope == "left" ? QStringLiteral("关闭左侧标签") : scope == "right" ? QStringLiteral("关闭右侧标签")
            : scope == "others" ? QStringLiteral("关闭其他标签") : QStringLiteral("关闭该组全部标签");
        auto selected = [this, scope](const CommandContext& c) {
            QVector<TabId> tabs;
            const auto* group = d->model.group(d->model.groupForTab(c.tab)); if (!group) return tabs;
            const auto current = group->tabs.indexOf(c.tab);
            for (int i = 0; i < group->tabs.size(); ++i)
                if (scope == "all" || (scope == "others" && i != current) || (scope == "left" && i < current) || (scope == "right" && i > current)) tabs << group->tabs[i];
            return tabs;
        };
        add("workspace.closeTabs." + scope, title, "文件", {},
            [this, selected](const CommandContext& c) { return closeTabs(selected(c)); },
            [selected](const CommandContext& c) { return !selected(c).isEmpty(); });
    }
    add("workspace.restoreGroupToCenter", QStringLiteral("边栏标签组移回中央"), "布局", {},
        [this](const CommandContext& c) { return moveGroupToCenter(c.group); }, hasTab);
    add("pane.extractFloating", QStringLiteral("当前窗格移到轻量浮窗"), "窗口", {},
        [this](const CommandContext& c) { return !extractPane(c.tab, c.pane, WindowKind::Floating).isNull(); }, hasTab);
    add("pane.extractWorkbench", QStringLiteral("当前窗格移到完整窗口"), "窗口", {},
        [this](const CommandContext& c) { return !extractPane(c.tab, c.pane, WindowKind::Workbench).isNull(); }, hasTab);
    add("pane.close", QStringLiteral("关闭当前窗格"), "布局", QKeySequence("Ctrl+Shift+W"),
        [this](const CommandContext& c) { return closePane(c.tab, c.pane); }, hasTab);
    add("window.toggleStripsLock", QStringLiteral("锁定／解锁菜单与工具条"), "工具", {},
        [this](const CommandContext& c) { auto* window = d->windows.value(c.window).data(); if (!window || !window->strips()) return false; window->strips()->setLocked(!window->strips()->isLocked()); return true; },
        [this](const CommandContext& c) { const auto* w = d->model.window(c.window); return w && w->kind == WindowKind::Workbench; });
    add("window.drag.selectTarget", QStringLiteral("拖拽中选择目标窗口"), "窗口", {},
        [this](const CommandContext&) { d->drag->selectTarget(); return true; }, [this](const CommandContext&) { return d->drag->isActive(); });
    add("window.drag.cancel", QStringLiteral("取消当前拖拽"), "窗口", {},
        [this](const CommandContext&) { d->drag->cancel(); return true; }, [this](const CommandContext&) { return d->drag->isActive(); });
    add("layout.setRatio", QStringLiteral("设置分割比例"), "布局", {},
        [this](const CommandContext& c) { const auto result = d->model.setSplitRatio(c.args.value("node").toUuid(), c.args.value("ratio").toDouble()); if (result) { d->drag->releaseForMutation(); emit layoutChanged(); } return result.ok; });
    add("layout.setEdgeExtent", QStringLiteral("设置固定边栏分区大小"), "布局", {},
        [this](const CommandContext& c) { const auto result = d->model.setEdgeGroupExtent(c.args.value("group").toUuid(), c.args.value("extent").toInt()); if (result) { d->drag->releaseForMutation(); emit layoutChanged(); } return result.ok; });
}
void WindowManager::showTabMenu(TabId tab, QPoint global)
{
    if (!d->model.tab(tab)) return;
    const auto group = d->model.groupForTab(tab); const auto window = d->model.group(group)->windowId;
    activateTab(tab);
    const auto captured = context(window);
    const auto location = d->model.group(group)->location;
    QMenu menu(windowWidget(window)); applyTheme(&menu, theme());
    const QStringList ids = {"workspace.detachFloating", "workspace.detachWorkbench", "workspace.moveTabToWindow",
        "workspace.splitGroup.left", "workspace.splitGroup.right", "workspace.splitGroup.up", "workspace.splitGroup.down",
        "pane.split.right", "pane.split.down", "pane.extractFloating", "pane.extractWorkbench",
        "workspace.hideToEdge.left", "workspace.hideToEdge.right", "workspace.hideToEdge.up", "workspace.hideToEdge.down",
        "workspace.pinTab", "workspace.unpinTab", "workspace.pinEdge", "workspace.unpinEdge", "workspace.restoreGroupToCenter",
        "workspace.closeTab", "workspace.closeTabs.left", "workspace.closeTabs.right", "workspace.closeTabs.others", "workspace.closeTabs.all"};
    for (const auto& id : ids) if (const auto* command = d->commands.find(id)) {
        if (id == "workspace.pinTab" && location != GroupLocation::AutoHide) continue;
        if (id == "workspace.unpinTab" && location != GroupLocation::Pinned) continue;
        if ((id == "workspace.pinEdge" || id == "workspace.unpinEdge" || id == "workspace.restoreGroupToCenter") && location == GroupLocation::Central) continue;
        auto* action = menu.addAction(command->title); action->setData(id);
        action->setObjectName(id); action->setEnabled(!command->enabled || command->enabled(captured));
    }
    if (auto* action = menu.exec(global)) { const auto id = action->data().toString(); QTimer::singleShot(0, this, [this, id, captured] { d->commands.execute(id, captured); }); }
}
void WindowManager::showWindowMenu(WindowId window, QPoint global)
{
    const auto* record = d->model.window(window); if (!record) return;
    const auto kind = record->kind; const auto captured = context(window);
    QMenu menu(windowWidget(window)); applyTheme(&menu, theme());
    const QStringList ids = {"window.promote", "window.demote", "window.reassignFamily", "window.redock",
        "window.moveAllTabs", "window.select", "window.toggleStripsLock", "window.settings", "window.close"};
    for (const auto& id : ids) if (const auto* command = d->commands.find(id)) {
        if ((id == "window.promote" || id == "window.reassignFamily" || id == "window.redock") && kind != WindowKind::Floating) continue;
        if ((id == "window.demote" || id == "window.toggleStripsLock") && kind != WindowKind::Workbench) continue;
        auto* action = menu.addAction(command->title); action->setData(id);
        action->setObjectName(id); action->setEnabled(!command->enabled || command->enabled(captured));
    }
    if (auto* action = menu.exec(global)) { const auto id = action->data().toString(); QTimer::singleShot(0, this, [this, id, captured] { d->commands.execute(id, captured); }); }
}
void WindowManager::showPaneMenu(TabId tab, PaneId pane, QPoint global, QWidget* content)
{
    if (!d->model.panes(tab).contains(pane)) return;
    activateTab(tab); focusPane(tab, pane);
    const auto group = d->model.groupForTab(tab); const auto window = d->model.group(group)->windowId;
    const auto captured = context(window);
    QMenu* menu = nullptr;
    for (auto* editor = content; editor; editor = editor->parentWidget()) {
        if (auto* plain = qobject_cast<QPlainTextEdit*>(editor)) { menu = plain->createStandardContextMenu(); break; }
        if (auto* rich = qobject_cast<QTextEdit*>(editor)) { menu = rich->createStandardContextMenu(); break; }
        if (auto* input = qobject_cast<QLineEdit*>(editor)) { menu = input->createStandardContextMenu(); break; }
        if (editor->property("paneId").toUuid() == pane) break;
    }
    if (!menu) menu = new QMenu(windowWidget(window));
    else menu->addSeparator();
    QPointer<QMenu> retained(menu); applyTheme(menu, theme());
    for (const auto& id : {QStringLiteral("pane.close"), QStringLiteral("pane.split.right"), QStringLiteral("pane.split.down"),
                          QStringLiteral("pane.extractFloating"), QStringLiteral("pane.extractWorkbench")})
        if (const auto* command = d->commands.find(id)) {
            auto* action = menu->addAction(command->title); action->setObjectName(id); action->setProperty("windowSystemCommand", id);
            action->setEnabled(!command->enabled || command->enabled(captured));
        }
    QString chosen;
    if (auto* action = menu->exec(global)) chosen = action->property("windowSystemCommand").toString();
    delete retained.data();
    if (!chosen.isEmpty()) QTimer::singleShot(0, this, [this, chosen, captured] { d->commands.execute(chosen, captured); });
}
} // namespace window_system
