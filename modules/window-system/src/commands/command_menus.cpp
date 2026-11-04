// SPDX-License-Identifier: MIT
#include <QCoreApplication>
#include "command_menus.hpp"
#include "popup_menu.hpp"
#include "chrome/command_strip.hpp"
#include "style/tab_appearance_p.hpp"
#include "localization/display_names.hpp"
#include "chrome/workspace_window.hpp"
#include <window_system/window_manager.hpp>

#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTimer>
#include <QVariant>
#include <algorithm>
#include <optional>

namespace window_system {
namespace {

using Item = CommandMenuItem;
using Items = QVector<Item>;
QString plainTitle(QString title) { return title.remove(QRegularExpression(QStringLiteral("\\(&[A-Za-z]\\)"))); }

void tidy(QMenu* menu)
{
    bool previous = false; QAction* pending = nullptr;
    QSet<QAction*> visibleSeparators;
    for (auto* action : menu->actions()) {
        if (auto* child = action->menu()) {
            tidy(child);
            const auto childActions = child->actions();
            action->setVisible(std::any_of(childActions.cbegin(), childActions.cend(),
                [](QAction* item) { return !item->isSeparator() && item->isVisible(); }));
        }
        if (action->isSeparator()) pending = action;
        else if (action->isVisible()) {
            if (previous && pending) visibleSeparators.insert(pending);
            pending = nullptr; previous = true;
        }
    }
    // Submenu opening refreshes the command tree while ancestors are visible.
    // Decide final separator visibility before mutation: hide-then-show makes
    // QMenu resize/reposition twice and flashes the parent under the cursor.
    for (auto* action : menu->actions()) if (action->isSeparator())
        action->setVisible(visibleSeparators.contains(action));
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
    const auto title = item.id.startsWith(QStringLiteral("extension."))
        ? commandCategoryDisplayName(item.title)
        : QCoreApplication::translate("WindowSystem", item.title.toUtf8().constData());
    auto* menu = new DismissOnlyMenu(title, owner);
    menu->setObjectName(item.id); menu->setProperty("commandMenuPath", path);
    menu->setStyleSheet(QStringLiteral("QMenu { menu-scrollable: 1; }"));
    menu->menuAction()->setObjectName(item.id); menu->menuAction()->setProperty("menuOrder", item.order);
    menu->menuAction()->setProperty("menuSortTitle", plainTitle(item.title));
    return menu;
}
using Context = std::function<CommandContext()>;
void addItems(QMenu* menu, const Items& items, WindowManager* manager, const Context& context,
              QList<QPointer<QAction>>& actions, QList<QPointer<QMenu>>* menus, QObject* actionOwner,
              bool contribution = false)
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
            if (contribution) action->setProperty("commandMenuContribution", true);
            const QPointer<QAction> retainedAction(action);
            refreshCommandAction(manager, action, context());
            if (!retainedMenu || !retainedManager || !retainedAction || (actionOwner && !retainedOwner)) return;
            if (actionOwner) QObject::connect(action, &QAction::triggered, actionOwner,
                [manager = QPointer<WindowManager>(manager), receiver = QPointer<QObject>(actionOwner), id = item.id, context] {
                if (!manager || !receiver) return;
                const auto captured = context();
                QTimer::singleShot(0, receiver.data(), [manager, id, captured] {
                    if (!manager) return;
                    manager->executeInContext(id, captured);
                });
            });
        } else if (!item.id.isEmpty()) {
            const auto path = menu->property("commandMenuPath").toStringList() + QStringList{item.pathKey};
            auto* child = makeMenu(menu, item, path); menu->addMenu(child); action = child->menuAction();
            if (menus) menus->append(child);
            addItems(child, item.children, manager, context, actions, menus, actionOwner, contribution);
            if (!retainedMenu || !retainedManager || (actionOwner && !retainedOwner)) return;
        } else action = menu->addSeparator();
        action->setProperty("menuOrder", item.order ? item.order : ++ordinal * 10);
        action->setProperty("menuSortTitle", item.command ? item.id : item.title);
    }
}

} // namespace

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
    if (id == "tab.stick" || id == "tab.unstick") visible = enabled;
    if (id == "tab.peek.setSize" || id == "tab.peek.resetSize") visible = group && group->location == GroupLocation::AutoHide;
    if (id == "workspace.unpinTab" || id == "workspace.unpinGroup") visible = group && group->location == GroupLocation::Pinned;
    if (id == "workspace.pinEdge" || id == "workspace.unpinEdge" || id == "workspace.restoreGroupToCenter")
        visible = group && group->location != GroupLocation::Central;
    std::optional<bool> appearanceChecked;
    if (id == "presentation.toggleIcons") appearanceChecked = manager->tabAppearance().showIcons;
    else if (id == "presentation.toggleSidebarIcons") appearanceChecked = manager->tabAppearance().showSidebarIcons;
    else if (id == "presentation.toggleSidebarNumbers") appearanceChecked = manager->tabAppearance().showSidebarNumbers;
    else if (id == "presentation.toggleNewTabButton") appearanceChecked = manager->tabAppearance().showNewTabButton;
    else if (id == "presentation.toggleNumbers") appearanceChecked = manager->tabAppearance().showNumbers;
    else if (id == "presentation.toggleTitleClipping") appearanceChecked = manager->tabAppearance().clipTitles;
    else if (id == "presentation.toggleSidebarTitleClipping") appearanceChecked = manager->tabAppearance().clipSidebarTitles;
    else if (id == "presentation.toggleMultiline") appearanceChecked = manager->tabAppearance().multiline;
    else if (id == "presentation.toggleAllRows") appearanceChecked = manager->tabAppearance().showAllRows;
    else if (id == "presentation.toggleStickyMultiline") appearanceChecked = manager->tabAppearance().stickyMultiline;
    else if (id == "presentation.toggleStickyAllRows") appearanceChecked = manager->tabAppearance().stickyShowAllRows;
    else if (id == "presentation.toggleWheelExpandRows") appearanceChecked = manager->tabAppearance().wheelExpandRows;
    const bool checkedCommand = appearanceChecked.has_value() || id == "window.tabBars.toggle" || id == "window.sideBars.toggle"
        || id == "window.toolbars.toggle" || id == "window.statusBar.toggle" || id == "window.zen.toggle"
        || (id.startsWith("window.fullscreen.") && id != "window.fullscreen.exit")
        || id == "window.toggleStripsLock" || id == "tab.topBar.setSharing" || id == "tab.bottomBar.setSharing";
    const QSignalBlocker blocker(action); action->setCheckable(checkedCommand);
    if (appearanceChecked) action->setChecked(*appearanceChecked);
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
    QStringList shortcuts;
    for (const auto& binding : definition.bindings)
        shortcuts.append(binding.sequence.toString(QKeySequence::NativeText));
    const auto shortcut = shortcuts.join(QStringLiteral(" / "));
    action->setText(definition.title + (!action->property("stripCommand").toBool() && !shortcut.isEmpty() ? "\t" + shortcut : QString()));
    action->setToolTip(definition.title + (shortcut.isEmpty() ? QString() : "  " + shortcut));
    action->setVisible(visible); action->setEnabled(enabled);
}

