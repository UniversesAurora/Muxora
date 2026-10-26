#include <QCoreApplication>
#include "style/tooltip_layout.hpp"
#include "style/wheel_guard.hpp"
#include "style/smooth_scroll.hpp"
#include "window_manager_p.hpp"
#include "view_state_p.hpp"
#include <window_system/window_manager.hpp>
#include <window_system/interface_appearance.hpp>
#include "chrome/workspace_window.hpp"
#include "chrome/command_strip.hpp"
#include "commands/command_dialogs.hpp"
#include "commands/popup_menu.hpp"
#include "docking/dock_surface.hpp"
#include "docking/drag_controller.hpp"
#include "docking/tab_strip.hpp"
#include "panes/tab_content.hpp"
#include <QApplication>
#include <QFrame>
#include <QEvent>
#include <QFocusEvent>
#include <QCursor>
#include <QGuiApplication>
#include <QJsonArray>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QContextMenuEvent>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QScreen>
#include <QScopeGuard>
#include <QSet>
#include <QStyle>
#include <QTimer>
#include <algorithm>
#include <limits>

namespace window_system {
QVector<WindowManager::Private::BindingCandidate> WindowManager::Private::shortcutCandidates(QKeyCombination first)
{
    if (bindingIndexRevision != bindingsRevision) {
        bindingIndex.clear();
        for (const auto& command : commands.commands())
            for (const auto& binding : command.bindings)
                bindingIndex[binding.sequence[0].toCombined()].append(
                    {command.id, binding.sequence, binding.args, command.enabled});
        bindingIndexRevision = bindingsRevision;
    }
    return bindingIndex.value(first.toCombined());
}
WindowManager::WindowManager(QObject* parent) : QObject(parent), d(std::make_unique<Private>())
{
    Q_ASSERT(qApp);
    d->storage = new QWidget; d->storage->setObjectName("WindowSystemStorage"); d->storage->hide();
    d->drag = new DragController(this);
    d->keyTimer = new QTimer(this); d->keyTimer->setSingleShot(true);
    d->keyTimer->setObjectName(QStringLiteral("CommandChordTimer"));
    QObject::connect(d->keyTimer, &QTimer::timeout, this, &WindowManager::cancelPendingShortcut);
    const QPointer<WindowManager> owner(this);
    d->commands.setExecutionValidator([owner](const CommandContext& target) {
        return owner && owner->validCommandContext(target);
    });
    d->commands.setCompositeContextResolver([owner](const CommandContext& original) -> std::optional<CommandContext> {
        if (!owner || original.window.isNull() || !owner->d->model.window(original.window)) return {};
        return owner->context(original.window);
    });
    d->commands.setBindingsChangedHandler([owner] {
        if (!owner) return;
        ++owner->d->bindingsRevision;
        owner->cancelPendingShortcut();
        emit owner->commandsChanged();
    });
    d->commands.setCatalogChangedHandler([owner] {
        if (!owner) return;
        ++owner->d->bindingsRevision;
        owner->cancelPendingShortcut();
        if (owner->d->catalogRefreshPending) return;
        owner->d->catalogRefreshPending = true;
        QTimer::singleShot(0, owner, [owner] {
            if (!owner) return;
            owner->d->catalogRefreshPending = false;
            emit owner->commandsChanged();
        });
    });
    qApp->installEventFilter(this); registerWindowCommands();
    d->theme = scaledInterfaceTheme(d->authoredTheme);
    if (auto* appearance = InterfaceAppearanceController::applicationInstance())
        connect(appearance, &InterfaceAppearanceController::appearanceChanged, this, [this] { setTheme(d->authoredTheme); });
    QObject::connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* next) {
        if (!d->destroying && !d->refreshing) d->inputMethods.focus(d->model, next);
    });
}
WindowManager::~WindowManager()
{
    d->destroying = true; qApp->removeEventFilter(this); d->drag->releaseForMutation();
    delete d->drag; d->drag = nullptr;
    for (auto window : d->windows) if (window) {
        window->allowClose = true;
        // Qt must finish the current content-resize layout before its widgets
        // can be released by a synchronous host callback.
        if (d->geometryUpdateDepth) window->deleteLater(); else delete window;
    }
    if (d->geometryUpdateDepth) d->storage->deleteLater(); else delete d->storage;
}
WindowId WindowManager::createWindow(WindowKind kind, const WindowOptions& options, WindowId owner)
{
    QSet<QString> hidden;
    for (const auto& item : options.hiddenStatusItems) {
        if (!statusItemIds().contains(item) || hidden.contains(item)) return {};
        hidden.insert(item);
    }
    if (kind == WindowKind::Floating && owner.isNull()) {
        const auto ctx = context(); const auto* record = d->model.window(ctx.window);
        if (record) owner = d->model.familyRoot(record->familyId);
    }
    const auto id = d->model.createWindow(kind, owner);
    if (id.isNull()) return {};
    auto resolved = options;
    if (options.inheritVisibilityProfile) {
        const auto bars = d->visibilityProfiles.value(kind);
        resolved.toolbarsVisible &= bars.toolbars; resolved.statusBarVisible &= bars.statusBar;
        resolved.tabBarsVisible &= bars.tabBars; resolved.sideBarsVisible &= bars.sideBars;
    }
    d->options.insert(id, resolved); d->visibilityKinds.insert(id, kind);
    d->windowVisibility[id].insert(kind, preferredBarVisibility(id));
    refresh(); return id;
}
TabId WindowManager::addTab(WindowId window, QWidget* content, const TabOptions& options, GroupId group, int insertionIndex)
{
    if (!content || content->property("windowSystemManaged").toBool()
        || (options.topBarSharing != PaneBarSharing::Shared && options.topBarSharing != PaneBarSharing::PerPane)
        || (options.bottomBarSharing != PaneBarSharing::Shared && options.bottomBarSharing != PaneBarSharing::PerPane)) return {};
    const auto* owner = d->model.window(window);
    if (!owner || insertionIndex < -1) return {};
    auto target = group.isNull() ? owner->activeGroup : group;
    FolderId folder = options.folder.isNull() ? std::exchange(d->newTabFolder, {}) : options.folder;
    if (!folder.isNull() && d->model.groupForFolder(folder).isNull()) folder = {};
    if (!folder.isNull()) {
        target = d->model.groupForFolder(folder);
        const auto* in = d->model.group(target);
        if (!in || in->windowId != window) return {};
        insertionIndex = int(in->tabs.indexOf(d->model.folderTabs(folder).last())) + 1;
    }
    const auto* record = d->model.group(target);
    if (insertionIndex < 0) {
        if (record && (d->behavior.newTabPosition == NewTabPosition::AfterActive || d->behavior.newTabPosition == NewTabPosition::BeforeActive))
            if (const auto* active = d->model.tab(record->activeTab)) folder = active->folder;
        const int count = record ? int(record->tabs.size()) : 0;
        const int active = record ? int(record->tabs.indexOf(record->activeTab)) : -1;
        switch (d->behavior.newTabPosition) {
        case NewTabPosition::Beginning: insertionIndex = 0; break;
        case NewTabPosition::BeforeActive: insertionIndex = active < 0 ? count : active; break;
        case NewTabPosition::AfterActive: insertionIndex = active < 0 ? count : active + 1; break;
        case NewTabPosition::End: insertionIndex = count; break;
        }
    }
    const auto id = d->model.addTab(window, options.title, target == owner->activeGroup && group.isNull() ? GroupId{} : target, insertionIndex, folder);
    if (id.isNull()) return {};
    const auto pane = d->model.tab(id)->activePane;
    content->setProperty("windowSystemManaged", true); content->setProperty("paneId", pane);
    if (content->focusPolicy() == Qt::NoFocus) content->setFocusPolicy(Qt::StrongFocus);
    content->setParent(d->storage); d->panes.insert(pane, content); d->descriptors.insert(pane, options);
    refresh(); activateTab(id); return id;
}
void WindowManager::setContentFactory(ContentFactory factory) { d->factory = std::move(factory); }
void WindowManager::setContentVisibilityHandler(
    std::function<void(const QVector<ContentVisibilityChange>&)> handler)
{
    d->contentVisibilityHandler = std::move(handler);
    d->contentVisibilitySnapshotRequested = bool(d->contentVisibilityHandler);
    scheduleContentVisibilityNotification();
}
void WindowManager::scheduleContentVisibilityNotification()
{
    if (!d->contentVisibilityHandler || d->contentVisibilityNotificationPending || d->destroying) return;
    d->contentVisibilityNotificationPending = true;
    QTimer::singleShot(0, this, [this] {
        d->contentVisibilityNotificationPending = false;
        if (d->destroying || !d->contentVisibilityHandler) return;
        QHash<PaneId, bool> current;
        QVector<PaneId> order;
        for (const auto window : d->model.windowOrder()) {
            const auto host = d->windows.value(window);
            const bool windowShown = host && host->isVisible() && !host->isMinimized();
            for (const auto tab : d->model.orderedTabs(window))
                for (const auto pane : d->model.panes(tab)) {
                    order.append(pane);
                    const QPointer<QWidget> widget = d->panes.value(pane);
                    current.insert(pane, windowShown && widget && widget->isVisible());
                }
        }
        QVector<ContentVisibilityChange> changes;
        const bool snapshot = d->contentVisibilitySnapshotRequested;
        for (const auto pane : order) {
            const auto previous = d->contentVisibility.constFind(pane);
            if (snapshot || previous == d->contentVisibility.cend() || previous.value() != current.value(pane))
                changes.append({pane, current.value(pane)});
        }
        for (const auto pane : d->contentVisibilityOrder)
            if (!current.contains(pane) && d->contentVisibility.value(pane))
                changes.append({pane, false});
        d->contentVisibility = std::move(current);
        d->contentVisibilityOrder = std::move(order);
        d->contentVisibilitySnapshotRequested = false;
        if (changes.isEmpty()) return;
        const auto handler = d->contentVisibilityHandler;
        handler(changes);
    });
}
void WindowManager::reportPanesHiddenBeforeReparent(const QVector<PaneId>& panes)
{
    if (!d->contentVisibilityHandler || panes.isEmpty()) return;
    QVector<ContentVisibilityChange> changes;
    for (const auto pane : panes) {
        if (!d->panes.contains(pane)) continue;
        d->contentVisibility.insert(pane, false);
        changes.append({pane, false});
    }
    if (changes.isEmpty()) return;
    const auto handler = d->contentVisibilityHandler;
    handler(changes);
}
TabId WindowManager::addTabToNewGroup(WindowId window, QWidget* content, const TabOptions& options,
                                    GroupId reference, Placement placement)
{
    const QPointer<WindowManager> self(this);
    const QPointer<QWidget> observed(content);
    if (!observed) return {};
    const auto previousManaged = content->property("windowSystemManaged");
    if (!self || !observed || previousManaged.toBool()
        || (options.topBarSharing != PaneBarSharing::Shared && options.topBarSharing != PaneBarSharing::PerPane)
        || (options.bottomBarSharing != PaneBarSharing::Shared && options.bottomBarSharing != PaneBarSharing::PerPane)) return {};
    const auto previousPane = content->property("paneId");
    if (!self || !observed) return {};
    const auto previousFocus = content->focusPolicy();
    const auto revision = d->model.revision();
    const auto state = d->model.stateId();
    auto candidate = d->model;
    const auto id = candidate.addTabToNewGroup(window, options.title, reference, placement);
    if (id.isNull()) return {};
    const auto pane = candidate.tab(id)->activePane;
    bool adopted = false;
    const auto restorePreparation = qScopeGuard([observed, previousManaged, previousPane, previousFocus, &adopted] {
        if (adopted) return;
        if (observed) observed->setFocusPolicy(previousFocus);
        if (observed) observed->setProperty("paneId", previousPane);
        if (observed) observed->setProperty("windowSystemManaged", previousManaged);
    });
    // Attribute callbacks still observe the old model and caller ownership.
    // Discard the candidate if a callback destroys either participant or
    // changes the model; never overwrite a host's reentrant layout mutation.
    // stateId() also catches a restored model whose revision repeats.
    content->setProperty("windowSystemManaged", true);
    if (!self || !observed) return {};
    content->setProperty("paneId", pane);
    if (!self || !observed) return {};
    if (content->focusPolicy() == Qt::NoFocus) content->setFocusPolicy(Qt::StrongFocus);
    if (!self || !observed || d->model.revision() != revision || d->model.stateId() != state) return {};
    // Commit the model and ownership together before parent/refresh callbacks.
    d->model = std::move(candidate);
    d->panes.insert(pane, observed); d->descriptors.insert(pane, options);
    adopted = true;
    content->setParent(d->storage);
    if (!self) { if (observed) delete observed.data(); return id; }
    if (!observed) {
        d->panes.remove(pane); d->descriptors.remove(pane);
        if (d->model.tab(id)) d->model.removeTab(id);
        refresh(); return id;
    }
    if (!d->model.tab(id)) return id;
    refresh();
    if (self) activateTab(id);
    // Ownership transferred before notifying the host, even if a synchronous
    // listener closes the tab or destroys its manager during refresh.
    return id;
}
bool WindowManager::setTabCreationCommands(QString defaultCommand, QStringList choices)
{
    if (!defaultCommand.isEmpty() && !d->commands.find(defaultCommand)) return false;
    QSet<QString> seen;
    for (const auto& id : choices) {
        if (id.isEmpty() || seen.contains(id) || !d->commands.find(id)) return false;
        seen.insert(id);
    }
    if (!defaultCommand.isEmpty() && !seen.contains(defaultCommand)) choices.prepend(defaultCommand);
    d->defaultTabCommand = std::move(defaultCommand);
    d->tabCreationCommands = std::move(choices);
    return true;
}
bool WindowManager::requestNewTab(WindowId window, GroupId group, QString command, QUuid folder)
{
    if (!d->model.window(window)) return false;
    auto ctx = context(window);
    if (!folder.isNull()) group = d->model.groupForFolder(folder);
    if (group.isNull()) group = ctx.group;
    const auto* target = d->model.group(group);
    if (!group.isNull() && (!target || target->windowId != window)) return false;
    if (command.isEmpty()) command = d->defaultTabCommand;
    if (command.isEmpty() || !d->tabCreationCommands.contains(command)) return false;
    ctx.group = group; ctx.tab = target ? target->activeTab : TabId{};
    const auto* tab = d->model.tab(ctx.tab);
    ctx.pane = tab ? tab->activePane : PaneId{};
    const auto previous = std::exchange(d->newTabFolder, folder);
    const QPointer<WindowManager> self(this);
    const bool result = executeResolved(command, std::move(ctx), true);
    if (self) d->newTabFolder = previous;
    return result;
}
QMenu* WindowManager::createTabCreationMenu(WindowId window, GroupId group, QWidget* parent)
{
    const auto* target = d->model.group(group);
    if (!target || target->windowId != window) return nullptr;
    auto ctx = context(window); ctx.group = group; ctx.tab = target->activeTab;
    const auto* tab = d->model.tab(ctx.tab); ctx.pane = tab ? tab->activePane : PaneId{};
    const QPointer<WindowManager> self(this);
    QPointer<QMenu> menu = new DismissOnlyMenu(parent);
    menu->setObjectName("TabCreationMenu");
    menu->setStyleSheet("QMenu{menu-scrollable:1;}");
    applyTheme(menu, d->theme);
    const auto choices = d->tabCreationCommands;
    for (const auto& id : choices) {
        const auto* found = d->commands.find(id);
        if (!found) continue;
        const Command retained = *found;
        const bool enabled = !retained.enabled || retained.enabled(ctx);
        if (!self || !menu) { if (menu) delete menu.data(); return nullptr; }
        const auto* currentGroup = d->model.group(group);
        if (!currentGroup || currentGroup->windowId != window) { delete menu.data(); return nullptr; }
        auto* action = menu->addAction(retained.title);
        action->setObjectName(id); action->setEnabled(enabled);
        action->setProperty("windowSystemCommand", id);
        QObject::connect(action, &QAction::triggered, this, [self, window, group, id] {
            if (self) self->requestNewTab(window, group, id);
        });
    }
    if (menu->isEmpty()) { delete menu.data(); return nullptr; }
    return menu;
}
QWidget* WindowManager::createContent(const TabOptions& options, PaneId pane)
{
    if (isInternalSettingsContentType(options.contentType)) return createInternalSettingsContent(this, options);
    // A host factory may replace itself or destroy the manager synchronously.
    // Keep its callable and captures alive until this invocation has returned.
    const ContentFactory retained = d->factory;
    return retained ? retained(options, pane) : nullptr;
}
TabContent* WindowManager::ensureTabView(TabId tab)
{
    if (!d->model.tab(tab)) return nullptr;
    if (const QPointer<TabContent> existing = d->tabs.value(tab)) return existing.data();
    const QPointer<WindowManager> self(this);
    const QPointer<TabContent> view = new TabContent(this, tab, d->storage);
    d->tabs.insert(tab, view);
    const auto currentView = [&] {
        return self && view && d->tabs.value(tab) == view ? view.data() : nullptr;
    };
    auto* record = d->model.tab(tab);
    if (!currentView() || !record) return nullptr;
    const auto options = d->descriptors.value(record->activePane);
    view->setBarSharing(options.topBarSharing, options.bottomBarSharing);
    if (!currentView()) return nullptr;
    if (const QPointer<QWidget> bar = d->pendingTabTopBars.take(tab)) view->setTopBar(bar);
    if (!currentView()) return nullptr;
    if (const QPointer<QWidget> bar = d->pendingTabBottomBars.take(tab)) view->setBottomBar(bar);
    if (!currentView()) return nullptr;
    record = d->model.tab(tab);
    if (!record) return nullptr;
    view->rebuild(*record, d->panes, d->paneTopBars, d->paneBottomBars);
    return self ? d->tabs.value(tab).data() : nullptr;
}
void WindowManager::setCloseGuard(CloseGuard guard) { d->closeGuard = std::move(guard); }
bool WindowManager::setTabTopBar(TabId tab, QWidget* bar) { return setTabBar(tab, bar, true); }
bool WindowManager::setTabBottomBar(TabId tab, QWidget* bar) { return setTabBar(tab, bar, false); }
bool WindowManager::setTabBar(TabId tab, QWidget* bar, bool top)
{
    if (!d->model.tab(tab) || (bar && bar->property("windowSystemManaged").toBool())) return false;
    if (bar) bar->setProperty("windowSystemManaged", true);
    if (auto view = d->tabs.value(tab)) {
        top ? view->setTopBar(bar) : view->setBottomBar(bar);
        return true;
    }
    // A tab never shown has no view yet; ensureTabView() delivers the bar.
    auto& bars = top ? d->pendingTabTopBars : d->pendingTabBottomBars;
    if (auto previous = bars.take(tab)) {
        previous->setParent(d->storage); previous->hide(); previous->deleteLater();
    }
    if (bar) { bar->setParent(d->storage); bar->hide(); bars.insert(tab, bar); }
    return true;
}
bool WindowManager::setPaneTopBar(PaneId pane, QWidget* bar) { return setPaneBar(pane, bar, true); }
bool WindowManager::setPaneBottomBar(PaneId pane, QWidget* bar) { return setPaneBar(pane, bar, false); }
bool WindowManager::setTabTopBarSharing(TabId tab, PaneBarSharing sharing) { return setTabBarSharing(tab, sharing, true); }
bool WindowManager::setTabBottomBarSharing(TabId tab, PaneBarSharing sharing) { return setTabBarSharing(tab, sharing, false); }
bool WindowManager::setTabBarSharing(TabId tab, PaneBarSharing sharing, bool top)
{
    if (!d->model.tab(tab) || (sharing != PaneBarSharing::Shared && sharing != PaneBarSharing::PerPane)) return false;
    for (const auto pane : d->model.panes(tab)) {
        auto& options = d->descriptors[pane];
        (top ? options.topBarSharing : options.bottomBarSharing) = sharing;
    }
    const auto options = tabOptions(tab);
    const QPointer<WindowManager> self(this);
    if (auto view = d->tabs.value(tab)) view->setBarSharing(options.topBarSharing, options.bottomBarSharing);
    if (!self) return false;
    emit layoutChanged();
    return true;
}
bool WindowManager::setPaneBar(PaneId pane, QWidget* bar, bool top)
{
    if (!d->panes.contains(pane)) return false;
    auto& bars = top ? d->paneTopBars : d->paneBottomBars;
    if (bars.value(pane) == bar) return true;
    if (bar && bar->property("windowSystemManaged").toBool()) return false;
    if (auto previous = bars.take(pane)) {
        previous->setParent(d->storage); previous->hide(); previous->deleteLater();
    }
    if (bar) {
        bar->setProperty("windowSystemManaged", true);
        bar->setParent(d->storage); bar->hide(); bars.insert(pane, bar);
    }
    refresh(); return true;
}
TabOptions WindowManager::tabOptions(TabId tab) const
{
    const auto* record = d->model.tab(tab);
    if (!record) return {};
    auto options = d->descriptors.value(record->activePane); options.title = record->title;
    return options;
}
bool WindowManager::setTabIconVisible(TabId tab, bool visible)
{
    if (!d->model.tab(tab)) return false;
    for (const auto pane : d->model.panes(tab)) d->descriptors[pane].showIcon = visible;
    refresh(); return true;
}
PaneId WindowManager::splitPane(TabId tab, PaneId pane, QWidget* content, Placement placement)
{
    if (!content || content->property("windowSystemManaged").toBool()) return {};
    const auto id = d->model.splitPane(tab, pane, placement);
    if (id.isNull()) return {};
    content->setProperty("windowSystemManaged", true); content->setProperty("paneId", id);
    if (content->focusPolicy() == Qt::NoFocus) content->setFocusPolicy(Qt::StrongFocus);
    content->setParent(d->storage); d->panes.insert(id, content);
    auto descriptor = d->descriptors.value(pane); descriptor.title = d->model.tab(tab)->title;
    d->descriptors.insert(id, descriptor); refresh(); focusPane(tab, id); return id;
}
bool WindowManager::closeTab(TabId tab)
{
    if (d->closeAllPending) return false;
    if (!d->model.tab(tab)) return false;
    if (!approveClose({tab})) return false;
    d->drag->releaseForMutation(); return applyResult(d->model.removeTab(tab));
}
bool WindowManager::closePane(TabId tab, PaneId pane)
{
    if (d->closeAllPending) return false;
    if (!d->model.panes(tab).contains(pane)) return false;
    if (d->model.panes(tab).size() == 1) return closeTab(tab);
    if (!approveClose({tab})) return false;
    return applyResult(d->model.removePane(tab, pane));
}
bool WindowManager::closeTabs(const QVector<TabId>& tabs)
{
    if (d->closeAllPending) return false;
    if (tabs.isEmpty() || QSet<TabId>(tabs.cbegin(), tabs.cend()).size() != tabs.size()) return false;
    if (!approveClose(tabs)) return false;
    d->drag->releaseForMutation();
    auto candidate = d->model;
    for (const auto tab : tabs) if (!candidate.removeTab(tab)) return false;
    d->model = std::move(candidate); refresh(); return true;
}
bool WindowManager::requestCloseWindow(WindowId window)
{
    if (d->closeAllPending) return false;
    const auto* record = d->model.window(window); if (!record) return false;
    if (d->closeRequests.contains(window)) return false;
    const auto kind = record->kind;
    const auto family = record->familyId;
    QPointer<WindowManager> self(this);
    d->closeRequests.insert(window);
    const auto releaseRequest = qScopeGuard([self, window] {
        if (self) self->d->closeRequests.remove(window);
    });
    auto windowScope = [this, window, kind, family] {
        QSet<WindowId> scope{window};
        if (kind == WindowKind::Workbench)
            for (const auto id : d->model.windowOrder())
                if (d->model.window(id)->familyId == family) scope.insert(id);
        return scope;
    };
    auto collectTabs = [this](const QSet<WindowId>& scope) {
        QVector<TabId> result;
        for (const auto id : d->model.windowOrder())
            if (scope.contains(id)) result += d->model.orderedTabs(id);
        return result;
    };
    const auto originalScope = windowScope();
    const auto affected = collectTabs(originalScope);
    if (!approveClose(affected) || !self) return false;
    record = d->model.window(window);
    if (!record || record->kind != kind || record->familyId != family || windowScope() != originalScope) return false;
    const auto currentTabs = collectTabs(originalScope);
    if (QSet<TabId>(affected.cbegin(), affected.cend()) != QSet<TabId>(currentTabs.cbegin(), currentTabs.cend())) return false;
    d->drag->releaseForMutation();
    auto candidate = d->model;
    const auto result = candidate.closeWindow(window);
    if (!result) return false;
    if (candidate.windows().isEmpty()) {
        const auto revision = d->model.revision();
        emit lastWindowAboutToClose();
        if (!self || d->model.revision() != revision) return false;
    }
    d->model = std::move(candidate); refresh(); return true;
}
bool WindowManager::requestCloseAllWindows()
{
    if (d->closeAllPending || !d->closeRequests.isEmpty() || !d->guardedTabs.isEmpty()
        || d->model.windows().isEmpty()) return false;
    // A callback during preparation/finalization must not close half of a
    // drag transaction. A fully active drag can first restore its stable view.
    if (d->drag->hasStableLayout() && !d->drag->isActive()) return false;
    QPointer<WindowManager> self(this);
    d->closeAllPending = true;
    const auto releaseRequest = qScopeGuard([self] {
        if (self) self->d->closeAllPending = false;
    });
    d->drag->cancel();
    if (!self) return false;

    // Approval covers exactly the content that will close. The model is
    // compared by identity (an implicitly shared copy costs O(1)); only when
    // a callback committed something is its content compared, so a no-op
    // such as activating the active tab still lets the close proceed. Window
    // state stays part of the comparison: placement, options and toolbar
    // edits from a callback cancel the close as before.
    const auto modelBefore = d->model;
    const auto viewBefore = saveViewLayout();
    const auto windows = d->windows;
    const auto panes = d->panes;
    auto unchanged = [this, &modelBefore, &viewBefore, &windows, &panes] {
        if (d->model.stateId() != modelBefore.stateId() && d->model.toJson() != modelBefore.toJson()) return false;
        if (saveViewLayout() != viewBefore || d->windows.size() != windows.size() || d->panes.size() != panes.size()) return false;
        for (auto it = windows.cbegin(); it != windows.cend(); ++it)
            if (!it.value() || d->windows.value(it.key()) != it.value()) return false;
        for (auto it = panes.cbegin(); it != panes.cend(); ++it)
            if (!it.value() || d->panes.value(it.key()) != it.value()) return false;
        return true;
    };
    QVector<TabId> affected;
    for (const auto id : d->model.windowOrder()) affected += d->model.orderedTabs(id);
    if (!approveClose(affected) || !self || !unchanged()) return false;

    auto candidate = d->model;
    for (const auto id : d->model.windowOrder())
        if (candidate.window(id) && !candidate.closeWindow(id)) return false;
    if (!candidate.windows().isEmpty()) return false;
    const auto revision = d->model.revision();
    emit lastWindowAboutToClose();
    if (!self || d->model.revision() != revision || !unchanged()) return false;
    d->model = std::move(candidate);
    refresh();
    return true;
}
bool WindowManager::approveClose(const QVector<TabId>& tabs)
{
    for (const auto tab : tabs)
        if (!d->model.tab(tab) || d->guardedTabs.contains(tab)) return false;
    if (!d->closeGuard) return true;
    QHash<TabId, QSet<PaneId>> paneIds;
    QHash<PaneId, QPointer<QWidget>> contents;
    for (const auto tab : tabs) {
        const auto panes = d->model.panes(tab);
        paneIds.insert(tab, QSet<PaneId>(panes.cbegin(), panes.cend()));
        for (const auto pane : panes) contents.insert(pane, d->panes.value(pane));
    }
    QPointer<WindowManager> self(this);
    for (const auto tab : tabs) d->guardedTabs.insert(tab);
    const auto releaseGuard = qScopeGuard([self, tabs] {
        if (self) for (const auto tab : tabs) self->d->guardedTabs.remove(tab);
    });
    const auto guard = d->closeGuard;
    if (!guard(tabs) || !self) return false;
    // A host confirmation can enter a nested event loop. Only the exact content
    // approved before it opened may be closed when that confirmation returns.
    for (const auto tab : tabs) {
        if (!d->model.tab(tab)) return false;
        const auto panes = d->model.panes(tab);
        if (QSet<PaneId>(panes.cbegin(), panes.cend()) != paneIds.value(tab)) return false;
    }
    for (auto it = contents.cbegin(); it != contents.cend(); ++it)
        if (it.value() != d->panes.value(it.key())) return false;
    return true;
}
bool WindowManager::moveTab(TabId tab, GroupId group, Placement placement, int index, std::optional<bool> sticky,
                            std::optional<FolderId> folder)
{ return applyResult(d->model.moveTab(tab, group, placement, index, sticky, folder)); }
WindowId WindowManager::detachTab(TabId tab, WindowKind kind)
{
    return detachTabImpl(tab, kind, {});
}
WindowId WindowManager::detachTabImpl(TabId tab, WindowKind kind, const std::function<void(QWidget*)>& beforeShow, bool preserveDrag)
{
    const auto* group = d->model.group(d->model.groupForTab(tab));
    const auto source = group ? group->windowId : WindowId{};
    return adoptDetachedWindow(d->model.detachTab(tab, kind), source, kind, tab, beforeShow, preserveDrag);
}
WindowId WindowManager::detachFolderImpl(FolderId folder, WindowKind kind, const std::function<void(QWidget*)>& beforeShow, bool preserveDrag)
{
    const auto* group = d->model.group(d->model.groupForFolder(folder));
    const auto tabs = d->model.folderTabs(folder);
    if (!group || tabs.isEmpty()) return {};
    const auto source = group->windowId;
    const auto active = tabs.contains(group->activeTab) ? group->activeTab : tabs.first();
    return adoptDetachedWindow(d->model.detachFolder(folder, kind), source, kind, active, beforeShow, preserveDrag);
}
WindowId WindowManager::adoptDetachedWindow(WindowId id, WindowId sourceWindow, WindowKind kind, TabId tab,
                                            const std::function<void(QWidget*)>& beforeShow, bool preserveDrag)
{
    if (id.isNull()) return {};
    const QPointer<WindowManager> self(this);
    const auto source = context(sourceWindow);
    auto options = d->options.value(source.window); options.size = QSize(880, 560);
    const auto bars = d->visibilityProfiles.value(kind);
    options.toolbarsVisible = bars.toolbars; options.statusBarVisible = bars.statusBar;
    options.tabBarsVisible = bars.tabBars; options.sideBarsVisible = bars.sideBars;
    d->options.insert(id, options); d->visibilityKinds.insert(id, kind); d->windowVisibility[id].insert(kind, bars);
    refresh(preserveDrag);
    if (!self) return {};
    if (beforeShow) if (auto* window = windowWidget(id)) beforeShow(window);
    if (!self) return {};
    showWindow(id);
    if (!self) return {};
    activateTab(tab);
    return self ? id : WindowId{};
}
bool WindowManager::moveAllTabs(WindowId source, GroupId target, Placement placement, int insertionIndex, FolderId asFolder)
{ return applyResult(d->model.moveAllTabs(source, target, placement, insertionIndex, {}, asFolder)); }
bool WindowManager::moveTabToEdge(TabId tab, Edge edge, bool pinned)
{
    return applyResult(d->model.moveTabToEdge(tab, edge, pinned));
}
bool WindowManager::moveTabToEdgeBar(TabId tab, WindowId target, Edge edge, int insertionIndex)
{ return applyResult(d->model.moveTabToEdgeBar(tab, target, edge, insertionIndex)); }
bool WindowManager::moveAllTabsToEdgeBar(WindowId source, WindowId target, Edge edge, int insertionIndex)
{ return applyResult(d->model.moveAllTabsToEdgeBar(source, target, edge, insertionIndex)); }
bool WindowManager::moveAllTabsToEdge(WindowId source, WindowId target, Edge edge, bool pinned)
{ return applyResult(d->model.moveAllTabsToEdge(source, target, edge, pinned)); }
bool WindowManager::pinGroup(GroupId group, bool pinned) { return applyResult(d->model.setGroupPinned(group, pinned)); }
bool WindowManager::pinTab(TabId tab, bool pinned) { return applyResult(d->model.setTabPinned(tab, pinned)); }
bool WindowManager::pinEdge(WindowId window, Edge edge, bool pinned) { return applyResult(d->model.setEdgePinned(window, edge, pinned)); }
QSize WindowManager::peekSize(TabId tab, Edge edge) const
{
    if (!d->model.tab(tab) || (edge != Edge::Left && edge != Edge::Right
        && edge != Edge::Top && edge != Edge::Bottom)) return {};
    const auto policy = peekSizePolicy(tab, edge);
    // The QSize query remains useful for numeric editors, even when the actual
    // panel stretches an axis. PeekSizePolicy exposes that distinction.
    return QSize(policy.width.value_or(250), policy.height.value_or(170));
}
PeekSizePolicy WindowManager::peekSizePolicy(TabId tab, Edge edge) const
{
    return d->model.peekSizePolicy(tab, edge);
}
bool WindowManager::setPeekSize(TabId tab, Edge edge, QSize size)
{
    return setPeekSizePolicy(tab, edge, {size.width(), size.height()});
}
bool WindowManager::setPeekSizePolicy(TabId tab, Edge edge, const PeekSizePolicy& size)
{
    const auto* record = d->model.tab(tab);
    if (!record || (edge != Edge::Left && edge != Edge::Right && edge != Edge::Top && edge != Edge::Bottom)) return false;
    const auto& existing = edge == Edge::Left || edge == Edge::Right
        ? record->verticalPeekSize : record->horizontalPeekSize;
    if (existing == size) return true;
    const auto result = d->model.setPeekSizePolicy(tab, edge, size);
    if (!result) return false;
    const QPointer<WindowManager> self(this);
    const auto* group = d->model.group(d->model.groupForTab(tab));
    ++d->geometryUpdateDepth;
    const auto releaseGeometry = qScopeGuard([self] { if (self) --self->d->geometryUpdateDepth; });
    if (group) if (auto window = d->windows.value(group->windowId)) window->surface()->updatePeekSize(tab);
    if (!self) return false;
    emit layoutChanged();
    return true;
}
bool WindowManager::resetPeekSize(TabId tab, Edge edge)
{
    return setPeekSizePolicy(tab, edge, {});
}
bool WindowManager::moveGroupToCenter(GroupId group) { return applyResult(d->model.moveGroupToCenter(group)); }
bool WindowManager::promoteWindow(WindowId window) { d->drag->releaseForMutation(); return applyResult(d->model.promoteWindow(window)); }
bool WindowManager::demoteWindow(WindowId window, WindowId target) { d->drag->releaseForMutation(); return applyResult(d->model.demoteWindow(window, target)); }
bool WindowManager::reassignFamily(WindowId window, WindowId target) { return applyResult(d->model.reassignFamily(window, target)); }
TabId WindowManager::extractPane(TabId tab, PaneId pane, WindowKind kind)
{
    if (!d->model.panes(tab).contains(pane)) return {};
    if (d->model.panes(tab).size() == 1) { detachTab(tab, kind); return tab; }
    auto candidate = d->model;
    const auto sourceWindow = candidate.group(candidate.groupForTab(tab))->windowId;
    const auto root = candidate.familyRoot(candidate.window(sourceWindow)->familyId);
    const auto newWindow = candidate.createWindow(WindowKind::Workbench);
    const auto placeholder = candidate.addTab(newWindow, QCoreApplication::translate("WindowSystem", "Temporary"));
    const auto targetGroup = candidate.groupForTab(placeholder);
    const auto extracted = candidate.extractPane(tab, pane, targetGroup);
    if (extracted.isNull()) return {};
    candidate.removeTab(placeholder);
    if (kind == WindowKind::Floating && !candidate.demoteWindow(newWindow, root)) return {};
    d->model = std::move(candidate);
    auto options = d->options.value(sourceWindow);
    const auto bars = d->visibilityProfiles.value(kind);
    options.toolbarsVisible = bars.toolbars; options.statusBarVisible = bars.statusBar;
    options.tabBarsVisible = bars.tabBars; options.sideBarsVisible = bars.sideBars;
    d->options.insert(newWindow, options); d->visibilityKinds.insert(newWindow, kind); d->windowVisibility[newWindow].insert(kind, bars);
    refresh(); showWindow(newWindow); activateTab(extracted); return extracted;
}
void WindowManager::showWindow(WindowId window)
{
    const QPointer<WindowManager> self(this);
    if (QPointer<WorkspaceWindow> widget = d->windows.value(window)) {
        // Resolve a new window's normal rectangle before starting ShowWindow.
        widget->fitInitialPlacement();
        if (!self || !widget || !d->model.window(window)) return;
        if (widget->isMinimized()) {
            if (viewMode(window) == ViewMode::Normal) widget->showNormal();
            else widget->showFullScreen();
        } else widget->show();
        if (!self || !widget || !d->model.window(window)) return;
        widget->raise();
        if (!self || !widget || !d->model.window(window)) return;
        widget->activateWindow();
        if (!self || !widget || !d->model.window(window)) return;
        d->activeWindow = window;
    }
}
bool WindowManager::activateWindow(WindowId window)
{
    if (!d->model.window(window)) return false;
    const QPointer<WindowManager> self(this);
    QPointer<WorkspaceWindow> view = d->windows.value(window);
    if (!view) return false;
    showWindow(window);
    if (!self || !view || !d->model.window(window)) return false;
    if (view->isMinimized()) view->showNormal();
    if (!self || !view || !d->model.window(window)) return false;
    view->raise();
    if (!self || !view || !d->model.window(window)) return false;
    view->activateWindow();
    if (!self || !view || !d->model.window(window)) return false;
    const auto ctx = context(window);
    if (!ctx.tab.isNull() && !ctx.pane.isNull()) return focusPane(ctx.tab, ctx.pane);
    return true;
}
bool WindowManager::activateTab(TabId tab)
{
    if (!d->model.tab(tab)) return false;
    const QPointer<WindowManager> self(this);
    if (!ensureTabView(tab)) return false;
    if (!self) return false;
    leaveFocusScopeFor(tab, {});
    if (!self) return false;
    const auto result = d->model.activateTab(tab); if (!result) return false;
    const auto group = d->model.groupForTab(tab); const auto window = d->model.group(group)->windowId;
    if (QPointer<WorkspaceWindow> widget = d->windows.value(window)) {
        widget->surface()->selectTab(tab);
        if (!self) return false;
        if (widget) widget->updateCaption();
        if (!self) return false;
    }
    const auto* active = d->model.tab(tab);
    if (!active) return false;
    const bool focused = focusPane(tab, active->activePane);
    if (focused) scheduleContentVisibilityNotification();
    return focused;
}
bool WindowManager::focusPane(TabId tab, PaneId pane)
{
    if (!d->model.panes(tab).contains(pane)) return false;
    const QPointer<WindowManager> self(this);
    if (!ensureTabView(tab)) return false;
    if (!self) return false;
    leaveFocusScopeFor(tab, pane);
    if (!self) return false;
    if (!d->model.activatePane(tab, pane)) return false;
    if (!d->directionalNavigation) d->navigationAnchor.reset();
    const QPointer<QWidget> widget = d->panes.value(pane);
    if (!widget) return false;
    const auto targetAlive = [self, widget, tab, pane] {
        return self && widget && self->d->panes.value(pane) == widget
            && self->d->model.panes(tab).contains(pane);
    };
    QVector<QPointer<QWidget>> frames;
    if (auto view = d->tabs.value(tab)) {
        const auto renderedFrames = view->paneFrames();
        frames.reserve(renderedFrames.size());
        for (auto* frame : renderedFrames) frames.append(frame);
    }
    for (const auto& frame : frames) {
        if (!targetAlive()) return false;
        if (!frame) continue;
        frame->setProperty("active", frame->property("paneId").toUuid() == pane);
        if (!targetAlive()) return false;
        if (!frame) continue;
        frame->style()->unpolish(frame);
        if (!targetAlive()) return false;
        if (!frame) continue;
        frame->style()->polish(frame);
        if (!targetAlive()) return false;
        if (frame) frame->update();
    }
    if (!targetAlive()) return false;
    if (auto view = d->tabs.value(tab)) view->setActivePane(pane);
    if (!targetAlive()) return false;
    // A composite pane may route root focus to a default control. Keep its
    // current editor when refreshing or selecting an already focused pane.
    auto* focused = QApplication::focusWidget();
    if (focused != widget && (!focused || !widget->isAncestorOf(focused)))
        widget->setFocus(Qt::OtherFocusReason);
    return targetAlive();
}
QWidget* WindowManager::windowWidget(WindowId window) const { return d->windows.value(window); }
QWidget* WindowManager::paneWidget(PaneId pane) const { return d->panes.value(pane); }
const LayoutModel& WindowManager::model() const { return d->model; }
CommandContext WindowManager::context(WindowId window) const
{
    if (window.isNull()) window = d->activeWindow;
    if (!d->model.window(window)) {
        const auto order = d->model.windowOrder(); if (order.isEmpty()) return {}; window = order.first();
    }
    CommandContext result; result.window = window;
    const auto* record = d->model.window(window); result.group = record->activeGroup;
    const auto* group = d->model.group(result.group);
    if (!group) { const auto groups = d->model.centralGroups(window); if (!groups.isEmpty()) result.group = groups.first(); group = d->model.group(result.group); }
    if (group) result.tab = group->activeTab;
    if (const auto* tab = d->model.tab(result.tab)) result.pane = tab->activePane;
    return result;
}
WindowBehavior WindowManager::behavior() const { return d->behavior; }
void WindowManager::setBehavior(const WindowBehavior& behavior)
{
    if (behavior.newTabPosition < NewTabPosition::Beginning || behavior.newTabPosition > NewTabPosition::End
        || !validZenFullscreenMode(behavior.zenFullscreenMode)
        || inputMethodScopeKey(behavior.inputMethodScope).isEmpty()) return;
    const bool updateTabBars = behavior.hideSingleTabWorkbench != d->behavior.hideSingleTabWorkbench
        || behavior.hideSingleTabFloating != d->behavior.hideSingleTabFloating;
    d->inputMethods.setScope(behavior.inputMethodScope);
    d->inputMethods.synchronize(d->model);
    d->inputMethods.focus(d->model, QApplication::focusWidget());
    d->behavior = behavior; d->behavior.revealDelayMs = qBound(200, behavior.revealDelayMs, 2000);
    const QPointer<WindowManager> self(this);
    if (updateTabBars) applyWindowViewStates();
    if (!self) return;
    const auto windows = d->windows;
    for (const auto& window : windows) {
        if (window && window->strips()) window->strips()->setHideOptionsWhenLocked(behavior.hideToolbarOptionsWhenLocked);
        if (!self) return;
        if (window) window->setCaptionButtonOrder(behavior.captionButtonOrder);
        if (!self) return;
    }
    emit layoutChanged();
}
void WindowManager::setTheme(const Theme& theme)
{
    d->authoredTheme = theme;
    d->theme = scaledInterfaceTheme(theme);
    ++d->themeRevision;
    // Tab strips paint from the theme itself, not only the style sheet.
    for (auto widget : d->windows) {
        if (!widget) continue;
        applyTheme(widget, d->theme);
        widget->surface()->refreshInterfaceMetrics();
        for (auto* frame : widget->findChildren<QFrame*>("TabGroup"))
            if (auto* tabs = dynamic_cast<GroupTabs*>(frame)) tabs->setTheme(d->theme);
    }
}
Theme WindowManager::theme() const { return d->authoredTheme; }
Theme WindowManager::interfaceTheme() const { return d->theme; }
CommandRegistry& WindowManager::commands() { return d->commands; }
bool WindowManager::execute(const QString& command, const QVariantMap& args, WindowId window)
{
    auto ctx = context(window); ctx.args = args;
    return executeResolved(command, std::move(ctx));
}
bool WindowManager::validCommandContext(const CommandContext& ctx) const
{
    if (!ctx.window.isNull() && !d->model.window(ctx.window)) return false;
    if (!ctx.group.isNull()) {
        const auto* group = d->model.group(ctx.group);
        if (!group || group->windowId != ctx.window) return false;
        if (!ctx.tab.isNull() && !group->tabs.contains(ctx.tab)) return false;
    }
    if (!ctx.tab.isNull()) {
        const auto* group = d->model.group(d->model.groupForTab(ctx.tab));
        if (!group || group->windowId != ctx.window) return false;
    }
    return ctx.pane.isNull() || d->model.panes(ctx.tab).contains(ctx.pane);
}
bool WindowManager::executeInContext(const QString& command, const CommandContext& ctx)
{
    return validCommandContext(ctx) && executeResolved(command, ctx, true);
}
bool WindowManager::executeResolved(const QString& command, CommandContext ctx, bool requireGroup)
{
    const QPointer<WindowManager> self(this);
    const bool executed = requireGroup ? d->commands.executeValidated(command, ctx)
                                      : d->commands.execute(command, ctx);
    if (!self) return false;
    if (executed) return true;
    emit commandFailed(QCoreApplication::translate("WindowSystem", "Command is unavailable: %1").arg(command)); return false;
}
void WindowManager::showCommandPalette(WindowId window, const FloatingPanelOptions& options) { const auto ctx = context(window); openCommandPalette(this, ctx, windowWidget(ctx.window), options); }
void WindowManager::showWindowSelector(WindowId window, const FloatingPanelOptions& options) { const auto ctx = context(window); openWindowSelector(this, ctx.window, windowWidget(ctx.window), options); }
void WindowManager::showSettings(WindowId window) { const auto ctx = context(window); openBehaviorSettings(this, windowWidget(ctx.window)); }
bool WindowManager::resetToolbarWidth(WindowId window, const QString& stripId)
{
    const auto view = d->windows.value(window);
    return view && view->strips() && view->strips()->resetStripWidth(stripId);
}
void WindowManager::setStatus(WindowId window, const QString& text) { if (auto widget = d->windows.value(window)) widget->setStatus(text); }
bool WindowManager::setStatusItemVisible(WindowId window, const QString& item, bool visible)
{
    if (!d->model.window(window) || !statusItemIds().contains(item)) return false;
    auto options = d->options.value(window);
    if (visible) options.hiddenStatusItems.removeAll(item);
    else if (!options.hiddenStatusItems.contains(item)) options.hiddenStatusItems.append(item);
    if (options.hiddenStatusItems == d->options.value(window).hiddenStatusItems) return true;
    const QPointer<WindowManager> self(this);
    d->options.insert(window, options);
    if (auto view = d->windows.value(window)) view->setOptions(options);
    if (!self) return false;
    emit layoutChanged(); return true;
}
bool WindowManager::statusItemVisible(WindowId window, const QString& item) const
{
    return d->model.window(window) && statusItemIds().contains(item)
        && !d->options.value(window).hiddenStatusItems.contains(item);
}
bool WindowManager::applyResult(const OperationResult& result)
{
    if (!result) { emit commandFailed(result.error); return false; }
    refresh(); return true;
}
void WindowManager::refresh(bool preserveDrag)
{
    d->navigationAnchor.reset();
    if (d->refreshing || d->destroying) return;
    const QPointer<WindowManager> self(this);
    d->inputMethods.synchronize(d->model);
    const QPointer<QWidget> previousFocus = QApplication::focusWidget();
    d->refreshing = true;
    if (!preserveDrag) d->drag->releaseForMutation();
    for (auto window : d->windows) if (window) window->surface()->captureStripScrollOffsets();
    for (auto window : d->windows) if (window) window->surface()->closePeek();
    // A view whose pane tree, content, pane bars and splitter metric are all
    // unchanged keeps its frames; its content and bars stay mounted there.
    QSet<TabId> reusable;
    for (auto it = d->tabs.cbegin(); it != d->tabs.cend(); ++it)
        if (const auto* record = d->model.tab(it.key()); record && it.value()
            && it.value()->canReuse(*record, d->panes, d->paneTopBars, d->paneBottomBars)) reusable.insert(it.key());
    const auto mounted = [&](PaneId pane) { return reusable.contains(d->model.tabForPane(pane)); };
    const auto panes = d->panes;
    for (auto it = panes.cbegin(); it != panes.cend(); ++it)
        if (it.value() && !mounted(it.key())) { it.value()->setParent(d->storage); it.value()->hide(); }
    for (const auto& bars : {d->paneTopBars, d->paneBottomBars})
        for (auto it = bars.cbegin(); it != bars.cend(); ++it)
            if (it.value() && !mounted(it.key())) { it.value()->setParent(d->storage); it.value()->hide(); }
    // A view stays mounted while its tab remains in the same window; that
    // window's surface moves it between its groups. Views changing window or
    // being deleted are parked so that no old surface deletes them.
    const auto views = d->tabs;
    QVector<QPointer<TabContent>> crossing;
    QVector<PaneId> crossingPanes;
    for (auto it = views.cbegin(); it != views.cend(); ++it) {
        const auto& view = it.value();
        if (!view) continue;
        const auto* group = d->model.group(d->model.groupForTab(it.key()));
        const auto target = group ? d->windows.value(group->windowId) : QPointer<WorkspaceWindow>{};
        if (!target || view->window() == target.data() || view->window() == d->storage) continue;
        crossing.append(view);
        crossingPanes += d->model.panes(it.key());
    }
    for (const auto& view : crossing) if (view) view->hide();
    if (!self) return;
    const auto visibilityState = d->model.stateId();
    reportPanesHiddenBeforeReparent(crossingPanes);
    if (!self) return;
    if (d->model.stateId() != visibilityState) {
        d->refreshing = false;
        QTimer::singleShot(0, this, [self] { if (self) self->refresh(); });
        return;
    }
    for (auto it = views.cbegin(); it != views.cend(); ++it) {
        const auto& view = it.value();
        if (!view) continue;
        const auto* group = d->model.group(d->model.groupForTab(it.key()));
        const auto target = group ? d->windows.value(group->windowId) : QPointer<WorkspaceWindow>{};
        if (target && view->window() == target.data()) continue;
        view->setParent(d->storage);
        if (view) view->hide();
    }
    QSet<PaneId> livePanes;
    for (auto it = d->model.tabs().cbegin(); it != d->model.tabs().cend(); ++it)
        for (const auto pane : d->model.panes(it.key())) livePanes.insert(pane);
    for (auto it = d->panes.begin(); it != d->panes.end();) {
        if (!livePanes.contains(it.key())) { if (it.value()) it.value()->deleteLater(); d->descriptors.remove(it.key()); it = d->panes.erase(it); }
        else ++it;
    }
    for (auto* bars : {&d->paneTopBars, &d->paneBottomBars})
        for (auto it = bars->begin(); it != bars->end();) {
            if (!livePanes.contains(it.key())) { if (it.value()) it.value()->deleteLater(); it = bars->erase(it); }
            else ++it;
        }
    for (auto it = d->tabs.begin(); it != d->tabs.end();) {
        if (!d->model.tab(it.key())) { if (it.value()) it.value()->deleteLater(); it = d->tabs.erase(it); }
        else ++it;
    }
    for (auto* bars : {&d->pendingTabTopBars, &d->pendingTabBottomBars}) {
        for (auto it = bars->begin(); it != bars->end();) {
            if (!d->model.tab(it.key())) {
                if (it.value()) it.value()->deleteLater();
                it = bars->erase(it);
            } else ++it;
        }
    }
    // A focused pane can retain its ID while moving into a different tab in
    // this window. Activate that target before rebuilding its containing stack.
    for (const auto id : d->model.windowOrder()) reconcileWindowViewState(id);
    // Materialize only groups that can be visible. An auto-hide group creates
    // its view when its rail entry is opened, while previously visited tabs
    // keep the view they already own.
    const auto materialize = [&](TabId tab) {
        if (tab.isNull() || !ensureTabView(tab)) {
            if (!self) return false;
            // A synchronous host callback can remove the target while its view
            // is being prepared. Settle this transaction and retry the model
            // snapshot instead of leaving refresh() permanently reentrant-locked.
            d->refreshing = false;
            QTimer::singleShot(0, this, [self] { if (self) self->refresh(); });
            return false;
        }
        return bool(self);
    };
    for (const auto window : d->model.windowOrder()) {
        const auto* record = d->model.window(window);
        if (!record) continue;
        for (const auto group : d->model.centralGroups(window))
            if (const auto* groupRecord = d->model.group(group);
                groupRecord && !materialize(groupRecord->activeTab)) return;
        for (const auto group : record->edgeGroups) {
            const auto* groupRecord = d->model.group(group);
            if (groupRecord && groupRecord->location == GroupLocation::Pinned
                && !materialize(groupRecord->activeTab)) return;
        }
        const auto state = windowViewState(window);
        if (state.mode == ViewMode::GroupFullscreen || state.mode == ViewMode::PaneFullscreen) {
            const auto* groupRecord = d->model.group(state.group);
            if (groupRecord && groupRecord->location == GroupLocation::AutoHide) {
                const auto target = state.mode == ViewMode::PaneFullscreen ? state.tab : groupRecord->activeTab;
                if (!materialize(target)) return;
            }
        }
    }
    // Reattach all existing pane widgets before rebuilding hosts, preserving UI
    // state and avoiding destruction through their previous QWidget parent.
    for (auto it = d->tabs.cbegin(); it != d->tabs.cend(); ++it) {
        const auto* record = d->model.tab(it.key());
        const QPointer<TabContent> view = it.value();
        if (!record || !view) continue;
        const auto options = d->descriptors.value(record->activePane);
        view->rebuild(*record, d->panes, d->paneTopBars, d->paneBottomBars);
        if (!self || !view) return;
        view->setBarSharing(options.topBarSharing, options.bottomBarSharing);
        if (!self) return;
    }
    for (auto it = d->windows.begin(); it != d->windows.end();) {
        if (!d->model.window(it.key())) {
            if (it.value()) { it.value()->allowClose = true; it.value()->hide(); it.value()->deleteLater(); }
            d->options.remove(it.key()); d->viewStates.remove(it.key());
            d->windowVisibility.remove(it.key()); d->visibilityKinds.remove(it.key()); it = d->windows.erase(it);
        } else ++it;
    }
    for (const auto id : d->model.windowOrder()) {
        syncVisibilityKind(id);
        auto& window = d->windows[id];
        if (!window) window = new WorkspaceWindow(this, id, d->options.value(id));
        window->setOptions(d->options.value(id));
        window->syncKind(); window->surface()->rebuild(d->tabs);
        reconcileWindowViewState(id); window->applyViewState(windowViewState(id));
        window->updateCaption();
    }
    d->refreshing = false;
    d->inputMethods.focus(d->model, QApplication::focusWidget());
    const auto active = context();
    if (auto window = d->windows.value(active.window); window && window->isVisible() && window->isActiveWindow()) {
        // Replacing a strip temporarily unmounts its pages and loses Qt focus.
        // Restore only a surviving editor in the resulting active pane.
        const auto content = d->panes.value(active.pane);
        if (previousFocus && content && previousFocus->isVisible() && previousFocus->isEnabled()
            && (previousFocus == content || content->isAncestorOf(previousFocus)))
            previousFocus->setFocus(Qt::OtherFocusReason);
        if (!self) return;
        if (!active.pane.isNull()) focusPane(active.tab, active.pane);
    }
    if (!self) return;
    const bool emptyAtNotification = d->model.windows().isEmpty();
    const auto notificationRevision = d->model.revision();
    emit layoutChanged();
    if (!self) return;
    // A direct layout listener may already close everything in a nested
    // refresh. That inner transaction owns its terminal notification.
    if (emptyAtNotification && d->model.windows().isEmpty()
        && d->model.revision() == notificationRevision) emit allWindowsClosed();
    if (!self) return;
    scheduleContentVisibilityNotification();
    if (d->behavior.closeEmptyWorkbench) {
        QVector<WindowId> empty;
        for (const auto id : d->model.windowOrder()) {
            const auto* window = d->model.window(id);
            if (window->kind == WindowKind::Workbench && !d->model.familyHasTabs(window->familyId)) empty << id;
        }
        // Defer and revalidate; creation can add a tab before this turn completes.
        QTimer::singleShot(0, this, [this, empty] {
            for (const auto id : empty) if (const auto* record = d->model.window(id))
                if (!d->model.familyHasTabs(record->familyId)) requestCloseWindow(id);
        });
    }
}
void WindowManager::updateActive(WindowId window, GroupId group, TabId tab)
{
    Q_UNUSED(group)
    if (d->refreshing || d->projectingViews.contains(window) || !d->model.tab(tab)) return;
    const QPointer<WindowManager> self(this);
    if (!ensureTabView(tab) || !self) return;
    d->activeWindow = window; d->model.activateTab(tab);
    if (auto widget = d->windows.value(window)) { widget->updateCaption(); widget->surface()->refreshGroupFocus(); }
    // The drag transaction publishes after its focus/placement restoration.
    // A host may destroy the manager in that notification, which must happen
    // after Qt has returned from setFocus on the content being restored.
    if (d->drag->hasStableLayout()) return;
    emit layoutChanged();
    if (self) scheduleContentVisibilityNotification();
}
void WindowManager::beginTabDrag(TabId tab, QPoint global) { cancelPendingShortcut(); d->drag->beginTab(tab, global); }
void WindowManager::beginTabFolderDrag(FolderId folder, QPoint global) { cancelPendingShortcut(); d->drag->beginFolder(folder, global); }
void WindowManager::beginWindowDrag(WindowId window, QPoint global) { cancelPendingShortcut(); d->drag->beginWindow(window, global); }

