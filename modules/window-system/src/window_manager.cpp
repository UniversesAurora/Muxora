#include "window_manager_p.hpp"
#include <window_system/window_manager.hpp>
#include "chrome/workspace_window.hpp"
#include "chrome/command_strip.hpp"
#include "commands/command_dialogs.hpp"
#include "docking/dock_surface.hpp"
#include "docking/drag_controller.hpp"
#include "panes/tab_content.hpp"
#include <QApplication>
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
WindowManager::WindowManager(QObject* parent) : QObject(parent), d(std::make_unique<Private>())
{
    Q_ASSERT(qApp);
    d->storage = new QWidget; d->storage->setObjectName("WindowSystemStorage"); d->storage->hide();
    d->drag = new DragController(this);
    d->keyTimer = new QTimer(this); d->keyTimer->setSingleShot(true);
    QObject::connect(d->keyTimer, &QTimer::timeout, this, [this] { d->pendingKeys = {}; });
    qApp->installEventFilter(this); registerWindowCommands();
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
    if (kind == WindowKind::Floating && owner.isNull()) {
        const auto ctx = context(); const auto* record = d->model.window(ctx.window);
        if (record) owner = d->model.familyRoot(record->familyId);
    }
    const auto id = d->model.createWindow(kind, owner);
    if (id.isNull()) return {};
    d->options.insert(id, options); refresh(); return id;
}
TabId WindowManager::addTab(WindowId window, QWidget* content, const TabOptions& options, GroupId group)
{
    if (!content || content->property("windowSystemManaged").toBool()
        || (options.topBarSharing != PaneBarSharing::Shared && options.topBarSharing != PaneBarSharing::PerPane)
        || (options.bottomBarSharing != PaneBarSharing::Shared && options.bottomBarSharing != PaneBarSharing::PerPane)) return {};
    const auto id = d->model.addTab(window, options.title, group);
    if (id.isNull()) return {};
    const auto pane = d->model.tab(id)->activePane;
    content->setProperty("windowSystemManaged", true); content->setProperty("paneId", pane);
    if (content->focusPolicy() == Qt::NoFocus) content->setFocusPolicy(Qt::StrongFocus);
    content->setParent(d->storage); d->panes.insert(pane, content); d->descriptors.insert(pane, options);
    refresh(); activateTab(id); return id;
}
void WindowManager::setContentFactory(ContentFactory factory) { d->factory = std::move(factory); }
QWidget* WindowManager::createContent(const TabOptions& options, PaneId pane)
{
    if (isInternalSettingsContentType(options.contentType)) return createInternalSettingsContent(this, options);
    return d->factory ? d->factory(options, pane) : nullptr;
}
void WindowManager::setCloseGuard(CloseGuard guard) { d->closeGuard = std::move(guard); }
bool WindowManager::setTabTopBar(TabId tab, QWidget* bar)
{
    auto view = d->tabs.value(tab); if (!view || (bar && bar->property("windowSystemManaged").toBool())) return false;
    if (bar) bar->setProperty("windowSystemManaged", true);
    view->setTopBar(bar);
    return true;
}
bool WindowManager::setTabBottomBar(TabId tab, QWidget* bar)
{
    auto view = d->tabs.value(tab); if (!view || (bar && bar->property("windowSystemManaged").toBool())) return false;
    if (bar) bar->setProperty("windowSystemManaged", true);
    view->setBottomBar(bar);
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
    if (auto view = d->tabs.value(tab)) view->setBarSharing(options.topBarSharing, options.bottomBarSharing);
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

    const auto before = saveLayout();
    const auto windows = d->windows;
    const auto panes = d->panes;
    auto unchanged = [this, &before, &windows, &panes] {
        if (saveLayout() != before || d->windows.size() != windows.size() || d->panes.size() != panes.size()) return false;
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
bool WindowManager::moveTab(TabId tab, GroupId group, Placement placement, int index)
{ return applyResult(d->model.moveTab(tab, group, placement, index)); }
WindowId WindowManager::detachTab(TabId tab, WindowKind kind)
{
    return detachTabImpl(tab, kind, {});
}
WindowId WindowManager::detachTabImpl(TabId tab, WindowKind kind, const std::function<void(QWidget*)>& beforeShow, bool preserveDrag)
{
    const QPointer<WindowManager> self(this);
    const auto source = context(d->model.group(d->model.groupForTab(tab)) ? d->model.group(d->model.groupForTab(tab))->windowId : WindowId{});
    const auto id = d->model.detachTab(tab, kind); if (id.isNull()) return {};
    auto options = d->options.value(source.window); options.size = QSize(880, 560); d->options.insert(id, options);
    refresh(preserveDrag);
    if (!self) return {};
    if (beforeShow) if (auto* window = windowWidget(id)) beforeShow(window);
    if (!self) return {};
    showWindow(id);
    if (!self) return {};
    activateTab(tab);
    return self ? id : WindowId{};
}
bool WindowManager::moveAllTabs(WindowId source, GroupId target, Placement placement, int insertionIndex)
{ return applyResult(d->model.moveAllTabs(source, target, placement, insertionIndex)); }
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
QSize WindowManager::peekSize(TabId tab) const
{
    const auto* record = d->model.tab(tab);
    return record ? record->peekSize.value_or(QSize(250, 170)) : QSize{};
}
bool WindowManager::setPeekSize(TabId tab, QSize size)
{
    const auto* record = d->model.tab(tab);
    if (!record) return false;
    if (record->peekSize == size) return true;
    const auto result = d->model.setPeekSize(tab, size);
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
bool WindowManager::resetPeekSize(TabId tab)
{
    const auto* record = d->model.tab(tab);
    if (!record) return false;
    if (!record->peekSize) return true;
    const auto result = d->model.setPeekSize(tab, std::nullopt);
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
    const auto placeholder = candidate.addTab(newWindow, QStringLiteral("临时"));
    const auto targetGroup = candidate.groupForTab(placeholder);
    const auto extracted = candidate.extractPane(tab, pane, targetGroup);
    if (extracted.isNull()) return {};
    candidate.removeTab(placeholder);
    if (kind == WindowKind::Floating && !candidate.demoteWindow(newWindow, root)) return {};
    d->model = std::move(candidate); d->options.insert(newWindow, d->options.value(sourceWindow));
    refresh(); showWindow(newWindow); activateTab(extracted); return extracted;
}
void WindowManager::showWindow(WindowId window)
{
    if (auto widget = d->windows.value(window)) {
        if (widget->isMinimized()) {
            if (viewMode(window) == ViewMode::Normal) widget->showNormal();
            else widget->showFullScreen();
        } else widget->show();
        widget->raise(); widget->activateWindow(); d->activeWindow = window;
    }
}
bool WindowManager::activateTab(TabId tab)
{
    if (!d->model.tab(tab)) return false;
    const QPointer<WindowManager> self(this);
    leaveFocusScopeFor(tab, {});
    if (!self) return false;
    const auto result = d->model.activateTab(tab); if (!result) return false;
    const auto group = d->model.groupForTab(tab); const auto window = d->model.group(group)->windowId;
    if (auto widget = d->windows.value(window)) { widget->surface()->selectTab(tab); widget->updateCaption(); }
    return focusPane(tab, d->model.tab(tab)->activePane);
}
bool WindowManager::focusPane(TabId tab, PaneId pane)
{
    if (!d->model.panes(tab).contains(pane)) return false;
    const QPointer<WindowManager> self(this);
    leaveFocusScopeFor(tab, pane);
    if (!self) return false;
    if (!d->model.activatePane(tab, pane)) return false;
    if (!d->directionalNavigation) d->navigationAnchor.reset();
    if (auto widget = d->panes.value(pane)) {
        if (auto view = d->tabs.value(tab)) for (auto* frame : view->paneFrames()) {
            frame->setProperty("active", frame->property("paneId").toUuid() == pane);
            frame->style()->unpolish(frame); frame->style()->polish(frame); frame->update();
        }
        if (auto view = d->tabs.value(tab)) view->setActivePane(pane);
        widget->setFocus(Qt::OtherFocusReason); return true;
    }
    return false;
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
    d->behavior = behavior; d->behavior.revealDelayMs = qBound(200, behavior.revealDelayMs, 2000);
    emit layoutChanged();
}
void WindowManager::setTheme(const Theme& theme)
{
    d->theme = theme;
    for (auto widget : d->windows) if (widget) applyTheme(widget, theme);
}
Theme WindowManager::theme() const { return d->theme; }
CommandRegistry& WindowManager::commands() { return d->commands; }
bool WindowManager::execute(const QString& command, const QVariantMap& args, WindowId window)
{
    const QPointer<WindowManager> self(this);
    auto ctx = context(window); ctx.args = args;
    bool executed = false;
    if (const auto* registered = d->commands.find(command)) {
        const Command retained = *registered;
        const bool enabled = !retained.enabled || retained.enabled(ctx);
        if (!self) return false;
        if (enabled) executed = retained.execute(ctx);
    }
    if (!self) return false;
    if (executed) return true;
    emit commandFailed(QStringLiteral("命令不可执行：%1").arg(command)); return false;
}
void WindowManager::showCommandPalette(WindowId window) { const auto ctx = context(window); openCommandPalette(this, ctx, windowWidget(ctx.window)); }
void WindowManager::showWindowSelector(WindowId window) { const auto ctx = context(window); openWindowSelector(this, ctx.window, windowWidget(ctx.window)); }
void WindowManager::showSettings(WindowId window) { const auto ctx = context(window); openBehaviorSettings(this, windowWidget(ctx.window)); }
bool WindowManager::resetToolbarWidth(WindowId window, const QString& stripId)
{
    const auto view = d->windows.value(window);
    return view && view->strips() && view->strips()->resetStripWidth(stripId);
}
void WindowManager::setStatus(WindowId window, const QString& text) { if (auto widget = d->windows.value(window)) widget->setStatus(text); }
bool WindowManager::applyResult(const OperationResult& result)
{
    if (!result) { emit commandFailed(result.error); return false; }
    refresh(); return true;
}
void WindowManager::refresh(bool preserveDrag)
{
    d->navigationAnchor.reset();
    if (d->refreshing || d->destroying) return;
    d->refreshing = true;
    if (!preserveDrag) d->drag->releaseForMutation();
    for (auto window : d->windows) if (window) window->surface()->captureStripScrollOffsets();
    for (auto window : d->windows) if (window) window->surface()->closePeek();
    for (auto pane : d->panes) if (pane) { pane->setParent(d->storage); pane->hide(); }
    for (const auto& bars : {d->paneTopBars, d->paneBottomBars})
        for (auto bar : bars) if (bar) { bar->setParent(d->storage); bar->hide(); }
    for (auto tab : d->tabs) if (tab) { tab->setParent(d->storage); tab->hide(); }
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
        if (!d->model.tab(it.key())) { if (it.value()) it.value()->deleteLater(); d->renderedTrees.remove(it.key()); it = d->tabs.erase(it); }
        else ++it;
    }
    // A focused pane can retain its ID while moving into a different tab in
    // this window. Activate that target before rebuilding its containing stack.
    for (const auto id : d->model.windowOrder()) reconcileWindowViewState(id);
    // Reattach all existing pane widgets before rebuilding hosts, preserving UI
    // state and avoiding destruction through their previous QWidget parent.
    for (auto it = d->model.tabs().cbegin(); it != d->model.tabs().cend(); ++it) {
        auto& view = d->tabs[it.key()];
        if (!view) view = new TabContent(this, it.key(), d->storage);
        const auto options = d->descriptors.value(it.value().activePane);
        view->rebuild(it.value(), d->panes, d->paneTopBars, d->paneBottomBars); d->renderedTrees.insert(it.key(), it.value().panes);
        view->setBarSharing(options.topBarSharing, options.bottomBarSharing);
    }
    for (auto it = d->windows.begin(); it != d->windows.end();) {
        if (!d->model.window(it.key())) {
            if (it.value()) { it.value()->allowClose = true; it.value()->hide(); it.value()->deleteLater(); }
            d->options.remove(it.key()); d->viewStates.remove(it.key()); it = d->windows.erase(it);
        } else ++it;
    }
    for (const auto id : d->model.windowOrder()) {
        auto& window = d->windows[id];
        if (!window) window = new WorkspaceWindow(this, id, d->options.value(id));
        window->setOptions(d->options.value(id));
        window->syncKind(); window->surface()->rebuild(d->tabs);
        reconcileWindowViewState(id); window->applyViewState(windowViewState(id));
        window->updateCaption();
    }
    d->refreshing = false;
    QPointer<WindowManager> self(this);
    const auto active = context();
    if (auto window = d->windows.value(active.window); window && window->isVisible() && window->isActiveWindow())
        if (!active.pane.isNull()) focusPane(active.tab, active.pane);
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
    d->activeWindow = window; d->model.activateTab(tab);
    if (auto widget = d->windows.value(window)) widget->updateCaption();
    // The drag transaction publishes after its focus/placement restoration.
    // A host may destroy the manager in that notification, which must happen
    // after Qt has returned from setFocus on the content being restored.
    if (d->drag->hasStableLayout()) return;
    emit layoutChanged();
}
void WindowManager::beginTabDrag(TabId tab, QPoint global) { d->pendingKeys = {}; d->drag->beginTab(tab, global); }
void WindowManager::beginWindowDrag(WindowId window, QPoint global) { d->pendingKeys = {}; d->drag->beginWindow(window, global); }

bool WindowManager::focusGroup(GroupId group)
{
    const auto* record = d->model.group(group); if (!record) return false;
    return activateTab(record->activeTab);
}
void WindowManager::resizeActive(Placement direction, bool panes)
{
    const auto ctx = context();
    const auto* window = d->model.window(ctx.window); const auto* tab = d->model.tab(ctx.tab);
    const auto target = panes ? ctx.pane : ctx.group;
    const auto root = panes ? (tab ? tab->panes : LayoutTree{}) : (window ? window->central : LayoutTree{});
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
bool WindowManager::eventFilter(QObject* watched, QEvent* event)
{
    if (d->destroying || d->refreshing) return false;
    auto* widget = qobject_cast<QWidget*>(watched);
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
                for (const auto& tab : d->model.tabs()) if (d->model.panes(tab.id).contains(pane)) {
                    const auto tabId = tab.id;
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
                    break;
                }
                break;
            }
        }
        d->pendingKeys = {};
    }
    if (event->type() == QEvent::ContextMenu && widget && !d->drag->isActive()) {
        for (auto* current = widget; current; current = current->parentWidget()) {
            const auto pane = current->property("paneId").toUuid();
            if (pane.isNull()) continue;
            for (const auto& tab : d->model.tabs()) if (d->model.panes(tab.id).contains(pane)) {
                showPaneMenu(tab.id, pane, static_cast<QContextMenuEvent*>(event)->globalPos(), widget);
                return true;
            }
            break;
        }
    }
    if (event->type() != QEvent::KeyPress || d->drag->isActive() || !widget) return false;
    for (auto* current = widget; current; current = current->parentWidget()) if (qobject_cast<QKeySequenceEdit*>(current)) return false;
    const auto windowId = widget->window()->property("windowId").toUuid();
    if (windowId.isNull()) return false; // Dialogs retain their native input handling.
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Control || key->key() == Qt::Key_Shift || key->key() == Qt::Key_Alt || key->key() == Qt::Key_Meta) return false;
    if (!d->pendingKeys.isEmpty() && key->key() == Qt::Key_Escape) { d->pendingKeys = {}; return true; }
    QVector<QKeyCombination> combinations;
    for (int i = 0; i < d->pendingKeys.count(); ++i) combinations << d->pendingKeys[i];
    if (combinations.size() == 4) { d->pendingKeys = {}; return true; }
    combinations << key->keyCombination();
    const auto emptyKey = QKeyCombination::fromCombined(0);
    const QKeySequence sequence(combinations.value(0, emptyKey), combinations.value(1, emptyKey),
                                combinations.value(2, emptyKey), combinations.value(3, emptyKey));
    const auto ctx = context(windowId);
    for (const auto& command : d->commands.commands()) {
        if (command.shortcut.isEmpty() || (command.enabled && !command.enabled(ctx))) continue;
        const auto match = sequence.matches(command.shortcut);
        if (match == QKeySequence::ExactMatch) { d->pendingKeys = {}; d->keyTimer->stop(); d->commands.execute(command.id, ctx); return true; }
        if (match == QKeySequence::PartialMatch) { d->pendingKeys = sequence; d->keyWindow = windowId; d->keyTimer->start(1500); setStatus(windowId, QStringLiteral("等待续键：%1").arg(sequence.toString())); return true; }
    }
    if (!d->pendingKeys.isEmpty()) { d->pendingKeys = {}; return true; }
    return false;
}

} // namespace window_system
