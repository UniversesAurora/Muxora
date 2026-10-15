#include "window_manager_p.hpp"
#include "tab_presentation_commands.hpp"
#include <window_system/window_manager.hpp>
#include "chrome/workspace_window.hpp"
#include "chrome/command_strip.hpp"
#include "commands/command_dialogs.hpp"
#include "commands/command_menus.hpp"
#include "commands/popup_menu.hpp"
#include "commands/peek_size_command.hpp"
#include "commands/toolbar_move_command.hpp"
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
#include <QSignalBlocker>
#include <QSplitter>
#include <algorithm>
#include <limits>

namespace window_system {
using StripSizing = CommandStripHost::StripSizing;
void WindowManager::registerWindowCommands()
{
    registerTabPresentationCommands(this);
    auto add = [this](QString id, QString title, QString category, QKeySequence shortcut,
                      std::function<bool(const CommandContext&)> handler,
                      std::function<bool(const CommandContext&)> enabled = {}) {
        d->commands.add({std::move(id), std::move(title), std::move(category), std::move(shortcut), std::move(enabled), std::move(handler)});
    };
    auto hasTab = [this](const CommandContext& c) { return d->model.tab(c.tab) != nullptr; };
    auto hasWindow = [this](const CommandContext& c) { return d->model.window(c.window) != nullptr; };
    auto hasPane = [this](const CommandContext& c) {
        return d->model.panes(c.args.value("tab", c.tab).toUuid()).contains(c.args.value("pane", c.pane).toUuid());
    };
    auto chooseWindow = [this](WindowId source, const CommandContext& c) -> WindowId {
        if (c.args.contains("targetWindow")) return c.args.value("targetWindow").toUuid();
        QPointer<QWidget> owner(windowWidget(c.window));
        DismissOnlyMenu picker;
        picker.setObjectName(QStringLiteral("ContentWindowPicker"));
        applyTheme(&picker, theme());
        for (const auto id : d->model.windowOrder()) if (id != source) {
            auto* action = picker.addAction(windowWidget(id)->windowTitle());
            action->setData(id);
        }
        if (picker.actions().isEmpty()) return {};
        if (owner) connect(owner, &QObject::destroyed, &picker, &QMenu::close);
        if (const auto* selected = picker.exec(QCursor::pos()); owner && selected) return selected->data().toUuid();
        return {};
    };
    add("window.tabBars.toggle", QStringLiteral("显示标签栏"), "视图", QKeySequence("Ctrl+Alt+T"),
        [this](const CommandContext& c) {
            return setTabBarsVisible(c.window, !tabBarsVisible(c.window));
        }, hasWindow);
    add("window.sideBars.toggle", QStringLiteral("显示侧边栏"), "视图", QKeySequence("Ctrl+Alt+B"),
        [this](const CommandContext& c) {
            return setSideBarsVisible(c.window, !sideBarsVisible(c.window));
        }, hasWindow);
    add("window.toolbars.toggle", QStringLiteral("显示工具栏"), "视图", QKeySequence("Ctrl+Alt+H"),
        [this](const CommandContext& c) { return setToolbarsVisible(c.window, !toolbarsVisible(c.window)); }, hasWindow);
    add("window.statusBar.toggle", QStringLiteral("显示状态栏"), "视图", QKeySequence("Ctrl+Alt+S"),
        [this](const CommandContext& c) { return setStatusBarVisible(c.window, !statusBarVisible(c.window)); }, hasWindow);
    add("window.statusItem.setVisible", QStringLiteral("切换状态栏控件"), "视图", {},
        [this](const CommandContext& c) {
            const auto id = c.args.value("item").toString();
            if (!WorkspaceWindow::statusItemIds().contains(id)) return false;
            if (c.args.contains("visible") && c.args.value("visible").metaType().id() != QMetaType::Bool) return false;
            return setStatusItemVisible(c.window, id,
                c.args.value("visible", !statusItemVisible(c.window, id)).toBool());
        }, hasWindow);
    for (const auto mode : {ViewMode::WindowFullscreen, ViewMode::GroupFullscreen, ViewMode::PaneFullscreen}) {
        const bool group = mode == ViewMode::GroupFullscreen;
        const bool pane = mode == ViewMode::PaneFullscreen;
        const auto id = group ? "window.fullscreen.group" : pane ? "window.fullscreen.pane" : "window.fullscreen.toggle";
        const auto title = group ? QStringLiteral("标签组全屏") : pane ? QStringLiteral("窗格全屏") : QStringLiteral("窗口全屏");
        const auto key = group ? "Ctrl+Shift+F11" : pane ? "Shift+F11" : "F11";
        std::function<bool(const CommandContext&)> enabled = hasWindow;
        if (group || pane) enabled = hasTab;
        add(id, title, "视图", QKeySequence(key), [this, mode](const CommandContext& c) {
            return setViewMode(c.window, viewMode(c.window) == mode ? ViewMode::Normal : mode);
        }, std::move(enabled));
    }
    add("window.fullscreen.exit", QStringLiteral("退出全屏"), "视图", QKeySequence(Qt::Key_Escape),
        [this](const CommandContext& c) { return setViewMode(c.window, ViewMode::Normal); },
        [this](const CommandContext& c) { return viewMode(c.window) != ViewMode::Normal; });
    add("window.zen.toggle", QStringLiteral("禅模式"), "视图", QKeySequence("Ctrl+K, Z"),
        [this](const CommandContext& c) { return setZenMode(c.window, !zenMode(c.window)); },
        [this](const CommandContext& c) { return viewMode(c.window) != ViewMode::Normal; });
    for (const bool top : {true, false}) {
        add(top ? "tab.topBar.setSharing" : "tab.bottomBar.setSharing",
            top ? QStringLiteral("切换上方窗格栏共享") : QStringLiteral("切换下方窗格栏共享"), "布局", {},
            [this, top](const CommandContext& c) {
                const auto options = tabOptions(c.tab);
                auto sharing = top ? options.topBarSharing : options.bottomBarSharing;
                if (c.args.contains("sharing")) {
                    const auto value = c.args.value("sharing").toString();
                    if (value != "shared" && value != "perPane") return false;
                    sharing = value == "shared" ? PaneBarSharing::Shared : PaneBarSharing::PerPane;
                } else sharing = sharing == PaneBarSharing::Shared ? PaneBarSharing::PerPane : PaneBarSharing::Shared;
                return top ? setTabTopBarSharing(c.tab, sharing) : setTabBottomBarSharing(c.tab, sharing);
            }, hasTab);
    }
    add("workbench.showCommandPalette", QStringLiteral("显示命令面板"), "视图", QKeySequence("Ctrl+Shift+P"),
        [this](const CommandContext& c) { showCommandPalette(c.window); return true; });
    add("window.select", QStringLiteral("选择窗口…"), "窗口", QKeySequence("Ctrl+Alt+W"),
        [this](const CommandContext& c) { showWindowSelector(c.window); return true; });
    add("window.activate", QStringLiteral("激活窗口"), "窗口", {},
        [this](const CommandContext& c) { return activateWindow(c.args.value("targetWindow").toUuid()); },
        [this](const CommandContext& c) { return d->model.window(c.args.value("targetWindow").toUuid()) != nullptr; });
    add("window.settings", QStringLiteral("设置"), "工具", QKeySequence("Ctrl+,"),
        [this](const CommandContext& c) { showSettings(c.window); return true; });
    add("preferences.openKeyboardShortcuts", QStringLiteral("键盘快捷方式"), "工具", QKeySequence("Ctrl+K, Ctrl+S"),
        [this](const CommandContext& c) { openShortcutEditor(this, windowWidget(c.window)); return true; });
    registerToolbarMoveCommand(this);
    registerPeekSizeCommands(this);
    registerTabGroupCommands();
    add("window.toolbar.setSizing", QStringLiteral("切换工具条弹性空间…"), "布局", {},
        [this](const CommandContext& c) {
            auto* window = static_cast<WorkspaceWindow*>(windowWidget(c.window));
            auto* strips = window ? window->strips() : nullptr;
            if (!strips) return false;
            const auto value = c.args.value("sizing").toString();
            if (!value.isEmpty() && value != "elastic" && value != "content") return false;
            QString id = c.args.value("strip").toString();
            if (!c.args.contains("strip")) {
                QPointer<WorkspaceWindow> observed(window);
                DismissOnlyMenu picker;
                picker.setObjectName(QStringLiteral("ToolbarSizingPicker"));
                applyTheme(&picker, theme());
                for (const auto& stripId : strips->stripIds()) if (strips->isStripSizingEditable(stripId)) {
                    auto* action = picker.addAction(strips->stripTitle(stripId));
                    action->setData(stripId);
                }
                if (picker.actions().isEmpty()) return false;
                connect(window, &QObject::destroyed, &picker, &QMenu::close);
                const auto* chosen = picker.exec(QCursor::pos());
                if (!observed) return false;
                if (!chosen) return true;
                id = chosen->data().toString();
                strips = observed->strips();
            }
            if (!strips || !strips->stripIds().contains(id)) return false;
            const auto sizing = value.isEmpty()
                ? (strips->stripSizing(id) == StripSizing::Elastic ? StripSizing::Content : StripSizing::Elastic)
                : (value == "elastic" ? StripSizing::Elastic : StripSizing::Content);
            return strips->setStripSizing(id, sizing);
        }, [this](const CommandContext& c) {
            auto* window = static_cast<WorkspaceWindow*>(windowWidget(c.window));
            if (!window || !window->strips()) return false;
            if (c.args.contains("strip")) return true;
            for (const auto& id : window->strips()->stripIds())
                if (window->strips()->isStripSizingEditable(id)) return true;
            return false;
        });
    add("window.toolbar.resetSize", QStringLiteral("重置工具条大小…"), "布局", {},
        [this](const CommandContext& c) {
            QPointer<WorkspaceWindow> window = d->windows.value(c.window);
            if (!window || !window->strips()) return false;
            if (c.args.contains("strip")) return resetToolbarWidth(c.window, c.args.value("strip").toString());
            // Keep the picker independent of QWidget parent ownership: its
            // window may close while the menu's event loop is running.
            DismissOnlyMenu picker;
            picker.setObjectName(QStringLiteral("ToolbarResetPicker"));
            applyTheme(&picker, theme());
            for (const auto& id : window->strips()->stripIds()) {
                auto* action = picker.addAction(window->strips()->stripTitle(id));
                action->setData(id);
            }
            connect(window, &QObject::destroyed, &picker, &QMenu::close);
            const auto* chosen = picker.exec(QCursor::pos());
            if (!window) return false;
            return !chosen || resetToolbarWidth(c.window, chosen->data().toString());
        }, [this](const CommandContext& c) {
            const auto window = d->windows.value(c.window);
            return window && window->strips();
        });
    add("workspace.closeTab", QStringLiteral("关闭当前标签"), "文件", QKeySequence("Ctrl+W"),
        [this](const CommandContext& c) { return closeTab(c.tab); }, hasTab);
    add("window.close", QStringLiteral("关闭当前窗口"), "窗口", QKeySequence("Alt+F4"),
        [this](const CommandContext& c) { return requestCloseWindow(c.window); }, hasWindow);
    add("window.new", QStringLiteral("新建完整工作窗口"), "文件", QKeySequence("Ctrl+Shift+N"),
        [this](const CommandContext& c) {
            auto options = d->options.value(c.window);
            // Keep the caption/size, but use the latest kind defaults rather
            // than a restored source window's older visibility overrides.
            options.toolbarsVisible = options.statusBarVisible = options.tabBarsVisible = options.sideBarsVisible = true;
            options.inheritVisibilityProfile = true;
            const QPointer<WindowManager> self(this);
            const auto id = createWindow(WindowKind::Workbench, options);
            if (!self || id.isNull()) return false;
            // An explicitly opened window starts with the host's usual tab.
            // Otherwise close-empty policy can delete it on the next turn.
            if (!d->defaultTabCommand.isEmpty() && !requestNewTab(id)) {
                if (self) requestCloseWindow(id);
                return false;
            }
            if (!self || !d->model.window(id)) return false;
            showWindow(id);
            return self && activateWindow(id);
        });
    add("workspace.detachFloating", QStringLiteral("当前标签移到轻量浮窗"), "窗口", QKeySequence("Ctrl+Shift+D"),
        [this](const CommandContext& c) { return !detachTab(c.tab, WindowKind::Floating).isNull(); }, hasTab);
    add("workspace.detachWorkbench", QStringLiteral("当前标签移到完整工作窗口"), "窗口", {},
        [this](const CommandContext& c) { return !detachTab(c.tab, WindowKind::Workbench).isNull(); }, hasTab);
    add("window.promote", QStringLiteral("浮窗转换为完整工作窗口"), "窗口", {},
        [this](const CommandContext& c) { return promoteWindow(c.window); },
        [this](const CommandContext& c) { const auto* w = d->model.window(c.window); return w && w->kind == WindowKind::Floating; });
    auto chooseWorkbench = [this](const CommandContext& c) -> WindowId {
        if (const auto target = c.args.value("targetWindow").toUuid(); !target.isNull()) return target;
        DismissOnlyMenu menu(windowWidget(c.window)); applyTheme(&menu, theme());
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
                DismissOnlyMenu menu(windowWidget(c.window)); applyTheme(&menu, theme());
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
        [this, chooseWindow](const CommandContext& c) {
            const auto tab = c.args.value("tab", c.tab).toUuid();
            const auto* source = d->model.group(d->model.groupForTab(tab));
            if (!source) return false;
            const auto target = chooseWindow(source->windowId, c);
            if (target.isNull()) return false;
            const auto place = static_cast<Placement>(c.args.value("placement", int(Placement::Center)).toInt());
            return moveTabToWindow(tab, target, place);
        }, hasTab);
    add("pane.moveToWindow", QStringLiteral("移动当前窗格到已有窗口…"), "窗口", {},
        [this, chooseWindow](const CommandContext& c) {
            const auto tab = c.args.value("tab", c.tab).toUuid();
            const auto pane = c.args.value("pane", c.pane).toUuid();
            const auto* source = d->model.group(d->model.groupForTab(tab));
            if (!source) return false;
            const auto target = chooseWindow(source->windowId, c);
            return !target.isNull() && movePaneToWindow(tab, pane, target,
                static_cast<Placement>(c.args.value("placement", int(Placement::Right)).toInt()));
        }, hasPane);
    add("pane.moveToTab", QStringLiteral("移动当前窗格到其他标签…"), "布局", {},
        [this](const CommandContext& c) {
            const auto sourceTab = c.args.value("tab", c.tab).toUuid();
            const auto sourcePane = c.args.value("pane", c.pane).toUuid();
            auto target = c.args.value("targetTab").toUuid();
            if (!c.args.contains("targetTab")) {
                QPointer<QWidget> owner(windowWidget(c.window));
                DismissOnlyMenu picker;
                picker.setObjectName(QStringLiteral("PaneTabPicker"));
                applyTheme(&picker, theme());
                for (const auto window : d->model.windowOrder())
                    for (const auto tab : d->model.orderedTabs(window)) if (tab != sourceTab) {
                        auto* action = picker.addAction(QStringLiteral("%1 — %2").arg(d->model.tab(tab)->title, windowWidget(window)->windowTitle()));
                        action->setData(tab);
                    }
                if (picker.actions().isEmpty()) return false;
                if (owner) connect(owner, &QObject::destroyed, &picker, &QMenu::close);
                if (const auto* selected = picker.exec(QCursor::pos()); owner && selected) target = selected->data().toUuid();
            }
            return !target.isNull() && movePane(sourceTab, sourcePane, target, c.args.value("targetPane").toUuid(),
                static_cast<Placement>(c.args.value("placement", int(Placement::Right)).toInt()));
        }, hasPane);
    for (const bool forward : {false, true}) {
        const auto suffix = forward ? QStringLiteral("next") : QStringLiteral("previous");
        const auto label = forward ? QStringLiteral("下一个") : QStringLiteral("上一个");
        add("workspace.moveTabToWindow." + suffix, QStringLiteral("将当前标签移到%1窗口").arg(label), "窗口", {},
            [this, forward](const CommandContext& c) { return moveTabToWindowCycle(c.tab, forward); },
            [this](const CommandContext& c) { return d->model.tab(c.tab) && d->model.windowOrder().size() > 1; });
        add("pane.moveToWindow." + suffix, QStringLiteral("将当前窗格移到%1窗口").arg(label), "窗口", {},
            [this, forward](const CommandContext& c) { return movePaneToWindowCycle(c.tab, c.pane, forward); },
            [this, hasPane](const CommandContext& c) { return hasPane(c) && d->model.windowOrder().size() > 1; });
        add("pane.moveToTab." + suffix, QStringLiteral("将当前窗格移到%1标签").arg(label), "布局",
            QKeySequence(forward ? "Alt+PgDown" : "Alt+PgUp"),
            [this, forward](const CommandContext& c) { return movePaneToAdjacentTab(c.tab, c.pane, forward); },
            [this, hasPane](const CommandContext& c) { const auto* group = d->model.group(c.group); return hasPane(c) && group && group->tabs.size() > 1; });
    }
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
    add("workspace.moveTabToEdgeBar", QStringLiteral("将当前标签插入边栏"), "布局", {},
        [this](const CommandContext& c) {
            const auto* group = d->model.group(d->model.groupForTab(c.args.value("tab", c.tab).toUuid()));
            const auto target = c.args.value("targetWindow", c.window).toUuid();
            const auto edge = static_cast<Edge>(c.args.value("edge", group ? int(group->edge) : int(Edge::Left)).toInt());
            return moveTabToEdgeBar(c.args.value("tab", c.tab).toUuid(), target, edge, c.args.value("index", -1).toInt());
        }, hasTab);
    add("window.moveAllToEdgeBar", QStringLiteral("将整窗标签插入边栏"), "布局", {},
        [this](const CommandContext& c) {
            return moveAllTabsToEdgeBar(c.args.value("source", c.window).toUuid(), c.args.value("targetWindow").toUuid(),
                static_cast<Edge>(c.args.value("edge", int(Edge::Left)).toInt()), c.args.value("index", -1).toInt());
        }, hasWindow);
    for (const bool next : {false, true}) {
        auto canReorder = [this](const CommandContext& c) {
            const auto* group = d->model.group(d->model.groupForTab(c.tab));
            if (!group) return false;
            const auto tabs = group->location == GroupLocation::AutoHide
                ? d->model.autoHiddenTabs(group->windowId, group->edge) : group->tabs;
            return tabs.contains(c.tab) && tabs.size() > 1;
        };
        add(next ? "workspace.reorderTab.next" : "workspace.reorderTab.previous",
            next ? QStringLiteral("将当前标签后移一位") : QStringLiteral("将当前标签前移一位"), "布局",
            QKeySequence(next ? "Ctrl+Shift+PgDown" : "Ctrl+Shift+PgUp"),
            [this, next](const CommandContext& c) { return reorderTab(c.tab, next); }, canReorder);
    }
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
            [this, direction](const CommandContext& c) { return navigate(direction.second, false, c); });
        add("pane.focus." + direction.first, QStringLiteral("聚焦%1侧窗格").arg(label), "布局", QKeySequence("Alt+" + QString(keyName)),
            [this, direction](const CommandContext& c) { return navigate(direction.second, true, c); });
        add("workspace.moveTabToGroup." + direction.first, QStringLiteral("将当前标签移到%1侧标签组").arg(label), "布局", {},
            [this, direction](const CommandContext& c) { return moveTabToAdjacentGroup(c.tab, direction.second); },
            [this, direction](const CommandContext& c) { return !adjacentGroup(c.tab, direction.second).isNull(); });
        add("pane.moveToGroup." + direction.first, QStringLiteral("将当前窗格移到%1侧标签组").arg(label), "布局", {},
            [this, direction](const CommandContext& c) { return movePaneToAdjacentGroup(c.tab, c.pane, direction.second); },
            [this, direction, hasPane](const CommandContext& c) { return hasPane(c) && !adjacentGroup(c.tab, direction.second).isNull(); });
        add("pane.swap." + direction.first, QStringLiteral("与%1侧窗格交换位置").arg(label), "布局", {},
            [this, direction](const CommandContext& c) { return swapPaneDirection(c.tab, c.pane, direction.second); },
            [this, direction](const CommandContext& c) { return !adjacentPane(c.tab, c.pane, direction.second).isNull(); });
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
        [this](const CommandContext& c) { const auto* window = d->windows.value(c.window).data(); return window && window->strips(); });
    add("window.drag.selectTarget", QStringLiteral("拖拽中选择目标窗口"), "窗口", {},
        [this](const CommandContext&) { d->drag->selectTarget(); return true; }, [this](const CommandContext&) { return d->drag->isActive(); });
    add("window.drag.cancel", QStringLiteral("取消当前拖拽"), "窗口", {},
        [this](const CommandContext&) { d->drag->cancel(); return true; }, [this](const CommandContext&) { return d->drag->isActive(); });
    add("layout.setRatio", QStringLiteral("设置分割比例"), "布局", {},
        [this](const CommandContext& c) {
            const auto node = c.args.value("node").toUuid();
            const auto ratio = c.args.value("ratio").toDouble();
            if (!d->model.setSplitRatio(node, ratio)) return false;
            d->drag->releaseForMutation();
            d->navigationAnchor.reset();
            auto update = [node, ratio](QWidget* owner) {
                if (!owner) return false;
                for (auto* splitter : owner->findChildren<QSplitter*>()) {
                    if (splitter->property("splitNodeId").toUuid() != node || !splitter->isVisibleTo(owner)) continue;
                    const auto sizes = splitter->sizes();
                    const int available = qMax(1000, sizes.value(0) + sizes.value(1));
                    const QSignalBlocker suppressFeedback(splitter);
                    splitter->setSizes({qRound(available * ratio), qRound(available * (1. - ratio))});
                    return true;
                }
                return false;
            };
            bool updated = false;
            for (const auto& view : d->tabs) if (view && update(view)) { updated = true; break; }
            if (!updated) for (const auto& view : d->windows) if (view && update(view->surface())) break;
            emit layoutChanged();
            return true;
        });
    add("layout.setEdgeExtent", QStringLiteral("设置固定边栏分区大小"), "布局", {},
        [this](const CommandContext& c) { const auto result = d->model.setEdgeGroupExtent(c.args.value("group").toUuid(), c.args.value("extent").toInt()); if (result) { d->drag->releaseForMutation(); emit layoutChanged(); } return result.ok; });
}
void WindowManager::showTabMenu(TabId tab, QPoint global)
{
    if (!d->model.tab(tab)) return;
    const auto group = d->model.groupForTab(tab); const auto window = d->model.group(group)->windowId;
    activateTab(tab);
    const auto captured = context(window);
    DismissOnlyMenu menu(windowWidget(window)); applyTheme(&menu, theme());
    populateContextMenu(&menu, CommandMenuSurface::Tab, this, captured);
    if (auto* action = menu.exec(global)) { const auto id = action->data().toString(); QTimer::singleShot(0, this, [this, id, captured] { d->commands.execute(id, captured); }); }
}
void WindowManager::showWindowMenu(WindowId window, QPoint global)
{
    const auto* record = d->model.window(window); if (!record) return;
    const auto captured = context(window);
    DismissOnlyMenu menu(windowWidget(window)); applyTheme(&menu, theme());
    populateContextMenu(&menu, CommandMenuSurface::Window, this, captured);
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
    if (!menu) menu = new DismissOnlyMenu(windowWidget(window));
    QPointer<QMenu> retained(menu); applyTheme(menu, theme());
    populateContextMenu(menu, CommandMenuSurface::Pane, this, captured);
    QString chosen;
    if (auto* action = menu->exec(global)) chosen = action->property("windowSystemCommand").toString();
    delete retained.data();
    if (!chosen.isEmpty()) QTimer::singleShot(0, this, [this, chosen, captured] { d->commands.execute(chosen, captured); });
}
} // namespace window_system