bool WindowManager::focusGroup(GroupId group)
{
    const auto* record = d->model.group(group); if (!record) return false;
    return activateTab(record->activeTab);
}
void WindowManager::resizeActive(Placement direction, bool panes, const CommandContext& ctx)
{
    const auto* window = d->model.window(ctx.window); const auto* tab = d->model.tab(ctx.tab);
    const auto* group = d->model.group(ctx.group);
    const auto target = panes ? ctx.pane : ctx.group;
    const auto root = panes ? (tab ? tab->panes : LayoutTree{})
        : group && group->location == GroupLocation::Pinned ? d->model.pinnedLayout(ctx.window, group->edge)
        : (window ? window->central : LayoutTree{});
    const auto orientation = direction == Placement::Left || direction == Placement::Right ? Qt::Horizontal : Qt::Vertical;
    std::function<bool(LayoutTree)> adjust = [&](LayoutTree node) {
        if (!node || node->isLeaf()) return false;
        std::function<bool(LayoutTree)> contains = [&](LayoutTree child) { return child && (child->isLeaf() ? child->itemId == target : contains(child->first) || contains(child->second)); };
        if (adjust(contains(node->first) ? node->first : node->second)) return true;
        if (node->orientation != orientation) return false;
        const auto delta = direction == Placement::Left || direction == Placement::Top ? -0.05 : 0.05;
        return applyResult(d->model.setSplitRatio(node->id, qBound(0.1, node->ratio + delta, 0.9)));
    };
    adjust(root);
}
void WindowManager::focusWindowCycle(bool forward, const QString& scope)
{
    const auto ctx = context(); const auto* source = d->model.window(ctx.window); if (!source) return;
    QVector<WindowId> targets;
    for (const auto id : d->model.windowOrder()) {
        const auto* record = d->model.window(id); auto* widget = windowWidget(id);
        if (!widget || widget->isMinimized()) continue;
        if (scope == "family" && record->familyId != source->familyId) continue;
        if (scope == "workbenches" && record->kind != WindowKind::Workbench) continue;
        targets << id;
    }
    if (targets.isEmpty()) return;
    const auto next = (targets.indexOf(ctx.window) + (forward ? 1 : targets.size() - 1) + targets.size()) % targets.size();
    showWindow(targets[next]); activateTab(context(targets[next]).tab);
}
void WindowManager::cancelPendingShortcut()
{
    const auto window = d->keyWindow;
    d->pendingKeys = {}; d->keyWindow = WindowId{}; d->keyFocus.clear(); d->keyContext = {};
    d->keyTimer->stop();
    if (auto widget = d->windows.value(window)) widget->setTransientStatus({});
}

