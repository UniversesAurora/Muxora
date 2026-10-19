#include <QCoreApplication>
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
    add("window.tabBars.toggle", QCoreApplication::translate("WindowSystem", "显示标签栏"), "视图", QKeySequence("Ctrl+Alt+T"),
        [this](const CommandContext& c) {
            return setTabBarsVisible(c.window, !tabBarsVisible(c.window));
        }, hasWindow);
    add("window.sideBars.toggle", QCoreApplication::translate("WindowSystem", "显示侧边栏"), "视图", QKeySequence("Ctrl+Alt+B"),
        [this](const CommandContext& c) {
            return setSideBarsVisible(c.window, !sideBarsVisible(c.window));
        }, hasWindow);
    add("window.toolbars.toggle", QCoreApplication::translate("WindowSystem", "显示工具栏"), "视图", QKeySequence("Ctrl+Alt+H"),
        [this](const CommandContext& c) { return setToolbarsVisible(c.window, !toolbarsVisible(c.window)); }, hasWindow);
    add("window.statusBar.toggle", QCoreApplication::translate("WindowSystem", "显示状态栏"), "视图", QKeySequence("Ctrl+Alt+S"),
        [this](const CommandContext& c) { return setStatusBarVisible(c.window, !statusBarVisible(c.window)); }, hasWindow);
    add("window.statusItem.setVisible", QCoreApplication::translate("WindowSystem", "切换状态栏控件"), "视图", {},
        [this](const CommandContext& c) {
            const auto id = c.args.value("item").toString();
            if (!statusItemIds().contains(id)) return false;
            if (c.args.contains("visible") && c.args.value("visible").metaType().id() != QMetaType::Bool) return false;
            return setStatusItemVisible(c.window, id,
                c.args.value("visible", !statusItemVisible(c.window, id)).toBool());
        }, hasWindow);
    for (const auto mode : {ViewMode::WindowFullscreen, ViewMode::GroupFullscreen, ViewMode::PaneFullscreen}) {
        const bool group = mode == ViewMode::GroupFullscreen;
        const bool pane = mode == ViewMode::PaneFullscreen;
        const auto id = group ? "window.fullscreen.group" : pane ? "window.fullscreen.pane" : "window.fullscreen.toggle";
        const auto title = group ? QCoreApplication::translate("WindowSystem", "标签组全屏") : pane ? QCoreApplication::translate("WindowSystem", "窗格全屏") : QCoreApplication::translate("WindowSystem", "窗口全屏");
        const auto key = group ? "Ctrl+Shift+F11" : pane ? "Shift+F11" : "F11";
        std::function<bool(const CommandContext&)> enabled = hasWindow;
        if (group || pane) enabled = hasTab;
        add(id, title, "视图", QKeySequence(key), [this, mode](const CommandContext& c) {
            return setViewMode(c.window, viewMode(c.window) == mode ? ViewMode::Normal : mode);
        }, std::move(enabled));
    }
    add("window.fullscreen.exit", QCoreApplication::translate("WindowSystem", "退出全屏"), "视图", QKeySequence(Qt::Key_Escape),
        [this](const CommandContext& c) { return setViewMode(c.window, ViewMode::Normal); },
        [this](const CommandContext& c) { return viewMode(c.window) != ViewMode::Normal; });
    add("window.zen.toggle", QCoreApplication::translate("WindowSystem", "禅模式"), "视图", QKeySequence("Ctrl+K, Z"),
        [this](const CommandContext& c) { return setZenMode(c.window, !zenMode(c.window)); },
        [this, hasWindow, hasTab](const CommandContext& c) {
            return hasWindow(c) && (viewMode(c.window) != ViewMode::Normal
                || behavior().zenFullscreenMode == ViewMode::WindowFullscreen || hasTab(c));
        });
    for (const bool top : {true, false}) {
        add(top ? "tab.topBar.setSharing" : "tab.bottomBar.setSharing",
            top ? QCoreApplication::translate("WindowSystem", "切换上方窗格栏共享") : QCoreApplication::translate("WindowSystem", "切换下方窗格栏共享"), "布局", {},
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
    add("workbench.showCommandPalette", QCoreApplication::translate("WindowSystem", "显示命令面板"), "视图", QKeySequence("Ctrl+Shift+P"),
        [this](const CommandContext& c) { showCommandPalette(c.window); return true; });
    add("window.select", QCoreApplication::translate("WindowSystem", "选择窗口…"), "窗口", QKeySequence("Ctrl+Alt+W"),
        [this](const CommandContext& c) { showWindowSelector(c.window); return true; });
    add("window.activate", QCoreApplication::translate("WindowSystem", "激活窗口"), "窗口", {},
        [this](const CommandContext& c) { return activateWindow(c.args.value("targetWindow").toUuid()); },
        [this](const CommandContext& c) { return d->model.window(c.args.value("targetWindow").toUuid()) != nullptr; });
    add("window.settings", QCoreApplication::translate("WindowSystem", "设置"), "工具", QKeySequence("Ctrl+,"),
        [this](const CommandContext& c) { showSettings(c.window); return true; });
    add("preferences.openKeyboardShortcuts", QCoreApplication::translate("WindowSystem", "键盘快捷方式"), "工具", QKeySequence("Ctrl+K, Ctrl+S"),
        [this](const CommandContext& c) { openShortcutEditor(this, windowWidget(c.window)); return true; });
    registerToolbarMoveCommand(this);
    registerPeekSizeCommands(this);
    registerTabGroupCommands();
    registerHierarchyCommands();
    registerCustomChromeCommands();
    add("window.toolbar.setSizing", QCoreApplication::translate("WindowSystem", "切换工具条弹性空间…"), "布局", {},
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
    add("window.toolbar.resetSize", QCoreApplication::translate("WindowSystem", "重置工具条大小…"), "布局", {},
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
    add("workspace.closeTab", QCoreApplication::translate("WindowSystem", "关闭当前标签"), "文件", QKeySequence("Ctrl+W"),
        [this](const CommandContext& c) { return closeTab(c.tab); }, hasTab);
    add("window.close", QCoreApplication::translate("WindowSystem", "关闭当前窗口"), "窗口", QKeySequence("Alt+F4"),
        [this](const CommandContext& c) { return requestCloseWindow(c.window); }, hasWindow);
    const auto setDisplayState = [this](const CommandContext& c, const QString& state) {
        const QPointer<WindowManager> owner(this);
        const QPointer<WorkspaceWindow> window(d->windows.value(c.window));
        if (!window) return false;
        if (state != "minimized" && viewMode(c.window) != ViewMode::Normal) {
            if (!setViewMode(c.window, ViewMode::Normal)) return false;
            if (!owner || !window || !owner->model().window(c.window)) return false;
        }
        if (state == "minimized") window->showMinimized();
        else if (state == "maximized") window->showMaximized();
        else window->showNormal();
        return owner && window && owner->model().window(c.window);
    };
    add("window.minimize", QCoreApplication::translate("WindowSystem", "最小化窗口"), "窗口", {},
        [setDisplayState](const CommandContext& c) { return setDisplayState(c, "minimized"); }, hasWindow);
    add("window.maximize", QCoreApplication::translate("WindowSystem", "最大化窗口"), "窗口", {},
        [setDisplayState](const CommandContext& c) { return setDisplayState(c, "maximized"); }, hasWindow);
    add("window.restore", QCoreApplication::translate("WindowSystem", "还原窗口"), "窗口", {},
        [setDisplayState](const CommandContext& c) { return setDisplayState(c, "normal"); }, hasWindow);
    add("window.new", QCoreApplication::translate("WindowSystem", "新建完整工作窗口"), "文件", QKeySequence("Ctrl+Shift+N"),
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
    add("workspace.detachFloating", QCoreApplication::translate("WindowSystem", "当前标签移到轻量浮窗"), "窗口", QKeySequence("Ctrl+Shift+D"),
        [this](const CommandContext& c) { return !detachTab(c.tab, WindowKind::Floating).isNull(); }, hasTab);
    add("workspace.detachWorkbench", QCoreApplication::translate("WindowSystem", "当前标签移到完整工作窗口"), "窗口", {},
        [this](const CommandContext& c) { return !detachTab(c.tab, WindowKind::Workbench).isNull(); }, hasTab);
    add("window.promote", QCoreApplication::translate("WindowSystem", "浮窗转换为完整工作窗口"), "窗口", {},
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
    add("window.demote", QCoreApplication::translate("WindowSystem", "转换为另一窗口的轻量浮窗…"), "窗口", {},
        [this, chooseWorkbench](const CommandContext& c) { const auto target = chooseWorkbench(c); return !target.isNull() && demoteWindow(c.window, target); },
        [this](const CommandContext& c) { const auto* w = d->model.window(c.window); return w && w->kind == WindowKind::Workbench && d->model.windowOrder().size() > 1; });
    add("window.reassignFamily", QCoreApplication::translate("WindowSystem", "浮窗更改所属工作窗口…"), "窗口", {},
        [this, chooseWorkbench](const CommandContext& c) { const auto target = chooseWorkbench(c); return !target.isNull() && reassignFamily(c.window, target); },
        [this](const CommandContext& c) { const auto* w = d->model.window(c.window); return w && w->kind == WindowKind::Floating; });
    add("window.focusOwner", QCoreApplication::translate("WindowSystem", "返回所属完整工作窗口"), "窗口", QKeySequence("Ctrl+Alt+Home"),
        [this](const CommandContext& c) { const auto* w = d->model.window(c.window); if (!w) return false; const auto root = d->model.familyRoot(w->familyId); showWindow(root); activateTab(context(root).tab); return true; }, hasWindow);
    const QVector<QPair<QString, QString>> scopes = {{"family", QCoreApplication::translate("WindowSystem", "所属窗口族")}, {"all", QCoreApplication::translate("WindowSystem", "全部窗口")}, {"workbenches", QCoreApplication::translate("WindowSystem", "完整窗口")}};
    for (const auto& scope : scopes) for (const bool forward : {true, false}) {
        QString key;
        if (scope.first == "family") key = forward ? "Ctrl+Alt+PgDown" : "Ctrl+Alt+PgUp";
        if (scope.first == "all") key = forward ? "Ctrl+Alt+Shift+PgDown" : "Ctrl+Alt+Shift+PgUp";
        const auto id = QStringLiteral("window.focus%1.%2").arg(forward ? "Next" : "Previous", scope.first);
        add(id, QCoreApplication::translate("WindowSystem", "切换%1：%2").arg(scope.second, forward ? QCoreApplication::translate("WindowSystem", "下一个") : QCoreApplication::translate("WindowSystem", "上一个")), "窗口", QKeySequence(key),
            [this, forward, scope](const CommandContext&) { focusWindowCycle(forward, scope.first); return true; });
    }
    add("window.redock", QCoreApplication::translate("WindowSystem", "浮窗整体停靠回所属窗口"), "窗口", {},
        [this](const CommandContext& c) {
            const auto* w = d->model.window(c.window); if (!w) return false;
            const auto root = d->model.familyRoot(w->familyId);
            const auto result = d->model.moveAllTabsToWindow(c.window, root, Placement::Right);
            if (!applyResult(result)) return false;
            showWindow(root);
            return true;
        }, [this](const CommandContext& c) { const auto* w = d->model.window(c.window); return w && w->kind == WindowKind::Floating; });
    add("window.moveAllTabs", QCoreApplication::translate("WindowSystem", "移动本窗口全部标签…"), "窗口", {},
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
    add("workspace.moveTabToWindow", QCoreApplication::translate("WindowSystem", "移动当前标签到另一个窗口…"), "窗口", {},
        [this, chooseWindow](const CommandContext& c) {
            const auto tab = c.args.value("tab", c.tab).toUuid();
            const auto* source = d->model.group(d->model.groupForTab(tab));
            if (!source) return false;
            const auto target = chooseWindow(source->windowId, c);
            if (target.isNull()) return false;
            const auto place = static_cast<Placement>(c.args.value("placement", int(Placement::Center)).toInt());
            return moveTabToWindow(tab, target, place);
        }, hasTab);
    add("pane.moveToWindow", QCoreApplication::translate("WindowSystem", "将当前窗格并入指定窗口的活动标签…"), "窗口", {},
        [this, chooseWindow](const CommandContext& c) {
            const auto tab = c.args.value("tab", c.tab).toUuid();
            const auto pane = c.args.value("pane", c.pane).toUuid();
            const auto* source = d->model.group(d->model.groupForTab(tab));
            if (!source) return false;
            const auto target = chooseWindow(source->windowId, c);
            return !target.isNull() && movePaneToWindow(tab, pane, target,
                static_cast<Placement>(c.args.value("placement", int(Placement::Right)).toInt()));
        }, hasPane);
    add("pane.moveToTab", QCoreApplication::translate("WindowSystem", "将当前窗格并入指定标签…"), "布局", {},
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
        const auto label = forward ? QCoreApplication::translate("WindowSystem", "下一个") : QCoreApplication::translate("WindowSystem", "上一个");
        add("workspace.moveTabToWindow." + suffix, QCoreApplication::translate("WindowSystem", "将当前标签移到%1窗口").arg(label), "窗口", {},
            [this, forward](const CommandContext& c) { return moveTabToWindowCycle(c.tab, forward); },
            [this](const CommandContext& c) { return d->model.tab(c.tab) && d->model.windowOrder().size() > 1; });
        add("pane.moveToWindow." + suffix, QCoreApplication::translate("WindowSystem", "将当前窗格并入%1窗口的活动标签").arg(label), "窗口", {},
            [this, forward](const CommandContext& c) { return movePaneToWindowCycle(c.tab, c.pane, forward); },
            [this, hasPane](const CommandContext& c) { return hasPane(c) && d->model.windowOrder().size() > 1; });
        add("pane.moveToTab." + suffix, QCoreApplication::translate("WindowSystem", "将当前窗格并入本组%1标签").arg(label), "布局",
            QKeySequence(forward ? "Alt+PgDown" : "Alt+PgUp"),
            [this, forward](const CommandContext& c) { return movePaneToAdjacentTab(c.tab, c.pane, forward); },
            [this, hasPane](const CommandContext& c) { const auto* group = d->model.group(c.group); return hasPane(c) && group && group->tabs.size() > 1; });
    }
    add("workspace.moveTabToEdge", QCoreApplication::translate("WindowSystem", "当前标签收纳到边栏"), "布局", {},
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
    add("workspace.moveTabToEdgeBar", QCoreApplication::translate("WindowSystem", "将当前标签插入边栏"), "布局", {},
        [this](const CommandContext& c) {
            const auto* group = d->model.group(d->model.groupForTab(c.args.value("tab", c.tab).toUuid()));
            const auto target = c.args.value("targetWindow", c.window).toUuid();
            const auto edge = static_cast<Edge>(c.args.value("edge", group ? int(group->edge) : int(Edge::Left)).toInt());
            return moveTabToEdgeBar(c.args.value("tab", c.tab).toUuid(), target, edge, c.args.value("index", -1).toInt());
        }, hasTab);
    add("window.moveAllToEdgeBar", QCoreApplication::translate("WindowSystem", "将整窗标签插入边栏"), "布局", {},
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
            next ? QCoreApplication::translate("WindowSystem", "将当前标签后移一位") : QCoreApplication::translate("WindowSystem", "将当前标签前移一位"), "布局",
            QKeySequence(next ? "Ctrl+Shift+PgDown" : "Ctrl+Shift+PgUp"),
            [this, next](const CommandContext& c) { return reorderTab(c.tab, next); }, canReorder);
    }
    add("window.moveAllToEdge", QCoreApplication::translate("WindowSystem", "整窗标签收纳到目标边栏"), "布局", {},
        [this](const CommandContext& c) {
            const auto source = c.args.value("source", c.window).toUuid();
            const auto target = c.args.value("targetWindow").toUuid(); if (target.isNull()) return false;
            return moveAllTabsToEdge(source, target, static_cast<Edge>(c.args.value("edge", 0).toInt()), c.args.value("pinned").toBool());
        }, hasWindow);
    const QVector<QPair<QString, Placement>> directions = {{"left", Placement::Left}, {"right", Placement::Right}, {"up", Placement::Top}, {"down", Placement::Bottom}};
    for (const auto& direction : directions) {
        const auto keyName = direction.first == "up" ? "Up" : direction.first == "down" ? "Down" : direction.first == "left" ? "Left" : "Right";
        const auto label = direction.first == "up" ? QCoreApplication::translate("WindowSystem", "上") : direction.first == "down" ? QCoreApplication::translate("WindowSystem", "下") : direction.first == "left" ? QCoreApplication::translate("WindowSystem", "左") : QCoreApplication::translate("WindowSystem", "右");
        add("workspace.focusGroup." + direction.first, QCoreApplication::translate("WindowSystem", "聚焦%1侧标签组").arg(label), "布局", QKeySequence("Ctrl+Alt+" + QString(keyName)),
            [this, direction](const CommandContext& c) { return navigate(direction.second, false, c); });
        add("pane.focus." + direction.first, QCoreApplication::translate("WindowSystem", "聚焦%1侧窗格").arg(label), "布局", QKeySequence("Alt+" + QString(keyName)),
            [this, direction](const CommandContext& c) { return navigate(direction.second, true, c); });
        add("workspace.moveTabToGroup." + direction.first, QCoreApplication::translate("WindowSystem", "将当前标签移到%1侧标签组").arg(label), "布局", {},
            [this, direction](const CommandContext& c) { return moveTabToAdjacentGroup(c.tab, direction.second); },
            [this, direction](const CommandContext& c) { return !adjacentGroup(c.tab, direction.second).isNull(); });
        add("pane.moveToGroup." + direction.first, QCoreApplication::translate("WindowSystem", "将当前窗格并入%1侧标签组的活动标签").arg(label), "布局", {},
            [this, direction](const CommandContext& c) { return movePaneToAdjacentGroup(c.tab, c.pane, direction.second); },
            [this, direction, hasPane](const CommandContext& c) { return hasPane(c) && !adjacentGroup(c.tab, direction.second).isNull(); });
        add("pane.swap." + direction.first, QCoreApplication::translate("WindowSystem", "与%1侧窗格交换位置").arg(label), "布局", {},
            [this, direction](const CommandContext& c) { return swapPaneDirection(c.tab, c.pane, direction.second); },
            [this, direction](const CommandContext& c) { return !adjacentPane(c.tab, c.pane, direction.second).isNull(); });
        add("workspace.resizeGroup." + direction.first, QCoreApplication::translate("WindowSystem", "向%1调整标签组边界").arg(label), "布局", QKeySequence("Ctrl+Alt+Shift+" + QString(keyName)),
            [this, direction](const CommandContext& c) { resizeActive(direction.second, false, c); return true; });
        add("pane.resize." + direction.first, QCoreApplication::translate("WindowSystem", "向%1调整窗格边界").arg(label), "布局", QKeySequence("Alt+Shift+" + QString(keyName)),
            [this, direction](const CommandContext& c) { resizeActive(direction.second, true, c); return true; });
        add("workspace.splitGroup." + direction.first, QCoreApplication::translate("WindowSystem", "新建%1侧标签组").arg(label), "布局", {},
            [this, direction](const CommandContext& c) {
                if (!d->model.tab(c.tab)) return false;
                const auto* target = d->model.group(c.group);
                if (!target || target->location == GroupLocation::AutoHide) return false;
                auto options = d->descriptors.value(c.pane); options.title += QCoreApplication::translate("WindowSystem", " · 新组");
                const QPointer<WindowManager> self(this);
                auto* created = createContent(options, {});
                // A factory that destroys its manager may return a child that
                // was already deleted with it. Do not inspect that raw result.
                if (!self) return false;
                const QPointer<QWidget> content(created); if (!content) return false;
                const auto tab = addTabToNewGroup(c.window, content.data(), options, c.group, direction.second);
                if (tab.isNull()) { delete content.data(); return false; }
                return !self.isNull();
            }, [this](const CommandContext& c) { const auto* group = d->model.group(c.group); return d->model.tab(c.tab) && group && group->location != GroupLocation::AutoHide; });
        add("pane.split." + direction.first, QCoreApplication::translate("WindowSystem", "向%1拆分窗格").arg(label), "布局",
            direction.second == Placement::Right ? QKeySequence("Ctrl+Shift+E") : direction.second == Placement::Bottom ? QKeySequence("Ctrl+Shift+O") : QKeySequence{},
            [this, direction](const CommandContext& c) {
                const QPointer<WindowManager> self(this);
                auto* created = createContent(d->descriptors.value(c.pane), {});
                if (!self) return false;
                const QPointer<QWidget> content(created); if (!content) return false;
                const auto pane = splitPane(c.tab, c.pane, content.data(), direction.second);
                if (pane.isNull()) { delete content.data(); return false; } return !self.isNull();
            }, hasTab);
        const auto edge = direction.second == Placement::Left ? Edge::Left : direction.second == Placement::Right ? Edge::Right : direction.second == Placement::Top ? Edge::Top : Edge::Bottom;
        add("workspace.hideToEdge." + direction.first, QCoreApplication::translate("WindowSystem", "当前标签收纳到%1边栏").arg(label), "布局", {},
            [this, edge](const CommandContext& c) { return moveTabToEdge(c.tab, edge); }, hasTab);
    }
    for (const bool next : {true, false}) add(next ? "workspace.nextTab" : "workspace.previousTab",
        next ? QCoreApplication::translate("WindowSystem", "当前组的下一个标签") : QCoreApplication::translate("WindowSystem", "当前组的上一个标签"), "视图",
        QKeySequence(next ? "Ctrl+PgDown" : "Ctrl+PgUp"), [this, next](const CommandContext& c) {
            const auto* group = d->model.group(c.group); if (!group || group->tabs.isEmpty()) return false;
            const auto index = (group->tabs.indexOf(c.tab) + (next ? 1 : group->tabs.size() - 1)) % group->tabs.size();
            return activateTab(group->tabs[index]);
        }, hasTab);
    add("workspace.pinGroup", QCoreApplication::translate("WindowSystem", "固定边栏标签组"), "布局", {},
        [this](const CommandContext& c) { return pinGroup(c.group, true); },
        [this](const CommandContext& c) { const auto* g = d->model.group(c.group); return g && g->location == GroupLocation::AutoHide; });
    add("workspace.unpinGroup", QCoreApplication::translate("WindowSystem", "取消固定并收纳到边栏"), "布局", {},
        [this](const CommandContext& c) { return pinGroup(c.group, false); },
        [this](const CommandContext& c) { const auto* g = d->model.group(c.group); return g && g->location == GroupLocation::Pinned; });
    add("workspace.pinTab", QCoreApplication::translate("WindowSystem", "固定当前边栏标签"), "布局", {},
        [this](const CommandContext& c) { return pinTab(c.tab, true); },
        [this](const CommandContext& c) { const auto* g = d->model.group(d->model.groupForTab(c.tab)); return g && g->location == GroupLocation::AutoHide; });
    add("workspace.unpinTab", QCoreApplication::translate("WindowSystem", "取消固定当前标签"), "布局", {},
        [this](const CommandContext& c) { return pinTab(c.tab, false); },
        [this](const CommandContext& c) { const auto* g = d->model.group(d->model.groupForTab(c.tab)); return g && g->location == GroupLocation::Pinned; });
    for (const bool pinned : {true, false}) add(pinned ? "workspace.pinEdge" : "workspace.unpinEdge",
        pinned ? QCoreApplication::translate("WindowSystem", "固定该侧全部标签") : QCoreApplication::translate("WindowSystem", "取消固定该侧全部标签"), "布局", {},
        [this, pinned](const CommandContext& c) {
            const auto* group = d->model.group(c.group);
            const auto edge = static_cast<Edge>(c.args.value("edge", group ? int(group->edge) : int(Edge::Left)).toInt());
            return pinEdge(c.window, edge, pinned);
        }, hasWindow);
    for (const auto& scope : {QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("others"), QStringLiteral("all")}) {
        const auto title = scope == "left" ? QCoreApplication::translate("WindowSystem", "关闭左侧标签") : scope == "right" ? QCoreApplication::translate("WindowSystem", "关闭右侧标签")
            : scope == "others" ? QCoreApplication::translate("WindowSystem", "关闭其他标签") : QCoreApplication::translate("WindowSystem", "关闭该组全部标签");
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
    add("workspace.restoreGroupToCenter", QCoreApplication::translate("WindowSystem", "边栏标签组移回中央"), "布局", {},
        [this](const CommandContext& c) { return moveGroupToCenter(c.group); }, hasTab);
    add("pane.extractFloating", QCoreApplication::translate("WindowSystem", "当前窗格移到轻量浮窗"), "窗口", {},
        [this](const CommandContext& c) { return !extractPane(c.tab, c.pane, WindowKind::Floating).isNull(); }, hasTab);
    add("pane.extractWorkbench", QCoreApplication::translate("WindowSystem", "当前窗格移到完整窗口"), "窗口", {},
        [this](const CommandContext& c) { return !extractPane(c.tab, c.pane, WindowKind::Workbench).isNull(); }, hasTab);
    add("pane.close", QCoreApplication::translate("WindowSystem", "关闭当前窗格"), "布局", QKeySequence("Ctrl+Shift+W"),
        [this](const CommandContext& c) { return closePane(c.tab, c.pane); }, hasTab);
    add("window.toggleStripsLock", QCoreApplication::translate("WindowSystem", "锁定／解锁菜单与工具条"), "工具", {},
        [this](const CommandContext& c) {
            const auto* window = d->windows.value(c.window).data();
            if (!window || !window->strips()) return false;
            if (c.args.contains("locked") && c.args.value("locked").metaType().id() != QMetaType::Bool) return false;
            return setToolbarsLocked(c.window, c.args.value("locked", !window->strips()->isLocked()).toBool());
        },
        [this](const CommandContext& c) { const auto* window = d->windows.value(c.window).data(); return window && window->strips(); });
    add("window.drag.selectTarget", QCoreApplication::translate("WindowSystem", "拖拽中选择目标窗口"), "窗口", {},
        [this](const CommandContext&) { d->drag->selectTarget(); return true; }, [this](const CommandContext&) { return d->drag->isActive(); });
    add("window.drag.cancel", QCoreApplication::translate("WindowSystem", "取消当前拖拽"), "窗口", {},
        [this](const CommandContext&) { d->drag->cancel(); return true; }, [this](const CommandContext&) { return d->drag->isActive(); });
    add("layout.setRatio", QCoreApplication::translate("WindowSystem", "设置分割比例"), "布局", {},
        [this](const CommandContext& c) {
            const auto node = c.args.value("node").toUuid();
            const auto ratio = c.args.value("ratio").toDouble();
            if (!d->model.setSplitRatio(node, ratio)) return false;
            d->drag->releaseForMutation();
            d->navigationAnchor.reset();
            // The model names the tree that holds the node; only that tab view
            // or window surface can have built its splitter.
            QWidget* owner = nullptr;
            QSplitter* splitter = nullptr;
            if (const auto tree = d->model.ownerOfNode(node)) {
                if (tree->kind == LayoutModel::NodeOwner::Kind::Tab) {
                    if (auto* view = d->tabs.value(tree->owner).data()) { owner = view; splitter = view->splitterFor(node); }
                } else if (auto* window = d->windows.value(tree->owner).data()) {
                    owner = window->surface(); splitter = window->surface()->splitterFor(node);
                }
            }
            if (splitter && splitter->isVisibleTo(owner)) {
                const auto sizes = splitter->sizes();
                const int available = qMax(1000, sizes.value(0) + sizes.value(1));
                const QSignalBlocker suppressFeedback(splitter);
                splitter->setSizes({qRound(available * ratio), qRound(available * (1. - ratio))});
            }
            emit layoutChanged();
            return true;
        });
    add("layout.setEdgeExtent", QCoreApplication::translate("WindowSystem", "设置固定边栏分区大小"), "布局", {},
        [this](const CommandContext& c) { const auto result = d->model.setEdgeGroupExtent(c.args.value("group").toUuid(), c.args.value("extent").toInt()); if (result) { d->drag->releaseForMutation(); emit layoutChanged(); } return result.ok; });
}
void WindowManager::showTabMenu(TabId tab, QPoint global)
{
    if (!d->model.tab(tab)) return;
    const auto group = d->model.groupForTab(tab); const auto window = d->model.group(group)->windowId;
    auto captured = context(window);
    // A context menu targets the clicked label without navigating away from
    // the content the user is currently reading (including an open peek).
    captured.group = group; captured.tab = tab; captured.pane = d->model.tab(tab)->activePane;
    const QPointer<WindowManager> self(this);
    DismissOnlyMenu menu(windowWidget(window)); applyTheme(&menu, theme());
    appendTabPresentationActions(&menu, this, captured);
    populateContextMenu(&menu, CommandMenuSurface::Tab, this, captured);
    menu.exec(global);
}
void WindowManager::showWindowMenu(WindowId window, QPoint global)
{
    const auto* record = d->model.window(window); if (!record) return;
    const auto captured = context(window);
    DismissOnlyMenu menu(windowWidget(window)); applyTheme(&menu, theme());
    populateContextMenu(&menu, CommandMenuSurface::Window, this, captured);
    menu.exec(global);
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
    menu->exec(global);
    delete retained.data();
}
} // namespace window_system
