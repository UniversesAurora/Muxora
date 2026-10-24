#include <QCoreApplication>
#include "window_manager_p.hpp"
#include "style/tab_appearance_p.hpp"
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
    add("window.tabBars.toggle", QCoreApplication::translate("WindowSystem", "Show tab bars"), "视图", QKeySequence("Ctrl+Alt+T"),
        [this](const CommandContext& c) {
            return setTabBarsVisible(c.window, !tabBarsVisible(c.window));
        }, hasWindow);
    add("window.sideBars.toggle", QCoreApplication::translate("WindowSystem", "Show sidebars"), "视图", QKeySequence("Ctrl+Alt+B"),
        [this](const CommandContext& c) {
            return setSideBarsVisible(c.window, !sideBarsVisible(c.window));
        }, hasWindow);
    add("window.toolbars.toggle", QCoreApplication::translate("WindowSystem", "Show toolbars"), "视图", QKeySequence("Ctrl+Alt+H"),
        [this](const CommandContext& c) { return setToolbarsVisible(c.window, !toolbarsVisible(c.window)); }, hasWindow);
    add("window.statusBar.toggle", QCoreApplication::translate("WindowSystem", "Show status bar"), "视图", QKeySequence("Ctrl+Alt+S"),
        [this](const CommandContext& c) { return setStatusBarVisible(c.window, !statusBarVisible(c.window)); }, hasWindow);
    add("window.statusItem.setVisible", QCoreApplication::translate("WindowSystem", "Toggle status bar item"), "视图", {},
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
        const auto title = group ? QCoreApplication::translate("WindowSystem", "Tab group fullscreen") : pane ? QCoreApplication::translate("WindowSystem", "Pane fullscreen") : QCoreApplication::translate("WindowSystem", "Window fullscreen");
        const auto key = group ? "Ctrl+Shift+F11" : pane ? "Shift+F11" : "F11";
        std::function<bool(const CommandContext&)> enabled = hasWindow;
        if (group || pane) enabled = hasTab;
        add(id, title, "视图", QKeySequence(key), [this, mode](const CommandContext& c) {
            return setViewMode(c.window, viewMode(c.window) == mode ? ViewMode::Normal : mode);
        }, std::move(enabled));
    }
    add("window.fullscreen.exit", QCoreApplication::translate("WindowSystem", "Exit fullscreen"), "视图", QKeySequence(Qt::Key_Escape),
        [this](const CommandContext& c) { return setViewMode(c.window, ViewMode::Normal); },
        [this](const CommandContext& c) { return viewMode(c.window) != ViewMode::Normal; });
    add("window.zen.toggle", QCoreApplication::translate("WindowSystem", "Zen mode"), "视图", QKeySequence("Ctrl+K, Z"),
        [this](const CommandContext& c) { return setZenMode(c.window, !zenMode(c.window)); },
        [this, hasWindow, hasTab](const CommandContext& c) {
            return hasWindow(c) && (viewMode(c.window) != ViewMode::Normal
                || behavior().zenFullscreenMode == ViewMode::WindowFullscreen || hasTab(c));
        });
    for (const bool top : {true, false}) {
        add(top ? "tab.topBar.setSharing" : "tab.bottomBar.setSharing",
            top ? QCoreApplication::translate("WindowSystem", "Toggle shared top pane bar") : QCoreApplication::translate("WindowSystem", "Toggle shared bottom pane bar"), "布局", {},
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
    add("workbench.showCommandPalette", QCoreApplication::translate("WindowSystem", "Show command palette"), "视图", QKeySequence("Ctrl+Shift+P"),
        [this](const CommandContext& c) { showCommandPalette(c.window); return true; });
    add("window.select", QCoreApplication::translate("WindowSystem", "Select window…"), "窗口", QKeySequence("Ctrl+Alt+W"),
        [this](const CommandContext& c) { showWindowSelector(c.window); return true; });
    add("window.activate", QCoreApplication::translate("WindowSystem", "Activate window"), "窗口", {},
        [this](const CommandContext& c) { return activateWindow(c.args.value("targetWindow").toUuid()); },
        [this](const CommandContext& c) { return d->model.window(c.args.value("targetWindow").toUuid()) != nullptr; });
    add("window.settings", QCoreApplication::translate("WindowSystem", "Settings"), "工具", QKeySequence("Ctrl+,"),
        [this](const CommandContext& c) { showSettings(c.window); return true; });
    add("preferences.openKeyboardShortcuts", QCoreApplication::translate("WindowSystem", "Keyboard shortcut settings"), "工具", QKeySequence("Ctrl+K, Ctrl+S"),
        [this](const CommandContext& c) { openShortcutEditor(this, windowWidget(c.window)); return true; });
    registerToolbarMoveCommand(this);
    registerPeekSizeCommands(this);
    registerTabGroupCommands();
    registerHierarchyCommands();
    registerCustomChromeCommands();
    add("window.toolbar.setSizing", QCoreApplication::translate("WindowSystem", "Change toolbar flexible space…"), "布局", {},
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
    add("window.toolbar.resetSize", QCoreApplication::translate("WindowSystem", "Reset toolbar size…"), "布局", {},
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
            // Closing the window can dismiss the picker before the widget is
            // deleted from the event loop; the model already knows it closed.
            if (!window || !d->model.window(c.window)) return false;
            return !chosen || resetToolbarWidth(c.window, chosen->data().toString());
        }, [this](const CommandContext& c) {
            const auto window = d->windows.value(c.window);
            return window && window->strips();
        });
    // VS Code's preventPinnedEditorClose: the close shortcut leaves a sticky
    // tab open and shows the first tab that is not sticky instead, in its
    // group first. Menus, the palette and the tab's own controls still close.
    const auto shortcutKeepsSticky = [this](TabId tab) {
        if (!d->dispatchingShortcut || !d->model.isTabSticky(tab) || !guardsShortcut(d->appearance.preventStickyClose)) return false;
        const auto groupId = d->model.groupForTab(tab);
        const auto* group = d->model.group(groupId);
        QVector<TabId> candidates = group ? group->tabs.mid(group->stickyTabs) : QVector<TabId>{};
        if (group)
            for (const auto other : d->model.orderedTabs(group->windowId)) {
                const auto* owner = d->model.group(d->model.groupForTab(other));
                if (owner && owner->id != groupId && owner->location != GroupLocation::AutoHide && !d->model.isTabSticky(other))
                    candidates << other;
            }
        if (!candidates.isEmpty()) activateTab(candidates.first());
        return true;
    };
    add("workspace.closeTab", QCoreApplication::translate("WindowSystem", "Close current tab"), "文件", QKeySequence("Ctrl+W"),
        [this, shortcutKeepsSticky](const CommandContext& c) { return shortcutKeepsSticky(c.tab) || closeTab(c.tab); }, hasTab);
    add("window.close", QCoreApplication::translate("WindowSystem", "Close current window"), "窗口", QKeySequence("Alt+F4"),
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
    add("window.minimize", QCoreApplication::translate("WindowSystem", "Minimize window"), "窗口", {},
        [setDisplayState](const CommandContext& c) { return setDisplayState(c, "minimized"); }, hasWindow);
    add("window.maximize", QCoreApplication::translate("WindowSystem", "Maximize window"), "窗口", {},
        [setDisplayState](const CommandContext& c) { return setDisplayState(c, "maximized"); }, hasWindow);
    add("window.restore", QCoreApplication::translate("WindowSystem", "Restore window"), "窗口", {},
        [setDisplayState](const CommandContext& c) { return setDisplayState(c, "normal"); }, hasWindow);
    add("window.new", QCoreApplication::translate("WindowSystem", "New workbench window"), "文件", QKeySequence("Ctrl+Shift+N"),
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
    add("workspace.detachFloating", QCoreApplication::translate("WindowSystem", "Move current tab to a floating window"), "窗口", QKeySequence("Ctrl+Shift+D"),
        [this](const CommandContext& c) { return !detachTab(c.tab, WindowKind::Floating).isNull(); }, hasTab);
    add("workspace.detachWorkbench", QCoreApplication::translate("WindowSystem", "Move current tab to a workbench window"), "窗口", {},
        [this](const CommandContext& c) { return !detachTab(c.tab, WindowKind::Workbench).isNull(); }, hasTab);
    add("window.promote", QCoreApplication::translate("WindowSystem", "Convert floating window to workbench window"), "窗口", {},
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
    add("window.demote", QCoreApplication::translate("WindowSystem", "Convert to another window's floating window…"), "窗口", {},
        [this, chooseWorkbench](const CommandContext& c) { const auto target = chooseWorkbench(c); return !target.isNull() && demoteWindow(c.window, target); },
        [this](const CommandContext& c) { const auto* w = d->model.window(c.window); return w && w->kind == WindowKind::Workbench && d->model.windowOrder().size() > 1; });
    add("window.reassignFamily", QCoreApplication::translate("WindowSystem", "Change the floating window's owner…"), "窗口", {},
        [this, chooseWorkbench](const CommandContext& c) { const auto target = chooseWorkbench(c); return !target.isNull() && reassignFamily(c.window, target); },
        [this](const CommandContext& c) { const auto* w = d->model.window(c.window); return w && w->kind == WindowKind::Floating; });
    add("window.focusOwner", QCoreApplication::translate("WindowSystem", "Return to owner workbench"), "窗口", QKeySequence("Ctrl+Alt+Home"),
        [this](const CommandContext& c) { const auto* w = d->model.window(c.window); if (!w) return false; const auto root = d->model.familyRoot(w->familyId); showWindow(root); activateTab(context(root).tab); return true; }, hasWindow);
    const QVector<QPair<QString, QString>> scopes = {{"family", QCoreApplication::translate("WindowSystem", "Window family")}, {"all", QCoreApplication::translate("WindowSystem", "All windows")}, {"workbenches", QCoreApplication::translate("WindowSystem", "Workbench windows")}};
    for (const auto& scope : scopes) for (const bool forward : {true, false}) {
        QString key;
        if (scope.first == "family") key = forward ? "Ctrl+Alt+PgDown" : "Ctrl+Alt+PgUp";
        if (scope.first == "all") key = forward ? "Ctrl+Alt+Shift+PgDown" : "Ctrl+Alt+Shift+PgUp";
        const auto id = QStringLiteral("window.focus%1.%2").arg(forward ? "Next" : "Previous", scope.first);
        add(id, QCoreApplication::translate("WindowSystem", "Switch %1: %2").arg(scope.second, forward ? QCoreApplication::translate("WindowSystem", "next") : QCoreApplication::translate("WindowSystem", "previous")), "窗口", QKeySequence(key),
            [this, forward, scope](const CommandContext&) { focusWindowCycle(forward, scope.first); return true; });
    }
    add("window.redock", QCoreApplication::translate("WindowSystem", "Dock the floating window back into its owner"), "窗口", {},
        [this](const CommandContext& c) {
            const auto* w = d->model.window(c.window); if (!w) return false;
            const auto root = d->model.familyRoot(w->familyId);
            const auto result = d->model.moveAllTabsToWindow(c.window, root, Placement::Right);
            if (!applyResult(result)) return false;
            showWindow(root);
            return true;
        }, [this](const CommandContext& c) { const auto* w = d->model.window(c.window); return w && w->kind == WindowKind::Floating; });
    add("window.moveAllTabs", QCoreApplication::translate("WindowSystem", "Move all tabs in this window…"), "窗口", {},
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
    add("workspace.moveTabToWindow", QCoreApplication::translate("WindowSystem", "Move current tab to another window…"), "窗口", {},
        [this, chooseWindow](const CommandContext& c) {
            const auto tab = c.args.value("tab", c.tab).toUuid();
            const auto* source = d->model.group(d->model.groupForTab(tab));
            if (!source) return false;
            const auto target = chooseWindow(source->windowId, c);
            if (target.isNull()) return false;
            const auto place = static_cast<Placement>(c.args.value("placement", int(Placement::Center)).toInt());
            return moveTabToWindow(tab, target, place);
        }, hasTab);
    add("pane.moveToWindow", QCoreApplication::translate("WindowSystem", "Merge current pane into active tab of a selected window…"), "窗口", {},
        [this, chooseWindow](const CommandContext& c) {
            const auto tab = c.args.value("tab", c.tab).toUuid();
            const auto pane = c.args.value("pane", c.pane).toUuid();
            const auto* source = d->model.group(d->model.groupForTab(tab));
            if (!source) return false;
            const auto target = chooseWindow(source->windowId, c);
            return !target.isNull() && movePaneToWindow(tab, pane, target,
                static_cast<Placement>(c.args.value("placement", int(Placement::Right)).toInt()));
        }, hasPane);
    add("pane.moveToTab", QCoreApplication::translate("WindowSystem", "Merge current pane into a selected tab…"), "布局", {},
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
        const auto label = forward ? QCoreApplication::translate("WindowSystem", "next") : QCoreApplication::translate("WindowSystem", "previous");
        add("workspace.moveTabToWindow." + suffix, QCoreApplication::translate("WindowSystem", "Move current tab to the %1 window").arg(label), "窗口", {},
            [this, forward](const CommandContext& c) { return moveTabToWindowCycle(c.tab, forward); },
            [this](const CommandContext& c) { return d->model.tab(c.tab) && d->model.windowOrder().size() > 1; });
        add("pane.moveToWindow." + suffix, QCoreApplication::translate("WindowSystem", "Merge current pane into active tab of %1 window").arg(label), "窗口", {},
            [this, forward](const CommandContext& c) { return movePaneToWindowCycle(c.tab, c.pane, forward); },
            [this, hasPane](const CommandContext& c) { return hasPane(c) && d->model.windowOrder().size() > 1; });
        add("pane.moveToTab." + suffix, QCoreApplication::translate("WindowSystem", "Merge current pane into %1 tab in this group").arg(label), "布局",
            QKeySequence(forward ? "Alt+PgDown" : "Alt+PgUp"),
            [this, forward](const CommandContext& c) { return movePaneToAdjacentTab(c.tab, c.pane, forward); },
            [this, hasPane](const CommandContext& c) { const auto* group = d->model.group(c.group); return hasPane(c) && group && group->tabs.size() > 1; });
    }
    add("workspace.moveTabToEdge", QCoreApplication::translate("WindowSystem", "Auto-hide current tab in sidebar"), "布局", {},
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
    add("workspace.moveTabToEdgeBar", QCoreApplication::translate("WindowSystem", "Insert current tab into sidebar"), "布局", {},
        [this](const CommandContext& c) {
            const auto* group = d->model.group(d->model.groupForTab(c.args.value("tab", c.tab).toUuid()));
            const auto target = c.args.value("targetWindow", c.window).toUuid();
            const auto edge = static_cast<Edge>(c.args.value("edge", group ? int(group->edge) : int(Edge::Left)).toInt());
            return moveTabToEdgeBar(c.args.value("tab", c.tab).toUuid(), target, edge, c.args.value("index", -1).toInt());
        }, hasTab);
    add("window.moveAllToEdgeBar", QCoreApplication::translate("WindowSystem", "Insert all window tabs into sidebar"), "布局", {},
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
            next ? QCoreApplication::translate("WindowSystem", "Move current tab one position right") : QCoreApplication::translate("WindowSystem", "Move current tab one position left"), "布局",
            QKeySequence(next ? "Ctrl+Shift+PgDown" : "Ctrl+Shift+PgUp"),
            [this, next](const CommandContext& c) { return reorderTab(c.tab, next); }, canReorder);
    }
    add("window.moveAllToEdge", QCoreApplication::translate("WindowSystem", "Auto-hide all window tabs in target sidebar"), "布局", {},
        [this](const CommandContext& c) {
            const auto source = c.args.value("source", c.window).toUuid();
            const auto target = c.args.value("targetWindow").toUuid(); if (target.isNull()) return false;
            return moveAllTabsToEdge(source, target, static_cast<Edge>(c.args.value("edge", 0).toInt()), c.args.value("pinned").toBool());
        }, hasWindow);
    const QVector<QPair<QString, Placement>> directions = {{"left", Placement::Left}, {"right", Placement::Right}, {"up", Placement::Top}, {"down", Placement::Bottom}};
    for (const auto& direction : directions) {
        const auto keyName = direction.first == "up" ? "Up" : direction.first == "down" ? "Down" : direction.first == "left" ? "Left" : "Right";
        const auto label = direction.first == "up" ? QCoreApplication::translate("WindowSystem", "up") : direction.first == "down" ? QCoreApplication::translate("WindowSystem", "down") : direction.first == "left" ? QCoreApplication::translate("WindowSystem", "left") : QCoreApplication::translate("WindowSystem", "right");
        const auto positionLabel = direction.first == "up" ? QCoreApplication::translate("WindowSystem", "above")
            : direction.first == "down" ? QCoreApplication::translate("WindowSystem", "below")
            : direction.first == "left" ? QCoreApplication::translate("WindowSystem", "to the left")
            : QCoreApplication::translate("WindowSystem", "to the right");
        const auto edgeLabel = direction.first == "up" ? QCoreApplication::translate("WindowSystem", "top")
            : direction.first == "down" ? QCoreApplication::translate("WindowSystem", "bottom") : label;
        add("workspace.focusGroup." + direction.first, QCoreApplication::translate("WindowSystem", "Focus tab group %1").arg(positionLabel), "布局", QKeySequence("Ctrl+Alt+" + QString(keyName)),
            [this, direction](const CommandContext& c) { return navigate(direction.second, false, c); });
        add("pane.focus." + direction.first, QCoreApplication::translate("WindowSystem", "Focus pane %1").arg(positionLabel), "布局", QKeySequence("Alt+" + QString(keyName)),
            [this, direction](const CommandContext& c) { return navigate(direction.second, true, c); });
        add("workspace.moveTabToGroup." + direction.first, QCoreApplication::translate("WindowSystem", "Move current tab to the tab group %1").arg(positionLabel), "布局", {},
            [this, direction](const CommandContext& c) { return moveTabToAdjacentGroup(c.tab, direction.second); },
            [this, direction](const CommandContext& c) { return !adjacentGroup(c.tab, direction.second).isNull(); });
        add("pane.moveToGroup." + direction.first, QCoreApplication::translate("WindowSystem", "Merge current pane into the active tab of the group %1").arg(positionLabel), "布局", {},
            [this, direction](const CommandContext& c) { return movePaneToAdjacentGroup(c.tab, c.pane, direction.second); },
            [this, direction, hasPane](const CommandContext& c) { return hasPane(c) && !adjacentGroup(c.tab, direction.second).isNull(); });
        add("pane.swap." + direction.first, QCoreApplication::translate("WindowSystem", "Swap with the pane %1").arg(positionLabel), "布局", {},
            [this, direction](const CommandContext& c) { return swapPaneDirection(c.tab, c.pane, direction.second); },
            [this, direction](const CommandContext& c) { return !adjacentPane(c.tab, c.pane, direction.second).isNull(); });
        add("workspace.resizeGroup." + direction.first, QCoreApplication::translate("WindowSystem", "Resize tab group %1").arg(label), "布局", QKeySequence("Ctrl+Alt+Shift+" + QString(keyName)),
            [this, direction](const CommandContext& c) { resizeActive(direction.second, false, c); return true; });
        add("pane.resize." + direction.first, QCoreApplication::translate("WindowSystem", "Resize pane %1").arg(label), "布局", QKeySequence("Alt+Shift+" + QString(keyName)),
            [this, direction](const CommandContext& c) { resizeActive(direction.second, true, c); return true; });
        add("workspace.splitGroup." + direction.first, QCoreApplication::translate("WindowSystem", "Create tab group %1").arg(positionLabel), "布局", {},
            [this, direction](const CommandContext& c) {
                if (!d->model.tab(c.tab)) return false;
                const auto* target = d->model.group(c.group);
                if (!target || target->location == GroupLocation::AutoHide) return false;
                auto options = d->descriptors.value(c.pane); options.title += QCoreApplication::translate("WindowSystem", " · New group");
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
        add("pane.split." + direction.first, QCoreApplication::translate("WindowSystem", "Split pane %1").arg(label), "布局",
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
        add("workspace.hideToEdge." + direction.first, QCoreApplication::translate("WindowSystem", "Auto-hide current tab on the %1 sidebar").arg(edgeLabel), "布局", {},
            [this, edge](const CommandContext& c) { return moveTabToEdge(c.tab, edge); }, hasTab);
    }
    for (const bool next : {true, false}) add(next ? "workspace.nextTab" : "workspace.previousTab",
        next ? QCoreApplication::translate("WindowSystem", "Next tab in current group") : QCoreApplication::translate("WindowSystem", "Previous tab in current group"), "视图",
        QKeySequence(next ? "Ctrl+PgDown" : "Ctrl+PgUp"), [this, next](const CommandContext& c) {
            const auto* group = d->model.group(c.group); if (!group || group->tabs.isEmpty()) return false;
            const auto index = (group->tabs.indexOf(c.tab) + (next ? 1 : group->tabs.size() - 1)) % group->tabs.size();
            return activateTab(group->tabs[index]);
        }, hasTab);
    add("workspace.pinGroup", QCoreApplication::translate("WindowSystem", "Pin sidebar tab group"), "布局", {},
        [this](const CommandContext& c) { return pinGroup(c.group, true); },
        [this](const CommandContext& c) { const auto* g = d->model.group(c.group); return g && g->location == GroupLocation::AutoHide; });
    add("workspace.unpinGroup", QCoreApplication::translate("WindowSystem", "Unpin to sidebar"), "布局", {},
        [this](const CommandContext& c) { return pinGroup(c.group, false); },
        [this](const CommandContext& c) { const auto* g = d->model.group(c.group); return g && g->location == GroupLocation::Pinned; });
    add("workspace.pinTab", QCoreApplication::translate("WindowSystem", "Pin current sidebar tab"), "布局", {},
        [this](const CommandContext& c) { return pinTab(c.tab, true); },
        [this](const CommandContext& c) { const auto* g = d->model.group(d->model.groupForTab(c.tab)); return g && g->location == GroupLocation::AutoHide; });
    add("workspace.unpinTab", QCoreApplication::translate("WindowSystem", "Unpin current tab"), "布局", {},
        [this](const CommandContext& c) { return pinTab(c.tab, false); },
        [this](const CommandContext& c) { const auto* g = d->model.group(d->model.groupForTab(c.tab)); return g && g->location == GroupLocation::Pinned; });
    for (const bool pinned : {true, false}) add(pinned ? "workspace.pinEdge" : "workspace.unpinEdge",
        pinned ? QCoreApplication::translate("WindowSystem", "Pin all tabs on this side") : QCoreApplication::translate("WindowSystem", "Unpin all tabs on this side"), "布局", {},
        [this, pinned](const CommandContext& c) {
            const auto* group = d->model.group(c.group);
            const auto edge = static_cast<Edge>(c.args.value("edge", group ? int(group->edge) : int(Edge::Left)).toInt());
            return pinEdge(c.window, edge, pinned);
        }, hasWindow);
    const auto canStick = [this](const CommandContext& c) {
        const auto* g = d->model.group(d->model.groupForTab(c.tab));
        return g && g->location != GroupLocation::AutoHide && !d->model.isTabSticky(c.tab);
    };
    const auto isSticky = [this](const CommandContext& c) { return d->model.isTabSticky(c.tab); };
    add("tab.stick", QCoreApplication::translate("WindowSystem", "Stick tab to front"), "布局", {},
        [this](const CommandContext& c) { return setTabSticky(c.tab, true); }, canStick);
    add("tab.unstick", QCoreApplication::translate("WindowSystem", "Unstick tab"), "布局", {},
        [this](const CommandContext& c) { return setTabSticky(c.tab, false); }, isSticky);
    add("tab.toggleSticky", QCoreApplication::translate("WindowSystem", "Stick or unstick tab"), "布局", {},
        [this](const CommandContext& c) { return setTabSticky(c.tab, !d->model.isTabSticky(c.tab)); },
        [canStick, isSticky](const CommandContext& c) { return canStick(c) || isSticky(c); });
    for (const auto& scope : {QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("others"), QStringLiteral("all")}) {
        const auto title = scope == "left" ? QCoreApplication::translate("WindowSystem", "Close tabs to the left") : scope == "right" ? QCoreApplication::translate("WindowSystem", "Close tabs to the right")
            : scope == "others" ? QCoreApplication::translate("WindowSystem", "Close other tabs") : QCoreApplication::translate("WindowSystem", "Close all tabs in this group");
        auto selected = [this, scope](const CommandContext& c) {
            QVector<TabId> tabs;
            const auto* group = d->model.group(d->model.groupForTab(c.tab)); if (!group) return tabs;
            const auto current = group->tabs.indexOf(c.tab);
            // Closing a range never takes sticky tabs; closing one directly does.
            for (int i = group->stickyTabs; i < group->tabs.size(); ++i)
                if (scope == "all" || (scope == "others" && i != current) || (scope == "left" && i < current) || (scope == "right" && i > current)) tabs << group->tabs[i];
            return tabs;
        };
        add("workspace.closeTabs." + scope, title, "文件", {},
            [this, selected](const CommandContext& c) { return closeTabs(selected(c)); },
            [selected](const CommandContext& c) { return !selected(c).isEmpty(); });
    }
    add("workspace.restoreGroupToCenter", QCoreApplication::translate("WindowSystem", "Move sidebar tab group back to center"), "布局", {},
        [this](const CommandContext& c) { return moveGroupToCenter(c.group); }, hasTab);
    add("pane.extractFloating", QCoreApplication::translate("WindowSystem", "Move current pane to a floating window"), "窗口", {},
        [this](const CommandContext& c) { return !extractPane(c.tab, c.pane, WindowKind::Floating).isNull(); }, hasTab);
    add("pane.extractWorkbench", QCoreApplication::translate("WindowSystem", "Move current pane to a workbench window"), "窗口", {},
        [this](const CommandContext& c) { return !extractPane(c.tab, c.pane, WindowKind::Workbench).isNull(); }, hasTab);
    // Closing a tab's last pane closes the tab, so the same protection applies.
    add("pane.close", QCoreApplication::translate("WindowSystem", "Close current pane"), "布局", QKeySequence("Ctrl+Shift+W"),
        [this, shortcutKeepsSticky](const CommandContext& c) {
            return (d->model.panes(c.tab).size() == 1 && shortcutKeepsSticky(c.tab)) || closePane(c.tab, c.pane);
        }, hasTab);
    add("window.toggleStripsLock", QCoreApplication::translate("WindowSystem", "Lock or unlock menus and toolbars"), "工具", {},
        [this](const CommandContext& c) {
            const auto* window = d->windows.value(c.window).data();
            if (!window || !window->strips()) return false;
            if (c.args.contains("locked") && c.args.value("locked").metaType().id() != QMetaType::Bool) return false;
            return setToolbarsLocked(c.window, c.args.value("locked", !window->strips()->isLocked()).toBool());
        },
        [this](const CommandContext& c) { const auto* window = d->windows.value(c.window).data(); return window && window->strips(); });
    add("window.drag.selectTarget", QCoreApplication::translate("WindowSystem", "Select target window while dragging"), "窗口", {},
        [this](const CommandContext&) { d->drag->selectTarget(); return true; }, [this](const CommandContext&) { return d->drag->isActive(); });
    add("window.drag.cancel", QCoreApplication::translate("WindowSystem", "Cancel current drag"), "窗口", {},
        [this](const CommandContext&) { d->drag->cancel(); return true; }, [this](const CommandContext&) { return d->drag->isActive(); });
    add("layout.setRatio", QCoreApplication::translate("WindowSystem", "Set split ratio"), "布局", {},
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
                const SplitterLayoutPass pass;
                splitter->setSizes({qRound(available * ratio), qRound(available * (1. - ratio))});
            }
            emit layoutChanged();
            return true;
        });
    add("layout.setEdgeExtent", QCoreApplication::translate("WindowSystem", "Set pinned sidebar region size"), "布局", {},
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