void populateContextMenu(QMenu* menu, CommandMenuSurface surface, WindowManager* manager, const CommandContext& captured)
{
    const QPointer<QMenu> retainedMenu(menu);
    const QPointer<WindowManager> retainedManager(manager);
    if (!menu->actions().isEmpty()) menu->addSeparator();
    QList<QPointer<QAction>> actions;
    addItems(menu, commandMenuDefinition(surface), manager, [captured] { return captured; }, actions, nullptr, manager);
    if (!retainedMenu || !retainedManager) return;
    struct Contribution { Command command; ContextMenuPlacement placement; };
    QVector<Contribution> contributions;
    const auto target = surface == CommandMenuSurface::Tab ? ContextMenuTarget::Tab
        : surface == CommandMenuSurface::Group ? ContextMenuTarget::Group
        : surface == CommandMenuSurface::Pane ? ContextMenuTarget::Pane : ContextMenuTarget::Window;
    const auto contentType = manager->tabOptions(captured.tab).contentType;
    for (const auto& command : manager->commands().commands())
        for (const auto& placement : command.contextMenus)
            if (placement.target == target && (placement.contentType.isEmpty() || placement.contentType == contentType))
                contributions.append({command, placement});
    std::stable_sort(contributions.begin(), contributions.end(), [](const auto& left, const auto& right) {
        if (left.placement.order != right.placement.order) return left.placement.order < right.placement.order;
        return left.command.id < right.command.id;
    });
    QHash<QString, QPointer<QMenu>> contributedMenus;
    if (!contributions.isEmpty()) menu->addSeparator();
    for (const auto& contribution : contributions) {
        if (!retainedMenu || !retainedManager) return;
        QPointer<QMenu> destination(menu);
        QString prefix;
        for (const auto& title : contribution.placement.path) {
            prefix += QChar(0x1f) + title;
            auto child = contributedMenus.value(prefix);
            if (!child) {
                child = new DismissOnlyMenu(title, destination);
                child->setObjectName(QStringLiteral("ExtensionContextMenu"));
                destination->addMenu(child); contributedMenus.insert(prefix, child);
            }
            destination = child;
        }
        addItems(destination, {Item::action(contribution.command.id)}, manager, [captured] { return captured; },
                 actions, nullptr, manager);
    }
    if (!retainedMenu || !retainedManager) return;
    if (surface == CommandMenuSurface::Tab || surface == CommandMenuSurface::Group) {
        for (auto* action : menu->actions()) if (action->objectName() == "tab.color.set") {
            auto* colors = new DismissOnlyMenu(QCoreApplication::translate("WindowSystem", "Set tab color"), menu);
            colors->setObjectName(QStringLiteral("TabColorChoices"));
            const auto addColor = [colors, owner = QPointer<WindowManager>(manager), captured](QString name, QString id) {
                auto* choice = colors->addAction(name); choice->setData(id); choice->setCheckable(true);
                if (owner && owner->model().tab(captured.tab)) choice->setChecked(owner->model().tab(captured.tab)->colorId == id);
                QObject::connect(choice, &QAction::triggered, colors, [owner, captured, id] {
                    if (!owner) return;
                    auto context = captured; context.args.insert("color", id);
                    owner->executeInContext(QStringLiteral("tab.color.set"), context);
                });
            };
            addColor(QCoreApplication::translate("WindowSystem", "Default color"), {});
            for (const auto& preset : manager->tabAppearance().colors) addColor(tabColorDisplayName(preset), preset.id);
            action->setMenu(colors);
        }
    }
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
    for (const auto& item : commandMenuDefinition(surface_)) {
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
            child->setProperty("commandMenuContribution", true);
            if (parent) { parent->addMenu(child); insertOrdered(parent, child->menuAction()); }
            else { bar_->addMenu(child); insertOrdered(bar_, child->menuAction()); }
            menus_.append(child); connect(child, &QMenu::aboutToShow, this, [this] { refresh(); }); parent = child;
        }
    }
    return parent;
}
bool ApplicationCommandMenus::removeUnusedContributions()
{
    bool removed = false;
    const QPointer<ApplicationCommandMenus> retained(this);
    const auto actions = actions_;
    for (const auto& action : actions) {
        if (!retained || !manager_ || !bar_) return removed;
        // Built-in placeholders intentionally survive absent host commands so
        // their declared position is still available for a later registration.
        if (action && action->property("commandMenuContribution").toBool()
            && !manager_->commands().find(action->data().toString())) {
            delete action.data();
            removed = true;
        }
    }
    if (!retained) return removed;
    actions_.removeIf([](const auto& action) { return action.isNull(); });
    // Children are appended after their parent. Reclaim empty dynamic branches
    // from the leaves upward, preserving both core menus and shared branches.
    const auto menus = menus_;
    for (auto it = menus.crbegin(); it != menus.crend(); ++it) {
        if (!retained || !manager_ || !bar_) return removed;
        const auto menu = *it;
        if (!menu || !menu->property("commandMenuContribution").toBool()) continue;
        const auto items = menu->actions();
        if (std::none_of(items.cbegin(), items.cend(), [](QAction* action) { return !action->isSeparator(); })) {
            delete menu.data();
            removed = true;
        }
    }
    if (retained) menus_.removeIf([](const auto& menu) { return menu.isNull(); });
    return removed;
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
            const auto category = definition.category.isEmpty() ? QStringLiteral("other") : definition.category;
            const QStringList core{QStringLiteral("file"), QStringLiteral("view"), QStringLiteral("layout"),
                QStringLiteral("window"), QStringLiteral("tools"), QStringLiteral("help")};
            path = core.contains(category) ? QStringList{category, QStringLiteral("extensions")}
                : QStringList{QStringLiteral("extensions"), category};
        }
        const QPointer<QMenu> menu(menuAtPath(path)); if (!menu) continue;
        addItems(menu, {Item::action(definition.id, definition.menuOrder)}, manager_,
            [this] { return manager_ ? manager_->context(window_) : CommandContext{}; }, actions_, nullptr, this, true);
        if (!retained || !manager_ || !bar_) return;
        // An enabled callback can unload its own command and synchronously
        // reclaim this branch. It can also register unrelated contributions;
        // the last action in the list is therefore not necessarily ours.
        if (!menu || !manager_->commands().find(definition.id)) continue;
        const auto found = std::find_if(actions_.cbegin(), actions_.cend(), [&definition](const auto& action) {
            return action && action->data().toString() == definition.id;
        });
        if (found == actions_.cend()) continue;
        const QPointer<QAction> action = *found;
        action->setProperty("menuOrder", definition.menuOrder);
        action->setProperty("menuSortTitle", definition.title);
        insertOrdered(menu, action);
    }
}
void ApplicationCommandMenus::refreshWindowList()
{
    if (!manager_ || !bar_) return;
    auto* menu = menuAtPath({QStringLiteral("window")});
    if (!menu) return;
    const auto order = manager_->model().windowOrder();
    const QSet<WindowId> present(order.cbegin(), order.cend());
    for (auto iterator = windowActions_.begin(); iterator != windowActions_.end();) {
        if (!present.contains(iterator.key()) || !iterator.value()) {
            if (iterator.value()) delete iterator.value().data();
            iterator = windowActions_.erase(iterator);
        } else ++iterator;
    }
    if (!windowListSeparator_) {
        windowListSeparator_ = menu->addSeparator();
        windowListSeparator_->setObjectName(QStringLiteral("WindowListSeparator"));
        windowListSeparator_->setProperty("menuOrder", 1000000);
    }
    QList<QAction*> desiredTail{windowListSeparator_};
    const auto append = [this, menu, &desiredTail](WindowId id, const QString& prefix, bool floating) {
        const auto* widget = manager_->windowWidget(id);
        if (!widget) return;
        auto retained = windowActions_.value(id);
        if (!retained) {
            auto* action = new QAction(menu);
            action->setObjectName(QStringLiteral("WindowListItem"));
            action->setProperty("windowSystemWindow", id);
            action->setProperty("menuOrder", 1000000);
            action->setCheckable(true);
            connect(action, &QAction::triggered, this,
                [manager = manager_, source = window_, target = id] {
                    if (!manager || !manager->model().window(target)) return;
                    QTimer::singleShot(0, manager, [manager, source, target] {
                        if (manager) manager->execute(QStringLiteral("window.activate"),
                            {{QStringLiteral("targetWindow"), target}}, source);
                    });
                });
            retained = action; windowActions_.insert(id, action);
        }
        retained->setText(prefix + widget->windowTitle());
        retained->setToolTip(widget->windowTitle());
        const auto theme = manager_->interfaceTheme();
        const QVariantList iconTokens{floating, theme.text, theme.mutedText, theme.selectionText, theme.accent};
        if (retained->property("windowListIconTokens").toList() != iconTokens) {
            retained->setIcon(iconFor(floating ? QStringLiteral("detach") : QStringLiteral("window"), theme));
            retained->setProperty("windowListIconTokens", iconTokens);
        }
        retained->setChecked(manager_->context().window == id);
        retained->setProperty("windowSystemWindowDepth", floating ? 1 : 0);
        desiredTail.append(retained);
    };
    QHash<FamilyId, QVector<WindowId>> children;
    for (const auto id : order) {
        const auto* record = manager_->model().window(id);
        if (record && record->kind == WindowKind::Floating) children[record->familyId].append(id);
    }
    int familyIndex = 0;
    for (const auto id : order) {
        const auto* record = manager_->model().window(id);
        if (!record || record->kind != WindowKind::Workbench) continue;
        const auto family = record->familyId;
        ++familyIndex;
        append(id, QStringLiteral("%1  ").arg(familyIndex), false);
        int childIndex = 0;
        for (const auto child : children.value(family))
            append(child, QStringLiteral("    %1.%2  ").arg(familyIndex).arg(++childIndex), true);
    }
    QList<QAction*> currentTail;
    const auto existing = menu->actions();
    for (auto* action : existing)
        if (action == windowListSeparator_ || action->objectName() == QStringLiteral("WindowListItem")) currentTail.append(action);
    // Moving the same actions on every nested aboutToShow invalidates native
    // parent-menu geometry. Only actual window/order changes touch the list.
    if (currentTail != desiredTail || existing.mid(existing.size() - desiredTail.size()) != desiredTail) {
        for (auto* action : currentTail) menu->removeAction(action);
        for (auto* action : desiredTail) menu->addAction(action);
    }
}
void ApplicationCommandMenus::refresh()
{
    if (!bar_) return;
    if (!manager_) {
        for (const auto& action : actions_) if (action) refreshCommandAction(nullptr, action, {});
        for (const auto& action : windowActions_) if (action) { action->setEnabled(false); action->setVisible(false); }
        if (windowListSeparator_) windowListSeparator_->setVisible(false);
        return;
    }
    const QPointer<ApplicationCommandMenus> retained(this);
    const auto previousCount = actions_.size();
    const bool removed = removeUnusedContributions();
    if (!retained || !manager_ || !bar_) return;
    addContributions(); if (!retained || !manager_ || !bar_) return;
    const auto context = manager_->context(window_);
    bool availabilityChanged = false;
    const auto actions = actions_;
    for (const auto& action : actions) {
        if (action) refreshCommandAction(manager_, action, context);
        if (!retained || !manager_ || !bar_) return;
    }
    refreshWindowList();
    for (auto* rootAction : bar_->actions()) if (auto* menu = rootAction->menu()) {
        tidy(menu); const auto rootActions = menu->actions();
        const bool available = std::any_of(rootActions.cbegin(), rootActions.cend(),
            [](QAction* action) { return !action->isSeparator() && action->isVisible(); });
        availabilityChanged |= rootAction->property("commandMenuAvailable").toBool() != available;
        rootAction->setProperty("commandMenuAvailable", available);
        if (!available) rootAction->setVisible(false);
    }
    if ((removed || previousCount != actions_.size() || availabilityChanged) && structureChanged) structureChanged();
}

} // namespace window_system
