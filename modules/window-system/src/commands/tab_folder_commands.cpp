// SPDX-License-Identifier: MIT
#include <QCoreApplication>
#include "window_manager_p.hpp"
#include "commands/popup_menu.hpp"
#include "commands/command_menus.hpp"
#include "commands/tab_presentation_commands.hpp"
#include <QCursor>
#include <QInputDialog>
#include <QPointer>

namespace window_system {
// Tab folder commands (tab strip design 12). Each acts on the folder named by
// the "folder" argument, or else on the context tab's folder.
void WindowManager::registerTabFolderCommands()
{
    const auto add = [this](QString id, QString title, std::function<bool(const CommandContext&)> run,
                            std::function<bool(const CommandContext&)> enabled) {
        d->commands.add({std::move(id), std::move(title), QStringLiteral("layout"), {}, std::move(enabled), std::move(run),
                         {QStringLiteral("layout"), QStringLiteral("tabFolder")}});
    };
    const auto folderOf = [this](const CommandContext& c) -> FolderId {
        if (c.args.contains("folder")) return c.args.value("folder").toUuid();
        const auto* tab = d->model.tab(c.tab);
        return tab ? tab->folder : FolderId{};
    };
    const auto inFolder = [this, folderOf](const CommandContext& c) { return d->model.folder(folderOf(c)) != nullptr; };
    // A tab that may join a folder: in a visible strip and not sticky.
    const auto foldable = [this](const CommandContext& c) {
        const auto* group = d->model.group(d->model.groupForTab(c.tab));
        return group && group->location != GroupLocation::AutoHide && !d->model.isTabSticky(c.tab);
    };
    const auto groupOf = [this](const CommandContext& c) {
        return c.group.isNull() ? d->model.groupForTab(c.tab) : c.group;
    };
    const auto changeFolder = [this, folderOf](const CommandContext& c, const std::function<void(TabFolderRecord&)>& change) {
        const auto* record = d->model.folder(folderOf(c));
        if (!record) return false;
        auto changed = *record; change(changed);
        return updateTabFolder(changed);
    };
    // Picks one of the strip's folders other than `except`.
    const auto chooseFolder = [this](GroupId groupId, FolderId except) -> std::optional<FolderId> {
        const auto* group = d->model.group(groupId);
        if (!group) return std::nullopt;
        DismissOnlyMenu menu; menu.setObjectName(QStringLiteral("TabFolderPicker")); applyTheme(&menu, theme());
        for (const auto& folder : group->folders) if (folder.id != except)
            menu.addAction(tabFolderDisplayName(folder.id))->setData(folder.id);
        if (menu.isEmpty()) return std::nullopt;
        const QPointer<WindowManager> self(this);
        const auto* selected = menu.exec(QCursor::pos());
        return self && selected ? std::optional<FolderId>(selected->data().toUuid()) : std::nullopt;
    };

    // Making and editing.
    add("tabFolder.new", QCoreApplication::translate("WindowSystem", "Add tab to new tab folder"), [this](const CommandContext& c) {
        QVector<TabId> tabs;
        if (c.args.contains("tabs")) for (const auto& value : c.args.value("tabs").toList()) tabs.append(value.toUuid());
        else tabs.append(c.tab);
        return !createTabFolder(tabs, c.args.value("name").toString(), c.args.value("color").toString()).isNull();
    }, foldable);
    add("tabFolder.addTab", QCoreApplication::translate("WindowSystem", "Add tab to a tab folder…"), [this, chooseFolder](const CommandContext& c) {
        const auto* tab = d->model.tab(c.tab);
        if (!tab) return false;
        if (c.args.contains("targetFolder")) return addTabToFolder(c.tab, c.args.value("targetFolder").toUuid());
        const QPointer<WindowManager> self(this);
        const auto target = chooseFolder(d->model.groupForTab(c.tab), tab->folder);
        return self && target && addTabToFolder(c.tab, *target);
    }, [this, foldable](const CommandContext& c) {
        const auto* group = d->model.group(d->model.groupForTab(c.tab));
        const auto* tab = d->model.tab(c.tab);
        return foldable(c) && group && tab && group->folders.size() > (tab->folder.isNull() ? 0 : 1);
    });
    add("tabFolder.removeTab", QCoreApplication::translate("WindowSystem", "Remove tab from its tab folder"),
        [this](const CommandContext& c) { return removeTabFromFolder(c.tab); },
        [this](const CommandContext& c) { const auto* tab = d->model.tab(c.tab); return tab && !tab->folder.isNull(); });
    add("tabFolder.rename", QCoreApplication::translate("WindowSystem", "Rename tab folder…"), [this, folderOf, changeFolder](const CommandContext& c) {
        const auto* record = d->model.folder(folderOf(c));
        if (!record) return false;
        if (c.args.contains("name")) {
            if (c.args.value("name").metaType().id() != QMetaType::QString) return false;
            const auto name = c.args.value("name").toString();
            return changeFolder(c, [&](TabFolderRecord& folder) { folder.name = name; });
        }
        const QPointer<WindowManager> self(this);
        bool accepted = false;
        const auto name = QInputDialog::getText(windowWidget(c.window), QCoreApplication::translate("WindowSystem", "Rename tab folder…"),
            QCoreApplication::translate("WindowSystem", "Tab folder name (empty for none)"), QLineEdit::Normal, record->name, &accepted);
        if (!self) return false;
        return !accepted || changeFolder(c, [&](TabFolderRecord& folder) { folder.name = name; });
    }, inFolder);
    add("tabFolder.setColor", QCoreApplication::translate("WindowSystem", "Set tab folder color…"), [this, changeFolder](const CommandContext& c) {
        std::optional<QString> color;
        if (c.args.contains("color")) color = c.args.value("color").toString();
        else {
            const QPointer<WindowManager> self(this);
            color = pickTabColor(this, QCoreApplication::translate("WindowSystem", "Automatic color"));
            if (!self) return false;
        }
        return color && changeFolder(c, [&](TabFolderRecord& folder) { folder.colorId = *color; });
    }, inFolder);
    add("tabFolder.toggleCollapsed", QCoreApplication::translate("WindowSystem", "Collapse or expand tab folder"), [changeFolder](const CommandContext& c) {
        return changeFolder(c, [](TabFolderRecord& folder) { folder.collapsed = !folder.collapsed; });
    }, inFolder);
    for (const bool collapse : {true, false})
        add(collapse ? "tabFolder.collapseAll" : "tabFolder.expandAll",
            collapse ? QCoreApplication::translate("WindowSystem", "Collapse all tab folders in this tab group")
                     : QCoreApplication::translate("WindowSystem", "Expand all tab folders in this tab group"),
            [this, groupOf, collapse](const CommandContext& c) {
                const auto* group = d->model.group(groupOf(c));
                if (!group) return false;
                for (auto folder : QVector<TabFolderRecord>(group->folders)) {
                    folder.collapsed = collapse;
                    if (!updateTabFolder(folder)) return false;
                }
                return true;
            }, [this, groupOf](const CommandContext& c) { const auto* group = d->model.group(groupOf(c)); return group && !group->folders.isEmpty(); });
    add("tabFolder.newTab", QCoreApplication::translate("WindowSystem", "New tab in this tab folder"), [this, folderOf](const CommandContext& c) {
        const auto folder = folderOf(c);
        const auto* group = d->model.group(d->model.groupForFolder(folder));
        return group && requestNewTab(group->windowId, group->id, {}, folder);
    }, inFolder);
    add("tabFolder.ungroup", QCoreApplication::translate("WindowSystem", "Ungroup tab folder"),
        [this, folderOf](const CommandContext& c) { return ungroupTabFolder(folderOf(c)); }, inFolder);
    add("tabFolder.close", QCoreApplication::translate("WindowSystem", "Close tab folder"),
        [this, folderOf](const CommandContext& c) { return closeTabFolder(folderOf(c)); }, inFolder);

    // Moving within the strip, between groups and windows.
    for (const bool forward : {false, true})
        add(forward ? "tabFolder.moveForward" : "tabFolder.moveBackward",
            forward ? QCoreApplication::translate("WindowSystem", "Move tab folder forward") : QCoreApplication::translate("WindowSystem", "Move tab folder backward"),
            [this, folderOf, forward](const CommandContext& c) { return moveTabFolder(folderOf(c), forward ? 1 : -1); }, inFolder);
    struct Direction { const char* id; Placement direction; const char* move; const char* convert; };
    static constexpr Direction directions[] = {
        {"left", Placement::Left, QT_TRANSLATE_NOOP("WindowSystem", "Move tab folder into the tab group to the left"), QT_TRANSLATE_NOOP("WindowSystem", "Convert tab folder to a tab group on the left")},
        {"right", Placement::Right, QT_TRANSLATE_NOOP("WindowSystem", "Move tab folder into the tab group to the right"), QT_TRANSLATE_NOOP("WindowSystem", "Convert tab folder to a tab group on the right")},
        {"up", Placement::Top, QT_TRANSLATE_NOOP("WindowSystem", "Move tab folder into the tab group above"), QT_TRANSLATE_NOOP("WindowSystem", "Convert tab folder to a tab group above")},
        {"down", Placement::Bottom, QT_TRANSLATE_NOOP("WindowSystem", "Move tab folder into the tab group below"), QT_TRANSLATE_NOOP("WindowSystem", "Convert tab folder to a tab group below")}};
    static constexpr const char* intoGroup[] = {
        QT_TRANSLATE_NOOP("WindowSystem", "Convert tab group to a tab folder of the group to the left"), QT_TRANSLATE_NOOP("WindowSystem", "Convert tab group to a tab folder of the group to the right"),
        QT_TRANSLATE_NOOP("WindowSystem", "Convert tab group to a tab folder of the group above"), QT_TRANSLATE_NOOP("WindowSystem", "Convert tab group to a tab folder of the group below")};
    for (int index = 0; index < 4; ++index) {
        const auto& direction = directions[index];
        // The adjacent group is found from the folder's first tab, as for tabs.
        const auto target = [this, folderOf, direction](const CommandContext& c) {
            const auto tabs = d->model.folderTabs(folderOf(c));
            return tabs.isEmpty() ? GroupId{} : adjacentGroup(tabs.first(), direction.direction);
        };
        add(QStringLiteral("tabFolder.move.") + direction.id, QCoreApplication::translate("WindowSystem", direction.move),
            [this, folderOf, target](const CommandContext& c) { const auto group = target(c); return !group.isNull() && moveTabFolderToGroup(folderOf(c), group); },
            [target](const CommandContext& c) { return !target(c).isNull(); });
        add(QStringLiteral("tabFolder.convertToGroup.") + direction.id, QCoreApplication::translate("WindowSystem", direction.convert),
            [this, folderOf, direction](const CommandContext& c) { return !convertTabFolderToGroup(folderOf(c), direction.direction).isNull(); }, inFolder);
        add(QStringLiteral("group.convertToFolder.") + direction.id, QCoreApplication::translate("WindowSystem", intoGroup[index]),
            [this, groupOf, direction](const CommandContext& c) {
                const auto group = adjacentVisibleGroup(groupOf(c), direction.direction);
                return !group.isNull() && !convertGroupToFolder(groupOf(c), group).isNull();
            }, [this, groupOf, direction](const CommandContext& c) { return !adjacentVisibleGroup(groupOf(c), direction.direction).isNull(); });
    }
    const auto hasWindow = [this, inFolder](const CommandContext& c) { return inFolder(c) && d->model.windowOrder().size() > 1; };
    for (const bool forward : {false, true})
        add(forward ? "tabFolder.moveWindow.next" : "tabFolder.moveWindow.previous",
            forward ? QCoreApplication::translate("WindowSystem", "Move tab folder to next window") : QCoreApplication::translate("WindowSystem", "Move tab folder to previous window"),
            [this, folderOf, forward](const CommandContext& c) {
                const auto* group = d->model.group(d->model.groupForFolder(folderOf(c)));
                const auto target = group ? adjacentWindow(group->windowId, forward) : WindowId{};
                return !target.isNull() && moveTabFolderToWindow(folderOf(c), target);
            }, hasWindow);
    add("tabFolder.moveWindow", QCoreApplication::translate("WindowSystem", "Move tab folder to a selected window…"), [this, folderOf](const CommandContext& c) {
        if (c.args.contains("targetWindow")) return moveTabFolderToWindow(folderOf(c), c.args.value("targetWindow").toUuid());
        const auto* group = d->model.group(d->model.groupForFolder(folderOf(c)));
        if (!group) return false;
        DismissOnlyMenu menu; applyTheme(&menu, theme());
        for (const auto id : d->model.windowOrder()) if (id != group->windowId) menu.addAction(windowWidget(id)->windowTitle())->setData(id);
        const QPointer<WindowManager> self(this); const auto* action = menu.exec(QCursor::pos());
        return self && action && moveTabFolderToWindow(folderOf(c), action->data().toUuid());
    }, hasWindow);
    for (const bool floating : {true, false})
        add(floating ? "tabFolder.detachFloating" : "tabFolder.detachWorkbench",
            floating ? QCoreApplication::translate("WindowSystem", "Move tab folder to a new floating window") : QCoreApplication::translate("WindowSystem", "Move tab folder to a new workbench window"),
            [this, folderOf, floating](const CommandContext& c) { return !detachTabFolder(folderOf(c), floating ? WindowKind::Floating : WindowKind::Workbench).isNull(); },
            inFolder);
    const auto hasOther = [this, folderOf](const CommandContext& c) { return !adjacentTabFolder(folderOf(c), true).isNull(); };
    for (const bool forward : {false, true})
        add(forward ? "tabFolder.merge.next" : "tabFolder.merge.previous",
            forward ? QCoreApplication::translate("WindowSystem", "Merge into the next tab folder") : QCoreApplication::translate("WindowSystem", "Merge into the previous tab folder"),
            [this, folderOf, forward](const CommandContext& c) {
                const auto target = adjacentTabFolder(folderOf(c), forward);
                return !target.isNull() && mergeTabFolders(folderOf(c), target);
            }, hasOther);
    add("tabFolder.merge", QCoreApplication::translate("WindowSystem", "Merge into a selected tab folder…"), [this, folderOf, chooseFolder](const CommandContext& c) {
        if (c.args.contains("targetFolder")) return mergeTabFolders(folderOf(c), c.args.value("targetFolder").toUuid());
        const QPointer<WindowManager> self(this);
        const auto target = chooseFolder(d->model.groupForFolder(folderOf(c)), folderOf(c));
        return self && target && mergeTabFolders(folderOf(c), *target);
    }, hasOther);
    // Switching: the first tab of the next or previous folder, expanded.
    for (const bool forward : {false, true})
        add(forward ? "tabFolder.focus.next" : "tabFolder.focus.previous",
            forward ? QCoreApplication::translate("WindowSystem", "Focus next tab folder") : QCoreApplication::translate("WindowSystem", "Focus previous tab folder"),
            [this, folderOf, groupOf, forward](const CommandContext& c) {
                auto target = folderOf(c);
                if (!target.isNull()) target = adjacentTabFolder(target, forward);
                else if (const auto* group = d->model.group(groupOf(c)); group && !group->folders.isEmpty()) {
                    // From a tab outside any folder: the nearest folder that way.
                    const int at = int(group->tabs.indexOf(c.tab));
                    for (int step = 1; step <= group->tabs.size() && target.isNull(); ++step) {
                        const int index = (at + (forward ? step : -step) + int(group->tabs.size()) * 2) % int(group->tabs.size());
                        target = d->model.tab(group->tabs.at(index))->folder;
                    }
                }
                const auto* record = d->model.folder(target);
                if (!record) return false;
                if (record->collapsed) { auto expanded = *record; expanded.collapsed = false; if (!updateTabFolder(expanded)) return false; }
                const auto tabs = d->model.folderTabs(target);
                return !tabs.isEmpty() && activateTab(tabs.first());
            }, [this, groupOf](const CommandContext& c) { const auto* group = d->model.group(groupOf(c)); return group && !group->folders.isEmpty(); });
    add("group.convertToFolder", QCoreApplication::translate("WindowSystem", "Convert tab group to a tab folder of a selected group…"), [this, groupOf](const CommandContext& c) {
        const auto source = groupOf(c);
        if (c.args.contains("targetGroup")) return !convertGroupToFolder(source, c.args.value("targetGroup").toUuid()).isNull();
        DismissOnlyMenu menu; menu.setObjectName(QStringLiteral("GroupTargetPicker")); applyTheme(&menu, theme());
        for (const auto windowId : d->model.windowOrder())
            for (const auto id : d->model.centralGroups(windowId)) if (id != source) menu.addAction(groupDisplayName(id))->setData(id);
        const QPointer<WindowManager> self(this);
        const auto* selected = menu.exec(QCursor::pos());
        return self && selected && !convertGroupToFolder(source, selected->data().toUuid()).isNull();
    }, [this, groupOf](const CommandContext& c) {
        for (const auto& group : d->model.groups()) if (group.id != groupOf(c) && group.location != GroupLocation::AutoHide) return true;
        return false;
    });
}
QString WindowManager::tabFolderDisplayName(FolderId folder) const
{
    const auto* record = d->model.folder(folder);
    if (!record) return {};
    if (!record->name.isEmpty()) return record->name;
    const auto tabs = d->model.folderTabs(folder);
    return QCoreApplication::translate("WindowSystem", "Tab folder: %1 (%n tabs)", nullptr, int(tabs.size()))
        .arg(tabs.isEmpty() ? QString() : d->model.tab(tabs.first())->title);
}
void WindowManager::showTabFolderMenu(FolderId folder, QPoint position)
{
    const auto tabs = d->model.folderTabs(folder);
    const auto* record = d->model.group(d->model.groupForFolder(folder));
    if (tabs.isEmpty() || !record) return;
    auto captured = context(record->windowId); captured.group = record->id;
    // The current tab when it is in the folder, else the folder's first.
    captured.tab = tabs.contains(record->activeTab) ? record->activeTab : tabs.first();
    captured.pane = d->model.tab(captured.tab)->activePane;
    auto* menu = new DismissOnlyMenu(windowWidget(record->windowId)); menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setObjectName(QStringLiteral("TabFolderContextMenu")); applyTheme(menu, theme());
    populateContextMenu(menu, CommandMenuSurface::TabFolder, this, captured); menu->popup(position);
}
} // namespace window_system
