// SPDX-License-Identifier: MIT
#include "script_host_p.hpp"
#include <window_system/localization.hpp>
#include <QApplication>
#include <QJsonArray>
#include <QLabel>
#include <QWidget>

namespace window_system_lua {
using namespace window_system;
namespace {
QString edgeName(Edge e) {
    return e == Edge::Left ? "left" : e == Edge::Right ? "right" : e == Edge::Top ? "top" : "bottom";
}
QVariantMap windowValue(const WindowRecord &w) {
    return {{"id", idText(w.id)},
            {"family", idText(w.familyId)},
            {"kind", w.kind == WindowKind::Floating ? "floating" : "workbench"},
            {"active_group", idText(w.activeGroup)}};
}
QVariantMap groupValue(const GroupRecord &g) {
    QVariantList tabs;
    for (const auto id : g.tabs)
        tabs.append(idText(id));
    return {{"id", idText(g.id)},
            {"window", idText(g.windowId)},
            {"tabs", tabs},
            {"active_tab", idText(g.activeTab)},
            {"location", g.location == GroupLocation::Central  ? "central"
                         : g.location == GroupLocation::Pinned ? "pinned"
                                                               : "auto_hide"},
            {"edge", edgeName(g.edge)}};
}
QVariantMap tabValue(const TabRecord &t) {
    return {{"id", idText(t.id)},
            {"title", t.title},
            {"active_pane", idText(t.activePane)},
            {"color", t.colorId}};
}
QVariantMap visibilityValue(BarVisibility v) {
    return {{"tabs", v.tabBars}, {"sidebar", v.sideBars}, {"toolbars", v.toolbars}, {"status", v.statusBar}};
}
ViewMode viewMode(QString mode) {
    if (mode == "normal")
        return ViewMode::Normal;
    if (mode == "window")
        return ViewMode::WindowFullscreen;
    if (mode == "group")
        return ViewMode::GroupFullscreen;
    if (mode == "pane")
        return ViewMode::PaneFullscreen;
    throw std::runtime_error("Unknown fullscreen mode");
}
QString modeName(ViewMode mode) {
    switch (mode) {
    case ViewMode::Normal:
        return "normal";
    case ViewMode::WindowFullscreen:
        return "window";
    case ViewMode::GroupFullscreen:
        return "group";
    case ViewMode::PaneFullscreen:
        return "pane";
    }
    return {};
}
ToolbarArea toolbarArea(QString area) {
    if (area == "top")
        return ToolbarArea::Top;
    if (area == "bottom")
        return ToolbarArea::Bottom;
    throw std::runtime_error("Toolbar area must be top or bottom");
}
WindowKind windowKind(QString kind) {
    if (kind == "floating")
        return WindowKind::Floating;
    if (kind == "workbench")
        return WindowKind::Workbench;
    throw std::runtime_error("Window kind must be floating or workbench");
}
bool boolField(const QVariantMap &map, const QString &key, bool fallback) {
    return map.contains(key) ? boolean({map.value(key)}, 0) : fallback;
}
FloatingPanelOptions panelOptions(const Args &args) {
    const auto spec = object(args, 1);
    for (auto it = spec.cbegin(); it != spec.cend(); ++it)
        if (it.key() != "modal" && it.key() != "dismiss_on_outside")
            throw std::runtime_error("Unknown panel option");
    return {boolField(spec, "dismiss_on_outside", true), boolField(spec, "modal", false)};
}
} // namespace
void ScriptHost::Private::installWindowApi() {
    handlers["context"] = [this](const Args &, const Context &c) { return result(contextMap(current(c))); };
    handlers["commands.list"] = [this](const Args &, const Context &) {
        QVariantList items;
        for (const auto &command : manager->commands().commands()) {
            QVariantList bindings;
            for (const auto &b : command.bindings)
                bindings.append(
                    QVariantMap{{"key", b.sequence.toString(QKeySequence::PortableText)}, {"args", b.args}});
            items.append(QVariantMap{{"id", command.id},
                                     {"title", command.title},
                                     {"category", command.category},
                                     {"menu", command.menuPath},
                                     {"menu_order", command.menuOrder},
                                     {"bindings", bindings}});
        }
        return result(items);
    };
    handlers["commands.execute"] = [this](const Args &a, const Context &c) {
        auto target = a.size() > 2 ? captured(object(a, 2)) : current(c);
        target.args = object(a, 1);
        return result(manager->executeInContext(text(a, 0), target));
    };
    handlers["windows.list"] = [this](const Args &, const Context &) {
        QVariantList list;
        for (auto id : manager->model().windowOrder())
            list.append(windowValue(*manager->model().window(id)));
        return result(list);
    };
    handlers["windows.new"] = [this](const Args &a, const Context &c) {
        auto options = object(a, 0);
        WindowOptions window;
        window.title = text({options.value("title")}, 0, "Muxora");
        window.size = QSize(number(options.value("width"), 1280, 200, 65536),
                            number(options.value("height"), 800, 100, 65536));
        const auto kind = windowKind(text({options.value("kind")}, 0, "workbench"));
        WindowId parent;
        if (kind == WindowKind::Floating) {
            const auto source =
                options.contains("owner") ? identifier({options.value("owner")}, 0) : current(c).window;
            const auto *record = manager->model().window(source);
            if (!record)
                return failure("Floating window owner does not exist");
            parent = manager->model().familyRoot(record->familyId);
        }
        const QPointer<WindowManager> windows(manager);
        const QPointer<ScriptHost> owner(q);
        const auto id = windows->createWindow(kind, window, parent);
        if (!owner || !windows)
            return result(QString{});
        if (!id.isNull())
            windows->showWindow(id);
        return result(idText(id));
    };
    handlers["windows.close"] = [this](const Args &a, const Context &c) {
        return result(manager->requestCloseWindow(target(a, 0, c, Target::Window)));
    };
    handlers["windows.activate"] = [this](const Args &a, const Context &) {
        return result(manager->activateWindow(identifier(a, 0)));
    };
    handlers["windows.promote"] = [this](const Args &a, const Context &c) {
        return result(manager->promoteWindow(target(a, 0, c, Target::Window)));
    };
    handlers["windows.demote"] = [this](const Args &a, const Context &c) {
        return result(manager->demoteWindow(target(a, 0, c, Target::Window), identifier(a, 1)));
    };
    handlers["windows.set_owner"] = [this](const Args &a, const Context &) {
        return result(manager->reassignFamily(identifier(a, 0), identifier(a, 1)));
    };
    handlers["windows.set_visibility"] = [this](const Args &a, const Context &c) {
        auto id = target(a, 0, c, Target::Window);
        auto values = object(a, 1);
        for (const auto &key : values.keys())
            if (key != "tabs" && key != "sidebar" && key != "toolbars" && key != "status")
                throw std::runtime_error("Unknown visibility bar");
        for (auto it = values.cbegin(); it != values.cend(); ++it)
            boolean({it.value()}, 0);
        const QPointer<WindowManager> windows(manager);
        const QPointer<ScriptHost> owner(q);
        bool ok = true;
        if (values.contains("tabs"))
            ok = windows->setTabBarsVisible(id, values["tabs"].toBool()) && ok;
        if (!owner || !windows)
            return result(false);
        if (values.contains("sidebar"))
            ok = windows->setSideBarsVisible(id, values["sidebar"].toBool()) && ok;
        if (!owner || !windows)
            return result(false);
        if (values.contains("toolbars"))
            ok = windows->setToolbarsVisible(id, values["toolbars"].toBool()) && ok;
        if (!owner || !windows)
            return result(false);
        if (values.contains("status"))
            ok = windows->setStatusBarVisible(id, values["status"].toBool()) && ok;
        return result(ok);
    };
    handlers["windows.visibility"] = [this](const Args &a, const Context &c) {
        const auto id = target(a, 0, c, Target::Window);
        return result(QVariantMap{{"preferred", visibilityValue(manager->preferredBarVisibility(id))},
                                  {"effective", visibilityValue(manager->barVisibility(id))}});
    };
    handlers["windows.set_mode"] = [this](const Args &a, const Context &c) {
        return result(manager->setViewMode(target(a, 0, c, Target::Window), viewMode(text(a, 1))));
    };
    handlers["windows.mode"] = [this](const Args &a, const Context &c) {
        return result(modeName(manager->viewMode(target(a, 0, c, Target::Window))));
    };
    handlers["windows.set_zen"] = [this](const Args &a, const Context &c) {
        return result(manager->setZenMode(target(a, 0, c, Target::Window), boolean(a, 1)));
    };
    handlers["windows.zen"] = [this](const Args &a, const Context &c) {
        return result(manager->zenMode(target(a, 0, c, Target::Window)));
    };
    handlers["windows.resize"] = [this](const Args &a, const Context &c) {
        auto *widget = manager->windowWidget(target(a, 0, c, Target::Window));
        if (!widget)
            return result(false);
        widget->resize(number(a.value(1), 1280, 200, 65536), number(a.value(2), 800, 100, 65536));
        return result(true);
    };
    handlers["windows.move"] = [this](const Args &a, const Context &c) {
        auto *widget = manager->windowWidget(target(a, 0, c, Target::Window));
        if (!widget)
            return result(false);
        widget->move(number(a.value(1), 0, -65536, 65536), number(a.value(2), 0, -65536, 65536));
        return result(true);
    };
    handlers["windows.set_title"] = [this](const Args &a, const Context &c) {
        return result(manager->setWindowTitle(target(a, 0, c, Target::Window), text(a, 1)));
    };
    handlers["windows.state"] = [this](const Args &a, const Context &c) {
        const auto id = target(a, 0, c, Target::Window);
        const auto *window = manager->windowWidget(id);
        if (!window)
            return result();
        return result(QVariantMap{{"minimized", window->isMinimized()},
                                  {"maximized", window->isMaximized()},
                                  {"mode", modeName(manager->viewMode(id))}});
    };
    handlers["windows.set_state"] = [this](const Args &a, const Context &c) {
        const auto id = target(a, 0, c, Target::Window);
        if (!manager->model().window(id))
            return result(false);
        const auto state = text(a, 1);
        const QString command = state == "normal"      ? "window.restore"
                                : state == "maximized" ? "window.maximize"
                                : state == "minimized" ? "window.minimize"
                                                       : QString{};
        if (command.isEmpty())
            return failure("Window state must be normal, maximized or minimized");
        return result(manager->executeInContext(command, manager->context(id)));
    };
    handlers["groups.list"] = [this](const Args &a, const Context &c) {
        QVariantList list;
        const auto window = target(a, 0, c, Target::Window);
        for (const auto &g : manager->model().groups())
            if (g.windowId == window)
                list.append(groupValue(g));
        return result(list);
    };
    handlers["tabs.list"] = [this](const Args &a, const Context &c) {
        QVariantList list;
        const auto *group = manager->model().group(target(a, 0, c, Target::Group));
        if (group)
            for (auto id : group->tabs)
                list.append(tabValue(*manager->model().tab(id)));
        return result(list);
    };
    handlers["tabs.new"] = [this](const Args &a, const Context &c) {
        const auto spec = object(a, 0);
        const auto requestedGroup = spec.contains("group") ? identifier({spec.value("group")}, 0) : GroupId{};
        WindowId window;
        if (spec.contains("window"))
            window = identifier({spec.value("window")}, 0);
        else if (!requestedGroup.isNull()) {
            const auto *group = manager->model().group(requestedGroup);
            if (!group)
                return failure("Tab group does not exist");
            window = group->windowId;
        } else
            window = current(c).window;
        const auto group = requestedGroup.isNull() ? manager->context(window).group : requestedGroup;
        const auto title = spec.contains("title") ? text({spec.value("title")}, 0) : QString{};
        const auto command = text({spec.value("command")}, 0);
        const QPointer<WindowManager> windows(manager);
        const QPointer<ScriptHost> owner(q);
        if (!windows->requestNewTab(window, group, command) || !windows || !owner)
            return result(QString{});
        const auto tab = windows->context(window).tab;
        if (spec.contains("title"))
            windows->renameTab(tab, title);
        return result(idText(tab));
    };
    handlers["tabs.close"] = [this](const Args &a, const Context &c) {
        return result(manager->closeTab(target(a, 0, c, Target::Tab)));
    };
    handlers["tabs.activate"] = [this](const Args &a, const Context &) {
        return result(manager->activateTab(identifier(a, 0)));
    };
    handlers["tabs.rename"] = [this](const Args &a, const Context &c) {
        return result(manager->renameTab(target(a, 0, c, Target::Tab), text(a, 1)));
    };
    handlers["tabs.color"] = [this](const Args &a, const Context &c) {
        return result(manager->setTabColor(target(a, 0, c, Target::Tab), text(a, 1)));
    };
    handlers["tabs.icon"] = [this](const Args &a, const Context &c) {
        return result(manager->setTabIconVisible(target(a, 0, c, Target::Tab), boolean(a, 1)));
    };
    handlers["tabs.move"] = [this](const Args &a, const Context &c) {
        return result(manager->moveTab(target(a, 0, c, Target::Tab), identifier(a, 1),
                                       placement(text(a, 2, "center")), number(a.value(3), -1, -1, 100000)));
    };
    handlers["tabs.move_to_window"] = [this](const Args &a, const Context &c) {
        return result(manager->moveTabToWindow(target(a, 0, c, Target::Tab), identifier(a, 1),
                                               placement(text(a, 2, "center"))));
    };
    handlers["tabs.reorder"] = [this](const Args &a, const Context &c) {
        return result(manager->reorderTab(target(a, 0, c, Target::Tab), boolean(a, 1, true)));
    };
    handlers["tabs.detach"] = [this](const Args &a, const Context &c) {
        return result(
            idText(manager->detachTab(target(a, 0, c, Target::Tab), windowKind(text(a, 1, "floating")))));
    };
    handlers["tabs.sort"] = [this](const Args &a, const Context &c) {
        const QStringList orders{"alphabetical_ascending", "alphabetical_descending", "opened_ascending",
                                 "opened_descending",      "visited_ascending",       "visited_descending"};
        int order = orders.indexOf(text(a, 1));
        if (order < 0)
            throw std::runtime_error("Unknown tab sort order");
        return result(manager->sortTabs(target(a, 0, c, Target::Group), TabSortOrder(order)));
    };
    handlers["tabs.set_bar_sharing"] = [this](const Args &a, const Context &c) {
        auto id = target(a, 0, c, Target::Tab);
        auto pos = text(a, 1);
        const auto sharing = boolean(a, 2, true) ? PaneBarSharing::Shared : PaneBarSharing::PerPane;
        if (pos == "top")
            return result(manager->setTabTopBarSharing(id, sharing));
        if (pos == "bottom")
            return result(manager->setTabBottomBarSharing(id, sharing));
        throw std::runtime_error("Bar must be top or bottom");
    };
    handlers["groups.close"] = [this](const Args &a, const Context &c) {
        return result(manager->closeGroup(target(a, 0, c, Target::Group)));
    };
    handlers["groups.move"] = [this](const Args &a, const Context &c) {
        return result(manager->moveGroup(target(a, 0, c, Target::Group), identifier(a, 1),
                                         placement(text(a, 2), false)));
    };
    handlers["groups.merge"] = [this](const Args &a, const Context &c) {
        return result(manager->mergeGroups(target(a, 0, c, Target::Group), identifier(a, 1),
                                           number(a.value(2), -1, -1, 100000)));
    };
    handlers["groups.swap"] = [this](const Args &a, const Context &c) {
        return result(manager->swapGroups(target(a, 0, c, Target::Group), identifier(a, 1)));
    };
    handlers["groups.detach"] = [this](const Args &a, const Context &c) {
        return result(
            idText(manager->detachGroup(target(a, 0, c, Target::Group), windowKind(text(a, 1, "floating")))));
    };
    handlers["groups.to_window"] = [this](const Args &a, const Context &c) {
        return result(manager->moveGroupToWindow(target(a, 0, c, Target::Group), identifier(a, 1),
                                                 placement(text(a, 2, "right"), false)));
    };
    handlers["groups.to_edge"] = [this](const Args &a, const Context &c) {
        return result(manager->moveGroupToEdge(target(a, 0, c, Target::Group),
                                               target(a, 1, c, Target::Window), edge(text(a, 2)),
                                               boolean(a, 3)));
    };
    handlers["groups.to_center"] = [this](const Args &a, const Context &c) {
        return result(manager->moveGroupToCenter(target(a, 0, c, Target::Group)));
    };
    handlers["panes.list"] = [this](const Args &a, const Context &c) {
        QVariantList list;
        for (auto id : manager->model().panes(target(a, 0, c, Target::Tab)))
            list.append(idText(id));
        return result(list);
    };
    handlers["panes.split"] = [this](const Args &a, const Context &c) {
        const auto [tab, pane] = paneTarget(a, c);
        const auto group = manager->model().groupForTab(tab);
        const auto *record = manager->model().group(group);
        if (!record)
            return result(QString{});
        auto ctx = manager->context(record->windowId);
        ctx.group = group;
        ctx.tab = tab;
        ctx.pane = pane;
        auto direction = text(a, 2, "right");
        placement(direction, false);
        if (direction == "top")
            direction = "up";
        if (direction == "bottom")
            direction = "down";
        const QPointer<WindowManager> windows(manager);
        const QPointer<ScriptHost> owner(q);
        if (!windows->executeInContext("pane.split." + direction, ctx) || !windows || !owner)
            return result(QString{});
        const auto *updated = windows->model().tab(tab);
        return result(updated ? idText(updated->activePane) : QString{});
    };
    handlers["panes.close"] = [this](const Args &a, const Context &c) {
        const auto [tab, pane] = paneTarget(a, c);
        return result(manager->closePane(tab, pane));
    };
    handlers["panes.focus"] = [this](const Args &a, const Context &c) {
        const auto [tab, pane] = paneTarget(a, c);
        return result(manager->focusPane(tab, pane));
    };
    handlers["panes.move"] = [this](const Args &a, const Context &c) {
        const auto [tab, pane] = paneTarget(a, c);
        return result(manager->movePane(tab, pane, identifier(a, 2), identifier(a, 3),
                                        placement(text(a, 4, "right"), false)));
    };
    handlers["panes.swap"] = [this](const Args &a, const Context &c) {
        const auto [tab, pane] = paneTarget(a, c);
        return result(manager->swapPanes(tab, pane, identifier(a, 2)));
    };
    handlers["panes.detach"] = [this](const Args &a, const Context &c) {
        const auto [tab, pane] = paneTarget(a, c);
        return result(idText(manager->extractPane(tab, pane, windowKind(text(a, 2, "floating")))));
    };
    handlers["sidebar.move"] = [this](const Args &a, const Context &c) {
        return result(manager->moveTabToEdge(target(a, 0, c, Target::Tab), edge(text(a, 1)), boolean(a, 2)));
    };
    handlers["sidebar.pin"] = [this](const Args &a, const Context &c) {
        return result(manager->pinTab(target(a, 0, c, Target::Tab), boolean(a, 1, true)));
    };
    handlers["sidebar.pin_all"] = [this](const Args &a, const Context &c) {
        return result(
            manager->pinEdge(target(a, 0, c, Target::Window), edge(text(a, 1)), boolean(a, 2, true)));
    };
    handlers["sidebar.set_size"] = [this](const Args &a, const Context &c) {
        const auto p = object(a, 2);
        PeekSizePolicy policy;
        if (p.contains("width"))
            policy.width = number(p["width"], 250, 140, 16777215);
        if (p.contains("height"))
            policy.height = number(p["height"], 170, 100, 16777215);
        return result(manager->setPeekSizePolicy(target(a, 0, c, Target::Tab), edge(text(a, 1)), policy));
    };
    handlers["sidebar.size"] = [this](const Args &a, const Context &c) {
        const auto size = manager->peekSize(target(a, 0, c, Target::Tab), edge(text(a, 1)));
        return result(QVariantMap{{"width", size.width()}, {"height", size.height()}});
    };
    handlers["sidebar.reset_size"] = [this](const Args &a, const Context &c) {
        return result(manager->resetPeekSize(target(a, 0, c, Target::Tab), edge(text(a, 1))));
    };
    handlers["toolbars.list"] = [this](const Args &, const Context &) {
        QVariantList list;
        for (const auto &t : manager->toolbars())
            list.append(QVariantMap{{"id", t.id},
                                    {"title", t.title},
                                    {"commands", t.commands},
                                    {"area", t.area == ToolbarArea::Top ? "top" : "bottom"},
                                    {"row", t.row}});
        return result(list);
    };
    handlers["toolbars.state"] = [this](const Args &a, const Context &c) {
        const auto state = manager->toolbarState(target(a, 0, c, Target::Window), text(a, 1));
        if (!state)
            return result();
        return result(QVariantMap{{"area", state->area == ToolbarArea::Top ? "top" : "bottom"},
                                  {"row", state->row},
                                  {"index", state->index},
                                  {"width", state->width},
                                  {"visible", state->visible},
                                  {"elastic", state->elastic},
                                  {"locked", state->locked},
                                  {"hidden_items", state->hiddenItems}});
    };
    handlers["toolbars.move"] = [this](const Args &a, const Context &c) {
        return result(manager->moveToolbar(target(a, 0, c, Target::Window), text(a, 1),
                                           toolbarArea(text(a, 2)), number(a.value(3), 0, 0, 255),
                                           number(a.value(4), -1, -1, 10000)));
    };
    handlers["toolbars.visible"] = [this](const Args &a, const Context &c) {
        return result(manager->setToolbarVisible(target(a, 0, c, Target::Window), text(a, 1), boolean(a, 2)));
    };
    handlers["toolbars.width"] = [this](const Args &a, const Context &c) {
        return result(manager->setToolbarWidth(target(a, 0, c, Target::Window), text(a, 1),
                                               number(a.value(2), 100, 1, 65536)));
    };
    handlers["toolbars.elastic"] = [this](const Args &a, const Context &c) {
        return result(manager->setToolbarElastic(target(a, 0, c, Target::Window), text(a, 1), boolean(a, 2)));
    };
    handlers["toolbars.item_visible"] = [this](const Args &a, const Context &c) {
        return result(manager->setToolbarItemVisible(target(a, 0, c, Target::Window), text(a, 1), text(a, 2),
                                                     boolean(a, 3)));
    };
    handlers["toolbars.locked"] = [this](const Args &a, const Context &c) {
        return result(manager->setToolbarsLocked(target(a, 0, c, Target::Window), boolean(a, 1)));
    };
    handlers["toolbars.reset_size"] = [this](const Args &a, const Context &c) {
        return result(manager->resetToolbarWidth(target(a, 0, c, Target::Window), text(a, 1)));
    };
    handlers["status.set"] = [this](const Args &a, const Context &c) {
        manager->setStatus(target(a, 0, c, Target::Window), text(a, 1));
        return result(true);
    };
    handlers["status.items"] = [this](const Args &, const Context &) {
        QVariantList list;
        for (const auto &item : manager->statusItems())
            list.append(QVariantMap{{"id", item.id}, {"text", item.text}, {"tooltip", item.tooltip}});
        return result(QVariantMap{{"ids", manager->statusItemIds()}, {"custom", list}});
    };
    handlers["status.visible"] = [this](const Args &a, const Context &c) {
        return result(
            manager->setStatusItemVisible(target(a, 0, c, Target::Window), text(a, 1), boolean(a, 2)));
    };
    handlers["layout.snapshot"] = [this](const Args &, const Context &) {
        return result(manager->saveLayout().toVariantMap());
    };
    handlers["layout.restore"] = [this](const Args &a, const Context &) {
        const auto operation = manager->restoreLayout(QJsonObject::fromVariantMap(object(a, 0)));
        return operation ? result(true) : failure(operation.error);
    };
    handlers["ui.settings"] = [this](const Args &a, const Context &c) {
        manager->showSettings(target(a, 0, c, Target::Window));
        return result(true);
    };
    handlers["ui.command_palette"] = [this](const Args &a, const Context &c) {
        manager->showCommandPalette(target(a, 0, c, Target::Window), panelOptions(a));
        return result(true);
    };
    handlers["ui.window_selector"] = [this](const Args &a, const Context &c) {
        manager->showWindowSelector(target(a, 0, c, Target::Window), panelOptions(a));
        return result(true);
    };
    installAppearanceApi();
}
} // namespace window_system_lua
