// SPDX-License-Identifier: MIT
#include "command_menus.hpp"
#include "menu_description.hpp"
#include "popup_menu.hpp"
#include <window_system/window_manager.hpp>

#include <QAction>
#include <QHash>
#include <QMenu>
#include <QMenuBar>
#include <QSignalBlocker>
#include <QTimer>
#include <QVariant>
#include <algorithm>

namespace window_system {
namespace {

const auto scrollableMenuStyle = QStringLiteral("QMenu { menu-scrollable: 1; }");

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

} // namespace

void refreshCommandAction(WindowManager* manager, QAction* action, const CommandContext& context)
{
    if (!action) return;
    const QPointer<QAction> retained(action);
    const QPointer<WindowManager> owner(manager);
    const auto description = describeCommandAction(manager, action->data().toString(), context);
    if (!retained || (manager && !owner)) return;
    if (!description) { action->setVisible(false); action->setEnabled(false); return; }
    const QSignalBlocker blocker(action);
    action->setCheckable(description->checkable);
    if (description->checked) action->setChecked(*description->checked);
    const auto& shortcut = description->shortcut;
    action->setText(description->title + (!action->property("stripCommand").toBool() && !shortcut.isEmpty()
        ? "\t" + shortcut : QString()));
    action->setToolTip(description->title + (shortcut.isEmpty() ? QString() : "  " + shortcut));
    action->setVisible(description->visible);
    action->setEnabled(description->enabled);
}

namespace {

void appendContextItems(QMenu* menu, const QVector<MenuItemDescription>& items, WindowManager* manager,
                        const CommandContext& context, QList<QPointer<QAction>>& commands)
{
    for (const auto& item : items) {
        QAction* action = nullptr;
        if (item.kind == MenuItemDescription::Kind::Separator) action = menu->addSeparator();
        else if (item.kind == MenuItemDescription::Kind::Submenu) {
            auto* child = new DismissOnlyMenu(item.menuTitle.isEmpty() ? item.action.title : item.menuTitle, menu);
            child->setObjectName(item.menuId.isEmpty() ? item.id : item.menuId);
            child->setProperty("commandMenuPath", item.path);
            child->setStyleSheet(scrollableMenuStyle);
            action = menu->addMenu(child);
            appendContextItems(child, item.children, manager, context, commands);
        } else action = menu->addAction(item.action.title);
        action->setObjectName(item.id);
        action->setText(item.action.title);
        action->setProperty("menuOrder", item.order);
        action->setProperty("menuSortTitle", item.sortTitle);
        action->setVisible(item.action.visible);
        action->setEnabled(item.action.enabled);
        action->setCheckable(item.action.checkable);
        if (item.action.checked) action->setChecked(*item.action.checked);
        if (item.commandId.isEmpty()) continue;
        if (item.kind == MenuItemDescription::Kind::TabColor) {
            action->setData(item.id);
            QObject::connect(action, &QAction::triggered, menu,
                [owner = QPointer<WindowManager>(manager), context, args = item.arguments, id = item.commandId] {
                    if (!owner) return;
                    auto captured = context;
                    for (auto it = args.cbegin(); it != args.cend(); ++it) captured.args.insert(it.key(), it.value());
                    owner->executeInContext(id, captured);
                });
        } else {
            action->setData(item.commandId);
            action->setProperty("windowSystemCommand", item.commandId);
            commands.append(action);
            const auto& shortcut = item.action.shortcut;
            action->setText(item.action.title + (shortcut.isEmpty() ? QString() : "\t" + shortcut));
            action->setToolTip(item.action.title + (shortcut.isEmpty() ? QString() : "  " + shortcut));
            QObject::connect(action, &QAction::triggered, manager,
                [owner = QPointer<WindowManager>(manager), context, id = item.commandId] {
                    if (!owner) return;
                    QTimer::singleShot(0, owner, [owner, id, context] {
                        if (owner) owner->executeInContext(id, context);
                    });
                });
        }
    }
}

} // namespace

void populateContextMenu(QMenu* menu, CommandMenuSurface surface, WindowManager* manager, const CommandContext& captured)
{
    const QPointer<QMenu> retainedMenu(menu);
    const QPointer<WindowManager> retainedManager(manager);
    const auto description = describeContextMenu(surface, manager, captured);
    if (!retainedMenu || !retainedManager) return;
    if (!menu->actions().isEmpty()) menu->addSeparator();
    QList<QPointer<QAction>> actions;
    appendContextItems(menu, description, manager, captured, actions);
    tidy(menu);
    for (auto* child : menu->findChildren<QMenu*>()) QObject::connect(child, &QMenu::aboutToShow, menu,
        [owner = retainedManager, actions, captured, root = retainedMenu] {
            for (const auto& action : actions) {
                if (action) refreshCommandAction(owner, action, captured);
                if (!root || !owner) return;
            }
            tidy(root);
        });
}

ApplicationCommandMenus::ApplicationCommandMenus(QMenuBar* bar, WindowManager* manager, WindowId window,
                                               CommandMenuSurface surface)
    : QObject(bar), bar_(bar), manager_(manager), window_(window), surface_(surface)
{
    refresh();
}

QAction* ApplicationCommandMenus::createAction(QWidget* owner, const MenuItemDescription& item)
{
    QPointer<QAction> retained;
    if (item.kind == MenuItemDescription::Kind::Submenu) {
        auto* child = new DismissOnlyMenu(item.action.title, owner);
        child->setObjectName(item.id);
        child->setStyleSheet(scrollableMenuStyle);
        child->setProperty("commandMenuPath", item.path);
        child->setProperty("commandMenuContribution", item.contribution);
        connect(child, &QMenu::aboutToShow, this, [this] { refresh(); });
        retained = child->menuAction();
    } else {
        retained = new QAction(owner);
        retained->setSeparator(item.kind == MenuItemDescription::Kind::Separator);
        if (!item.commandId.isEmpty()) {
            if (item.kind == MenuItemDescription::Kind::Window) {
                connect(retained, &QAction::triggered, this,
                    [manager = manager_, source = window_, args = item.arguments, id = item.commandId] {
                        if (!manager || !manager->model().window(args.value(QStringLiteral("targetWindow")).toUuid())) return;
                        QTimer::singleShot(0, manager, [manager, source, args, id] {
                            if (manager) manager->execute(id, args, source);
                        });
                    });
            } else {
                connect(retained, &QAction::triggered, this,
                    [manager = manager_, source = window_, id = item.commandId, receiver = QPointer(this)] {
                        if (!manager || !receiver) return;
                        const auto context = manager->context(source);
                        QTimer::singleShot(0, receiver, [manager, id, context] {
                            if (manager) manager->executeInContext(id, context);
                        });
                    });
            }
        }
    }
    retained->setObjectName(item.kind == MenuItemDescription::Kind::Window ? QStringLiteral("WindowListItem") : item.id);
    return retained;
}

bool ApplicationCommandMenus::updateAction(QAction* action, const MenuItemDescription& item, bool root)
{
    bool availabilityChanged = false;
    action->setProperty("menuOrder", item.order);
    action->setProperty("menuSortTitle", item.sortTitle);
    action->setProperty("commandMenuContribution", item.contribution);
    const QSignalBlocker blocker(action);
    action->setText(item.action.title + (item.action.shortcut.isEmpty() ? QString() : "\t" + item.action.shortcut));
    action->setToolTip(item.tooltip.isEmpty()
        ? item.action.title + (item.action.shortcut.isEmpty() ? QString() : "  " + item.action.shortcut) : item.tooltip);
    action->setEnabled(item.action.enabled);
    action->setCheckable(item.action.checkable);
    if (item.action.checked) action->setChecked(*item.action.checked);
    if (root) {
        const bool available = item.action.visible;
        availabilityChanged = action->property("commandMenuAvailable").toBool() != available;
        action->setProperty("commandMenuAvailable", available);
        if (!available) action->setVisible(false);
    } else action->setVisible(item.action.visible);
    if (item.kind == MenuItemDescription::Kind::Window) {
        action->setProperty("windowSystemWindow", item.arguments.value(QStringLiteral("targetWindow")));
        action->setProperty("windowSystemWindowDepth", item.depth);
        const auto theme = manager_->interfaceTheme();
        const QVariantList tokens{item.iconName, theme.text, theme.mutedText, theme.selectionText, theme.accent};
        if (action->property("windowListIconTokens").toList() != tokens) {
            action->setIcon(iconFor(item.iconName, theme));
            action->setProperty("windowListIconTokens", tokens);
        }
    } else if (!item.commandId.isEmpty()) {
        action->setData(item.commandId);
        action->setProperty("windowSystemCommand", item.commandId);
    }
    return availabilityChanged;
}

bool ApplicationCommandMenus::syncMenu(QWidget* owner, const QVector<MenuItemDescription>& items)
{
    const QPointer<ApplicationCommandMenus> self(this);
    const QPointer<QWidget> retainedOwner(owner);
    QHash<QString, QPointer<QAction>> existing;
    for (auto* action : owner->actions()) {
        const auto key = action->property("menuDescriptionKey").toString();
        if (!key.isEmpty()) existing.insert(key, action);
    }
    QList<QAction*> desired;
    bool changed = false;
    const bool root = owner == bar_;
    int separator = 0;
    for (const auto& item : items) {
        const auto key = item.kind == MenuItemDescription::Kind::Separator && item.id.isEmpty()
            ? QStringLiteral("separator.%1").arg(++separator) : QString::number(int(item.kind)) + ':' + item.id;
        auto retained = existing.take(key);
        if (!retained) {
            retained = createAction(owner, item);
            retained->setProperty("menuDescriptionKey", key);
            changed = true;
        }
        auto* action = retained.data();
        changed |= updateAction(action, item, root);
        if (auto* child = action->menu()) changed |= syncMenu(child, item.children);
        if (!self || !retainedOwner || !retained) return changed;
        desired.append(action);
    }
    for (const auto& action : existing) if (action) {
        owner->removeAction(action);
        if (auto* menu = action->menu()) delete menu;
        else delete action.data();
        if (!self || !retainedOwner) return true;
        changed = true;
    }
    QList<QAction*> current;
    for (auto* action : owner->actions()) if (!action->property("menuDescriptionKey").toString().isEmpty()) current.append(action);
    // Reusing actions is insufficient: removing and adding unchanged items
    // also invalidates native popup geometry while a submenu is opening.
    if (current != desired) {
        for (auto* action : current) owner->removeAction(action);
        for (auto* action : desired) owner->addAction(action);
        changed = true;
    }
    return changed;
}

void ApplicationCommandMenus::refresh()
{
    if (!bar_) return;
    if (!manager_) {
        for (auto* action : bar_->findChildren<QAction*>()) { action->setEnabled(false); action->setVisible(false); }
        return;
    }
    const QPointer<ApplicationCommandMenus> retained(this);
    const auto description = describeApplicationMenu(surface_, manager_, window_);
    if (!retained || !manager_ || !bar_) return;
    const bool changed = syncMenu(bar_, description);
    if (!retained) return;
    const auto callback = structureChanged;
    if (changed && callback) callback();
}

} // namespace window_system