bool WindowManager::eventFilter(QObject* watched, QEvent* event)
{
    // Stateless value-control rules apply even while the layout refreshes:
    // controls created then are polished and scrolled over like any other.
    removeValueControlWheelFocus(watched, event);
    balanceToolTipShape(watched, event);
    if (redirectValueControlWheel(watched, event)) return true;
    if (filterSmoothScroll(watched, event)) return true;
    if (d->destroying || d->refreshing) return false;
    auto* widget = qobject_cast<QWidget*>(watched);
    const auto cancelChord = [this] { cancelPendingShortcut(); };
    if (!d->pendingKeys.isEmpty()
        && (event->type() == QEvent::ApplicationDeactivate || event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::InputMethod
            || (event->type() == QEvent::WindowDeactivate && widget
                && widget->window()->property("windowId").toUuid() == d->keyWindow)
            || (event->type() == QEvent::FocusOut && widget == d->keyFocus))) cancelChord();
    if (widget && (event->type() == QEvent::Move || event->type() == QEvent::Resize)
        && d->navigationAnchor && widget->property("windowId").toUuid() == d->navigationAnchor->window)
        d->navigationAnchor.reset();
    if (event->type() == QEvent::FocusIn && widget) {
        if (d->projectingViews.contains(widget->window()->property("windowId").toUuid())) return false;
        const QPointer<WindowManager> self(this);
        if (static_cast<QFocusEvent*>(event)->reason() == Qt::MouseFocusReason) d->navigationAnchor.reset();
        for (auto* current = widget; current; current = current->parentWidget()) {
            const auto pane = current->property("paneId").toUuid();
            if (!pane.isNull()) {
                if (const auto tabId = d->model.tabForPane(pane); !tabId.isNull()) {
                    leaveFocusScopeFor(tabId, pane);
                    if (!self) return true;
                    if (!d->model.activatePane(tabId, pane)) return false;
                    if (auto view = d->tabs.value(tabId)) view->setActivePane(pane);
                    const auto group = d->model.groupForTab(tabId);
                    const auto* groupRecord = d->model.group(group);
                    if (!groupRecord) return false;
                    updateActive(groupRecord->windowId, group, tabId);
                    // The host can destroy its manager in a synchronous layout
                    // listener while this focus event is still being delivered.
                    if (!self) return true;
                }
                break;
            }
        }
        cancelChord();
    }
    if (event->type() == QEvent::ContextMenu && widget && !d->drag->isActive()) {
        for (auto* current = widget; current; current = current->parentWidget()) {
            const auto pane = current->property("paneId").toUuid();
            if (pane.isNull()) continue;
            if (const auto tab = d->model.tabForPane(pane); !tab.isNull()) {
                showPaneMenu(tab, pane, static_cast<QContextMenuEvent*>(event)->globalPos(), widget);
                return true;
            }
            break;
        }
    }
    if ((event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride)
        || d->drag->isActive() || !widget) return false;
    for (auto* current = widget; current; current = current->parentWidget()) if (qobject_cast<QKeySequenceEdit*>(current)) {
        cancelChord(); return false;
    }
    const auto windowId = widget->window()->property("windowId").toUuid();
    if (windowId.isNull()) { cancelChord(); return false; } // Dialogs retain their native input handling.
    if (!d->pendingKeys.isEmpty() && d->keyWindow != windowId) cancelChord();
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Control || key->key() == Qt::Key_Shift || key->key() == Qt::Key_Alt
        || key->key() == Qt::Key_Meta || key->key() == Qt::Key_AltGr || !key->key() || key->key() == Qt::Key_unknown) return false;
    if (!d->pendingKeys.isEmpty() && key->key() == Qt::Key_Escape) {
        key->accept();
        if (event->type() == QEvent::KeyPress) cancelChord();
        return true;
    }
    if (!d->pendingKeys.isEmpty() && key->isAutoRepeat()) { key->accept(); return true; }
    QVector<QKeyCombination> combinations;
    for (int i = 0; i < d->pendingKeys.count(); ++i) combinations << d->pendingKeys[i];
    if (combinations.size() == 4) { cancelChord(); key->accept(); return true; }
    combinations << key->keyCombination();
    const auto emptyKey = QKeyCombination::fromCombined(0);
    const QKeySequence sequence(combinations.value(0, emptyKey), combinations.value(1, emptyKey),
                                combinations.value(2, emptyKey), combinations.value(3, emptyKey));
    const bool continuing = !d->pendingKeys.isEmpty();
    const auto ctx = continuing ? d->keyContext : context(windowId);
    // Only bindings sharing the first stroke can match. The retained copy keeps
    // enabled callbacks alive even if one of them changes the registry.
    const auto candidates = d->shortcutCandidates(combinations.first());
    const QPointer<WindowManager> self(this);
    const QPointer<QWidget> receiver(widget);
    const auto bindingRevision = d->bindingsRevision;
    QString exact;
    QVariantMap exactArgs;
    bool partial = false;
    for (const auto& candidate : candidates) {
        const auto match = sequence.matches(candidate.sequence);
        if (match == QKeySequence::NoMatch) continue;
        auto target = ctx; target.args = candidate.args;
        const bool enabled = !candidate.enabled || candidate.enabled(target);
        if (!self || !receiver) return true;
        if (bindingRevision != d->bindingsRevision) { key->accept(); return true; }
        if (!enabled) continue;
        if (event->type() == QEvent::ShortcutOverride) { key->accept(); return true; }
        if (match == QKeySequence::ExactMatch) {
            exact = candidate.command; exactArgs = candidate.args; break;
        }
        partial = true;
    }
    // Override events reserve only the input. Advancing the chord here would
    // process the same physical stroke twice when KeyPress follows it.
    if (event->type() == QEvent::ShortcutOverride) {
        if (continuing) { key->accept(); return true; }
        return false;
    }
    if (!exact.isEmpty()) {
        cancelChord(); key->accept();
        auto target = ctx; target.args = exactArgs;
        // The command may destroy this manager; the flag lives in its state.
        d->dispatchingShortcut = true;
        executeInContext(exact, target);
        if (self) d->dispatchingShortcut = false;
        return true;
    }
    if (partial && validCommandContext(ctx)) {
        d->pendingKeys = sequence; d->keyWindow = windowId; d->keyFocus = widget; d->keyContext = ctx;
        d->keyTimer->start(1500); key->accept();
        if (auto window = d->windows.value(windowId)) window->setTransientStatus(
            QCoreApplication::translate("WindowSystem", "Waiting for next key: %1").arg(sequence.toString()));
        return true;
    }
    if (continuing) { cancelChord(); key->accept(); return true; }
    return false;
}

} // namespace window_system
