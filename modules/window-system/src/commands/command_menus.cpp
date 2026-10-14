// SPDX-License-Identifier: MIT
#include "command_menus.hpp"
#include "popup_menu.hpp"
#include "chrome/command_strip.hpp"
#include "chrome/workspace_window.hpp"
#include <window_system/window_manager.hpp>

#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTimer>
#include <algorithm>

namespace window_system {
namespace {

struct Item {
    QString id;
    QString title;
    QVector<Item> children;
    int order = 0;
    bool command = false;
};
using Items = QVector<Item>;
Item command(QString id, int order = 0) { return {std::move(id), {}, {}, order, true}; }
Item command(const char* id, int order = 0) { return command(QString::fromUtf8(id), order); }
Item group(const char* id, const char* title, Items children, int order = 0)
{
    return {QString::fromUtf8(id), QString::fromUtf8(title), std::move(children), order, false};
}
Item separator(int order = 0) { return {{}, {}, {}, order, false}; }
Items directions(const char* prefix)
{
    Items result;
    for (const auto* direction : {"left", "right", "up", "down"})
        result.append(command(QString::fromLatin1(prefix) + QString::fromLatin1(direction)));
    return result;
}
Items tabMovement()
{
    return {group("tab.move.group", "到标签组", directions("workspace.moveTabToGroup.")),
        group("tab.move.window", "到已有窗口", {command("workspace.moveTabToWindow.previous"),
            command("workspace.moveTabToWindow.next"), separator(), command("workspace.moveTabToWindow")}),
        group("tab.move.newWindow", "到新窗口", {command("workspace.detachFloating"), command("workspace.detachWorkbench")})};
}
Items paneMovement()
{
    return {group("pane.move.tab", "到标签", {command("pane.moveToTab.previous"), command("pane.moveToTab.next"),
            separator(), command("pane.moveToTab")}),
        group("pane.move.group", "到标签组", directions("pane.moveToGroup.")),
        group("pane.move.window", "到已有窗口", {command("pane.moveToWindow.previous"), command("pane.moveToWindow.next"),
            separator(), command("pane.moveToWindow")}),
        group("pane.move.newWindow", "到新窗口", {command("pane.extractFloating"), command("pane.extractWorkbench")})};
}
Items edgeOperations(bool includeCurrent = true)
{
    Items result{group("edge.collect", "收纳当前标签", directions("workspace.hideToEdge.")), separator()};
    if (includeCurrent) result += {command("workspace.pinTab"), command("workspace.unpinTab")};
    result += {
        group("edge.peek", "临时展开大小", {command("tab.peek.setSize"), command("tab.peek.resetSize")}),
        group("edge.group", "当前边栏组", {command("workspace.pinGroup"), command("workspace.unpinGroup"),
            command("workspace.restoreGroupToCenter")}),
        group("edge.all", "当前侧全部标签", {command("workspace.pinEdge"), command("workspace.unpinEdge")})};
    return result;
}
Items windowOperations()
{
    return {group("window.kind", "窗口形态", {command("window.promote"), command("window.demote"), command("window.reassignFamily")}),
        group("window.transfer", "整窗移动", {command("window.moveAllTabs"), command("window.redock")})};
}
Items floatingItems(Items items)
{
    for (auto iterator = items.begin(); iterator != items.end();) {
        if (iterator->command && !floatingMenuExclusion(iterator->id).isEmpty()) iterator = items.erase(iterator);
        else {
            iterator->children = floatingItems(std::move(iterator->children));
            ++iterator;
        }
    }
    return items;
}
const Items& layout(CommandMenuSurface surface)
{
    static const Items application = {
        group("文件", "文件(&F)", {command("window.new", 20), separator(30), separator(60),
            command("workspace.closeTab", 70), command("window.close", 80), separator(90)}, 10),
        group("视图", "视图(&V)", {command("workbench.showCommandPalette"), separator(),
            group("view.visibility", "显示", {command("window.toolbars.toggle"), command("window.statusBar.toggle"),
                command("window.tabBars.toggle"), command("window.sideBars.toggle")}),
            group("view.fullscreen", "全屏", {command("window.fullscreen.toggle"), command("window.fullscreen.group"),
                command("window.fullscreen.pane"), separator(), command("window.zen.toggle"), command("window.fullscreen.exit")}),
            group("view.tabs", "切换标签", {command("workspace.previousTab"), command("workspace.nextTab")}),
            group("view.groups", "聚焦标签组", directions("workspace.focusGroup.")),
            group("view.panes", "聚焦窗格", directions("pane.focus.")),
            group("view.toolbars", "工具条", {command("window.toggleStripsLock"), command("window.toolbar.move")})}, 20),
        group("布局", "布局(&L)", {
            group("layout.groups", "标签组", {group("group.create", "新建标签组", directions("workspace.splitGroup.")),
                group("group.resize", "调整边界", directions("workspace.resizeGroup."))}),
            group("layout.panes", "窗格", {group("pane.split", "拆分", directions("pane.split.")),
                group("pane.resize", "调整边界", directions("pane.resize.")),
                group("pane.swap", "交换位置", directions("pane.swap."))}),
            group("layout.tabOrder", "排列标签", {command("workspace.reorderTab.previous"), command("workspace.reorderTab.next")}),
            group("layout.moveTab", "移动标签", tabMovement()), group("layout.movePane", "移动窗格", paneMovement()),
            group("layout.edge", "边栏", edgeOperations()), separator(), command("pane.close"), separator(900)}, 30),
        group("窗口", "窗口(&W)", {command("window.select"), command("window.focusOwner"), separator(),
            group("window.cycle", "切换窗口", {
                group("window.cycle.family", "所属窗口族", {command("window.focusPrevious.family"), command("window.focusNext.family")}),
                group("window.cycle.all", "全部窗口", {command("window.focusPrevious.all"), command("window.focusNext.all")}),
                group("window.cycle.workbenches", "完整窗口", {command("window.focusPrevious.workbenches"), command("window.focusNext.workbenches")})}),
            windowOperations()[0], windowOperations()[1],
            group("window.drag", "拖动", {command("window.drag.selectTarget"), command("window.drag.cancel")})}, 40),
        group("工具", "工具(&T)", {command("window.settings"), command("preferences.openKeyboardShortcuts")}, 50),
        group("帮助", "帮助(&H)", {}, 60)};
    static const Items floating = floatingItems(application);
    static const Items tab = {command("workspace.closeTab"), command("workspace.pinTab"), command("workspace.unpinTab"),
        group("tab.closeRange", "批量关闭标签", {command("workspace.closeTabs.left"), command("workspace.closeTabs.right"),
            command("workspace.closeTabs.others"), command("workspace.closeTabs.all")}), separator(),
        group("tab.order", "排列标签", {command("workspace.reorderTab.previous"), command("workspace.reorderTab.next")}),
        group("tab.move", "移动标签", tabMovement()),
        group("tab.group", "标签组", {group("tab.group.create", "新建标签组", directions("workspace.splitGroup.")),
            group("tab.group.resize", "调整边界", directions("workspace.resizeGroup."))}),
        group("tab.edge", "边栏", edgeOperations(false))};
    static const Items pane = {command("pane.close"), separator(),
        group("pane.split", "拆分窗格", directions("pane.split.")),
        group("pane.move", "移动窗格", paneMovement()),
        group("pane.swap", "交换窗格位置", directions("pane.swap.")),
        group("pane.resize", "调整边界", directions("pane.resize.")),
        group("pane.bars", "附加栏", {command("tab.topBar.setSharing"), command("tab.bottomBar.setSharing")})};
    static const Items window = {command("window.select"), command("window.focusOwner"), separator(),
        windowOperations()[0], windowOperations()[1], separator(), command("window.settings"), command("window.close")};
    switch (surface) {
    case CommandMenuSurface::Application: return application;
    case CommandMenuSurface::FloatingApplication: return floating;
    case CommandMenuSurface::Tab: return tab;
    case CommandMenuSurface::Pane: return pane;
    case CommandMenuSurface::Window: return window;
    }
    return application;
}
void collectCommands(const Items& items, QStringList& result)
{
    for (const auto& item : items) {
        if (item.command) result.append(item.id);
        else collectCommands(item.children, result);
    }
}
QString plainTitle(QString title) { return title.remove(QRegularExpression(QStringLiteral("\\(&[A-Za-z]\\)"))); }

void tidy(QMenu* menu)
{
    bool previous = false; QAction* pending = nullptr;
    for (auto* action : menu->actions()) {
        if (auto* child = action->menu()) {
            tidy(child);
            const auto childActions = child->actions();
            action->setVisible(std::any_of(childActions.cbegin(), childActions.cend(),
                [](QAction* item) { return !item->isSeparator() && item->isVisible(); }));
        }
        if (action->isSeparator()) { action->setVisible(false); pending = action; }
        else if (action->isVisible()) {
            if (previous && pending) pending->setVisible(true);
            pending = nullptr; previous = true;
        }
    }
}
bool orderedBefore(const QAction* left, const QAction* right)
{
    const int a = left->property("menuOrder").toInt(), b = right->property("menuOrder").toInt();
    if (a != b) return a < b;
    const auto titleA = left->property("menuSortTitle").toString(), titleB = right->property("menuSortTitle").toString();
    const int titleOrder = QString::localeAwareCompare(titleA, titleB);
    return titleOrder ? titleOrder < 0 : left->objectName() < right->objectName();
}
void insertOrdered(QWidget* owner, QAction* added)
{
    const auto actions = owner->actions();
    const auto before = std::find_if(actions.cbegin(), actions.cend(), [added](QAction* action) {
        return action != added && orderedBefore(added, action);
    });
    if (before != actions.cend()) owner->insertAction(*before, added);
}
QMenu* makeMenu(QWidget* owner, const Item& item, const QStringList& path)
{
    auto* menu = new DismissOnlyMenu(item.title, owner);
    menu->setObjectName(item.id); menu->setProperty("commandMenuPath", path);
    menu->setStyleSheet(QStringLiteral("QMenu { menu-scrollable: 1; }"));
    menu->menuAction()->setObjectName(item.id); menu->menuAction()->setProperty("menuOrder", item.order);
    menu->menuAction()->setProperty("menuSortTitle", plainTitle(item.title));
    return menu;
}
using Context = std::function<CommandContext()>;
void addItems(QMenu* menu, const Items& items, WindowManager* manager, const Context& context,
              QList<QPointer<QAction>>& actions, QList<QPointer<QMenu>>* menus, QObject* actionOwner)
{
    const QPointer<QMenu> retainedMenu(menu);
    const QPointer<WindowManager> retainedManager(manager);
    const QPointer<QObject> retainedOwner(actionOwner);
    int ordinal = 0;
    for (const auto& item : items) {
        if (!retainedMenu || !retainedManager || (actionOwner && !retainedOwner)) return;
        QAction* action = nullptr;
        if (item.command) {
            action = menu->addAction(QString()); action->setData(item.id); action->setObjectName(item.id);
            action->setProperty("windowSystemCommand", item.id); actions.append(action);
            const QPointer<QAction> retainedAction(action);
            refreshCommandAction(manager, action, context());
            if (!retainedMenu || !retainedManager || !retainedAction || (actionOwner && !retainedOwner)) return;
            if (actionOwner) QObject::connect(action, &QAction::triggered, actionOwner,
                [manager = QPointer<WindowManager>(manager), receiver = QPointer<QObject>(actionOwner), id = item.id, context] {
                if (!manager || !receiver) return;
                const auto captured = context();
                QTimer::singleShot(0, receiver.data(), [manager, id, captured] {
                    if (!manager) return;
                    const bool executed = manager->commands().execute(id, captured);
                    if (manager && !executed) emit manager->commandFailed(QStringLiteral("命令不可执行：%1").arg(id));
                });
            });
        } else if (!item.id.isEmpty()) {
            const auto path = menu->property("commandMenuPath").toStringList() + QStringList{plainTitle(item.title)};
            auto* child = makeMenu(menu, item, path); menu->addMenu(child); action = child->menuAction();
            if (menus) menus->append(child);
            addItems(child, item.children, manager, context, actions, menus, actionOwner);
            if (!retainedMenu || !retainedManager || (actionOwner && !retainedOwner)) return;
        } else action = menu->addSeparator();
        action->setProperty("menuOrder", item.order ? item.order : ++ordinal * 10);
        action->setProperty("menuSortTitle", item.command ? item.id : item.title);
    }
}

} // namespace

QStringList declaredMenuCommands(CommandMenuSurface surface)
{
    QStringList result; collectCommands(layout(surface), result); return result;
}
QString applicationMenuExclusion(const QString& id)
{
    if (id == "layout.setRatio" || id == "layout.setEdgeExtent") return QStringLiteral("内部布局提交，需要明确节点或分区参数");
    if (id == "window.moveAllToEdge" || id == "window.moveAllToEdgeBar") return QStringLiteral("内部整窗投放，需要目标窗口和落点参数");
    if (id == "workspace.moveTabToEdge" || id == "workspace.moveTabToEdgeBar") return QStringLiteral("内部标签投放；用户入口为四方向收纳及拖拽");
    if (id == "window.toolbar.setSizing" || id == "window.toolbar.resetSize") return QStringLiteral("工具条选项菜单及命令面板提供定制入口");
    if (id == "tab.topBar.setSharing" || id == "tab.bottomBar.setSharing") return QStringLiteral("内容右键附加栏菜单及命令面板提供入口");
    const QStringList closeScopes{"workspace.closeTabs.left", "workspace.closeTabs.right", "workspace.closeTabs.others", "workspace.closeTabs.all"};
    if (closeScopes.contains(id)) return QStringLiteral("标签右键提供关闭范围；文件菜单保留关闭当前标签");
    return {};
}
QString floatingMenuExclusion(const QString& id)
{
    if (const auto reason = applicationMenuExclusion(id); !reason.isEmpty()) return reason;
    if (id == "application.exit") return QStringLiteral("退出整个应用只在完整窗口提供菜单入口");
    if (id == "layout.save" || id == "layout.reset") return QStringLiteral("完整工作区保存与重置只在完整窗口提供菜单入口");
    if (id == "window.new") return QStringLiteral("新建完整工作窗口只在完整窗口提供菜单入口");
    if (id == "window.statusBar.toggle") return QStringLiteral("轻量窗口没有全局状态栏");
    if (id == "window.demote") return QStringLiteral("轻量窗口已经采用浮动形态");
    return {};
}

void refreshCommandAction(WindowManager* manager, QAction* action, const CommandContext& context)
{
    if (!action) return;
    if (!manager) { action->setVisible(false); action->setEnabled(false); return; }
    const auto id = action->data().toString(); const auto* found = manager->commands().find(id);
    if (!found) { action->setVisible(false); action->setEnabled(false); return; }
    const Command definition = *found;
    const QPointer<QAction> retained(action); const QPointer<WindowManager> owner(manager);
    const bool enabled = !definition.enabled || definition.enabled(context);
    if (!retained || !owner) return;
    const auto* window = manager->model().window(context.window);
    const auto* group = manager->model().group(context.group);
    bool visible = true;
    if (id == "window.promote") visible = window && window->kind == WindowKind::Floating;
    if (id == "window.demote") visible = window && window->kind == WindowKind::Workbench && enabled;
    if (id == "window.redock" || id == "window.reassignFamily") visible = window && window->kind == WindowKind::Floating && enabled;
    if (id == "window.drag.selectTarget" || id == "window.drag.cancel") visible = enabled;
    if (id == "workspace.pinTab" || id == "workspace.pinGroup") visible = group && group->location == GroupLocation::AutoHide;
    if (id == "tab.peek.setSize" || id == "tab.peek.resetSize") visible = group && group->location == GroupLocation::AutoHide;
    if (id == "workspace.unpinTab" || id == "workspace.unpinGroup") visible = group && group->location == GroupLocation::Pinned;
    if (id == "workspace.pinEdge" || id == "workspace.unpinEdge" || id == "workspace.restoreGroupToCenter")
        visible = group && group->location != GroupLocation::Central;
    const bool checkedCommand = id == "window.tabBars.toggle" || id == "window.sideBars.toggle"
        || id == "window.toolbars.toggle" || id == "window.statusBar.toggle" || id == "window.zen.toggle"
        || (id.startsWith("window.fullscreen.") && id != "window.fullscreen.exit")
        || id == "window.toggleStripsLock" || id == "tab.topBar.setSharing" || id == "tab.bottomBar.setSharing";
    const QSignalBlocker blocker(action); action->setCheckable(checkedCommand);
    if (id == "window.tabBars.toggle") action->setChecked(manager->tabBarsVisible(context.window));
    if (id == "window.sideBars.toggle") action->setChecked(manager->sideBarsVisible(context.window));
    if (id == "window.toolbars.toggle") action->setChecked(manager->toolbarsVisible(context.window));
    if (id == "window.statusBar.toggle") action->setChecked(manager->statusBarVisible(context.window));
    if (id == "window.zen.toggle") action->setChecked(manager->zenMode(context.window));
    if (id == "window.fullscreen.toggle") action->setChecked(manager->viewMode(context.window) == ViewMode::WindowFullscreen);
    if (id == "window.fullscreen.group") action->setChecked(manager->viewMode(context.window) == ViewMode::GroupFullscreen);
    if (id == "window.fullscreen.pane") action->setChecked(manager->viewMode(context.window) == ViewMode::PaneFullscreen);
    if (id == "window.toggleStripsLock") {
        const auto* view = static_cast<WorkspaceWindow*>(manager->windowWidget(context.window));
        action->setChecked(view && view->strips() && view->strips()->isLocked());
    }
    if (id == "tab.topBar.setSharing" || id == "tab.bottomBar.setSharing") {
        const auto options = manager->tabOptions(context.tab);
        action->setChecked(manager->model().tab(context.tab) && (id == "tab.topBar.setSharing"
            ? options.topBarSharing : options.bottomBarSharing) == PaneBarSharing::Shared);
    }
    const auto shortcut = definition.shortcut.toString(QKeySequence::NativeText);
    action->setText(definition.title + (!action->property("stripCommand").toBool() && !shortcut.isEmpty() ? "\t" + shortcut : QString()));
    action->setToolTip(definition.title + (shortcut.isEmpty() ? QString() : "  " + shortcut));
    action->setVisible(visible); action->setEnabled(enabled);
}

void populateContextMenu(QMenu* menu, CommandMenuSurface surface, WindowManager* manager, const CommandContext& captured)
{
    if (!menu->actions().isEmpty()) menu->addSeparator();
    QList<QPointer<QAction>> actions;
    addItems(menu, layout(surface), manager, [captured] { return captured; }, actions, nullptr, nullptr);
    tidy(menu);
    for (auto* child : menu->findChildren<QMenu*>()) QObject::connect(child, &QMenu::aboutToShow, menu,
        [manager = QPointer<WindowManager>(manager), actions, captured, menu] {
        for (const auto& action : actions) if (action) refreshCommandAction(manager, action, captured);
        tidy(menu);
    });
}

ApplicationCommandMenus::ApplicationCommandMenus(QMenuBar* bar, WindowManager* manager, WindowId window,
                                               CommandMenuSurface surface)
    : QObject(bar), bar_(bar), manager_(manager), window_(window), surface_(surface)
{
    for (const auto& item : layout(surface_)) {
        auto* menu = makeMenu(bar, item, {item.id}); bar->addMenu(menu); menus_.append(menu);
        addItems(menu, item.children, manager_, [this] { return manager_ ? manager_->context(window_) : CommandContext{}; }, actions_, &menus_, this);
    }
    for (const auto& menu : menus_) connect(menu, &QMenu::aboutToShow, this, [this] { refresh(); });
    refresh();
}
QMenu* ApplicationCommandMenus::menuAtPath(const QStringList& path)
{
    if (path.isEmpty() || !bar_) return nullptr;
    QStringList partial;
    QMenu* parent = nullptr;
    for (const auto& title : path) {
        partial.append(title);
        auto existing = std::find_if(menus_.cbegin(), menus_.cend(), [&partial](const auto& menu) {
            return menu && menu->property("commandMenuPath").toStringList() == partial;
        });
        if (existing != menus_.cend()) parent = existing->data();
        else {
            Item item{QStringLiteral("extension.") + partial.join('.'), title, {}, 500, false};
            auto* child = makeMenu(parent ? static_cast<QWidget*>(parent) : bar_.data(), item, partial);
            if (parent) { parent->addMenu(child); insertOrdered(parent, child->menuAction()); }
            else { bar_->addMenu(child); insertOrdered(bar_, child->menuAction()); }
            menus_.append(child); connect(child, &QMenu::aboutToShow, this, [this] { refresh(); }); parent = child;
        }
    }
    return parent;
}
void ApplicationCommandMenus::addContributions()
{
    if (!manager_ || !bar_) return;
    const QPointer<ApplicationCommandMenus> retained(this);
    const auto declared = declaredMenuCommands(surface_);
    for (const auto& definition : manager_->commands().commands()) {
        const auto exclusion = surface_ == CommandMenuSurface::FloatingApplication
            ? floatingMenuExclusion(definition.id) : applicationMenuExclusion(definition.id);
        if (declared.contains(definition.id) || !exclusion.isEmpty()) continue;
        if (std::any_of(actions_.cbegin(), actions_.cend(), [&definition](const auto& action) {
            return action && action->data().toString() == definition.id;
        })) continue;
        auto path = definition.menuPath;
        if (path.isEmpty()) {
            const auto category = definition.category.isEmpty() ? QStringLiteral("其他") : definition.category;
            const QStringList core{QStringLiteral("文件"), QStringLiteral("视图"), QStringLiteral("布局"),
                QStringLiteral("窗口"), QStringLiteral("工具"), QStringLiteral("帮助")};
            path = core.contains(category) ? QStringList{category, QStringLiteral("扩展")}
                : QStringList{QStringLiteral("扩展"), category};
        }
        auto* menu = menuAtPath(path); if (!menu) continue;
        const auto count = actions_.size();
        addItems(menu, {command(definition.id, definition.menuOrder)}, manager_,
            [this] { return manager_ ? manager_->context(window_) : CommandContext{}; }, actions_, nullptr, this);
        if (!retained || !manager_ || !bar_) return;
        if (actions_.size() > count) {
            actions_.last()->setProperty("menuOrder", definition.menuOrder);
            actions_.last()->setProperty("menuSortTitle", definition.title);
        }
        insertOrdered(menu, actions_.last());
    }
}
void ApplicationCommandMenus::refresh()
{
    if (!bar_) return;
    if (!manager_) {
        for (const auto& action : actions_) if (action) refreshCommandAction(nullptr, action, {});
        return;
    }
    const QPointer<ApplicationCommandMenus> retained(this);
    const auto previousCount = actions_.size();
    addContributions(); if (!retained || !manager_ || !bar_) return;
    const auto context = manager_->context(window_);
    bool availabilityChanged = false;
    const auto actions = actions_;
    for (const auto& action : actions) {
        if (action) refreshCommandAction(manager_, action, context);
        if (!retained || !manager_ || !bar_) return;
    }
    for (auto* rootAction : bar_->actions()) if (auto* menu = rootAction->menu()) {
        tidy(menu); const auto rootActions = menu->actions();
        const bool available = std::any_of(rootActions.cbegin(), rootActions.cend(),
            [](QAction* action) { return !action->isSeparator() && action->isVisible(); });
        availabilityChanged |= rootAction->property("commandMenuAvailable").toBool() != available;
        rootAction->setProperty("commandMenuAvailable", available);
        if (!available) rootAction->setVisible(false);
    }
    if ((previousCount != actions_.size() || availabilityChanged) && structureChanged) structureChanged();
}

} // namespace window_system
