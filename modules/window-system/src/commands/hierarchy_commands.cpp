// SPDX-License-Identifier: MIT
#include "window_manager_p.hpp"
#include "commands/popup_menu.hpp"
#include <QCoreApplication>
#include <QAction>
#include <QCursor>
#include <QMenu>
#include <cmath>

namespace window_system {
namespace {
std::optional<Placement> direction(const QVariantMap &args) {
    if (!args.contains("placement"))
        return Placement::Right;
    const auto value = args.value("placement");
    bool ok = false;
    const auto number = value.toDouble(&ok);
    if (!ok || value.metaType().id() == QMetaType::Bool || value.metaType().id() == QMetaType::QString ||
        !std::isfinite(number) || std::trunc(number) != number || number < int(Placement::Left) ||
        number > int(Placement::Bottom))
        return {};
    return Placement(int(number));
}
} // namespace
void WindowManager::registerHierarchyCommands() {
    const auto add = [this](QString id, const char *title, std::function<bool(const CommandContext &)> run,
                            std::function<bool(const CommandContext &)> enabled) {
        d->commands.add({std::move(id),
                         QCoreApplication::translate("WindowSystem", title),
                         QStringLiteral("布局"),
                         {},
                         std::move(enabled),
                         std::move(run),
                         {QStringLiteral("布局"), QStringLiteral("层级转换")}});
    };
    const auto visibleGroup = [this](GroupId id) {
        const auto *group = d->model.group(id);
        return group && group->location != GroupLocation::AutoHide;
    };
    const auto transfer = [this](const CommandContext &context, bool wholeGroup) {
        const auto place = direction(context.args);
        if (!place)
            return false;
        if (context.args.contains("targetTab")) {
            const auto target = context.args.value("targetTab").toUuid();
            const auto pane = context.args.value("targetPane").toUuid();
            if (context.args.contains("targetPane") && pane.isNull())
                return false;
            return wholeGroup ? moveGroupIntoTab(context.group, target, pane, *place)
                              : moveTabIntoTab(context.tab, target, pane, *place);
        }
        auto *menu = new DismissOnlyMenu(windowWidget(context.window));
        menu->setObjectName("HierarchyTargetPicker");
        menu->setAttribute(Qt::WA_DeleteOnClose);
        applyTheme(menu, theme());
        const QPointer<WindowManager> self(this);
        const QStringList directions{QCoreApplication::translate("WindowSystem", "Left of target pane"),
                                     QCoreApplication::translate("WindowSystem", "Right of target pane"),
                                     QCoreApplication::translate("WindowSystem", "Above target pane"),
                                     QCoreApplication::translate("WindowSystem", "Below target pane")};
        for (const auto window : d->model.windowOrder()) {
            const auto *widget = windowWidget(window);
            auto *windows = menu->addMenu(widget ? widget->windowTitle()
                                                 : QCoreApplication::translate("WindowSystem", "Window"));
            auto groups = d->model.centralGroups(window);
            groups += d->model.window(window)->edgeGroups;
            for (const auto id : groups) {
                const auto *group = d->model.group(id);
                if (!group || (wholeGroup && id == context.group))
                    continue;
                auto *groupMenu = windows->addMenu(QCoreApplication::translate("WindowSystem", "Tab group: %1")
                                                       .arg(d->model.tab(group->activeTab)->title));
                for (const auto tab : group->tabs) {
                    if (tab == context.tab && !wholeGroup)
                        continue;
                    auto *tabMenu = groupMenu->addMenu(d->model.tab(tab)->title);
                    const auto panes = d->model.panes(tab);
                    for (int paneIndex = 0; paneIndex < panes.size(); ++paneIndex) {
                        const auto targetPane = panes[paneIndex];
                        auto *paneMenu = tabMenu;
                        if (panes.size() > 1) {
                            const auto title = d->descriptors.value(targetPane).title;
                            paneMenu = tabMenu->addMenu(
                                QCoreApplication::translate("WindowSystem", "Pane %1: %2")
                                    .arg(paneIndex + 1)
                                    .arg(title.isEmpty() ? d->model.tab(tab)->title : title));
                            paneMenu->menuAction()->setProperty("targetPane", targetPane);
                        }
                        for (int index = 0; index < 4; ++index) {
                            auto *action = paneMenu->addAction(directions[index]);
                            action->setProperty("targetTab", tab);
                            action->setProperty("targetPane", targetPane);
                            action->setProperty("placement", index + int(Placement::Left));
                            QObject::connect(
                                action, &QAction::triggered, this,
                                [self, context, wholeGroup, tab, targetPane, index] {
                                    if (!self)
                                        return;
                                    const auto placement = Placement(index + int(Placement::Left));
                                    if (wholeGroup)
                                        self->moveGroupIntoTab(context.group, tab, targetPane, placement);
                                    else
                                        self->moveTabIntoTab(context.tab, tab, targetPane, placement);
                                });
                        }
                    }
                }
                if (groupMenu->isEmpty()) {
                    windows->removeAction(groupMenu->menuAction());
                    delete groupMenu;
                }
            }
            if (windows->isEmpty()) {
                menu->removeAction(windows->menuAction());
                delete windows;
            }
        }
        if (menu->isEmpty()) {
            delete menu;
            return false;
        }
        menu->popup(QCursor::pos());
        return true;
    };
    add(
        "group.convertToTab", QT_TRANSLATE_NOOP("WindowSystem", "Convert current tab group to a multi-pane tab"),
        [this](const CommandContext &c) {
            const auto place = direction(c.args);
            return place && !convertGroupToTab(c.group, *place).isNull();
        },
        [this, visibleGroup](const CommandContext &c) {
            return visibleGroup(c.group) && d->model.group(c.group)->tabs.size() > 1;
        });
    add(
        "group.moveIntoTab", QT_TRANSLATE_NOOP("WindowSystem", "Merge current tab group into a selected tab's panes…"),
        [transfer](const CommandContext &c) { return transfer(c, true); },
        [this, visibleGroup](const CommandContext &c) {
            if (!visibleGroup(c.group))
                return false;
            for (const auto &tab : d->model.tabs())
                if (d->model.groupForTab(tab.id) != c.group)
                    return true;
            return false;
        });
    add(
        "tab.moveIntoTab", QT_TRANSLATE_NOOP("WindowSystem", "Merge current tab into another tab's panes…"),
        [transfer](const CommandContext &c) { return transfer(c, false); },
        [this](const CommandContext &c) { return d->model.tab(c.tab) && d->model.tabs().size() > 1; });
    add(
        "tab.convertToGroup", QT_TRANSLATE_NOOP("WindowSystem", "Convert current tab's panes to a separate tab group"),
        [this](const CommandContext &c) {
            const auto place = direction(c.args);
            return place && !convertTabToGroup(c.tab, *place).isNull();
        },
        [this, visibleGroup](const CommandContext &c) {
            return visibleGroup(c.group) && d->model.panes(c.tab).size() > 1;
        });
    add(
        "pane.extractToTab", QT_TRANSLATE_NOOP("WindowSystem", "Extract current pane as a separate tab in this group"),
        [this](const CommandContext &c) { return !extractPaneToTab(c.tab, c.pane).isNull(); },
        [this](const CommandContext &c) {
            return d->model.panes(c.tab).contains(c.pane) && d->model.panes(c.tab).size() > 1;
        });
}
} // namespace window_system
