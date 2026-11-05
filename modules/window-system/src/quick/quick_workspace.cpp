// SPDX-License-Identifier: MIT
#include <window_system/quick_workspace.hpp>
#include <window_system/window_manager.hpp>
#include "button_item.hpp"
#include "content_host.hpp"
#include "content_bars.hpp"
#include "dock_surface_item.hpp"
#include "tab_drag.hpp"
#include "window_chrome_item.hpp"
#ifdef Q_OS_WIN
#include "platform/windows/window_frame.hpp"
#endif

#include <QCoreApplication>
#include <QGuiApplication>
#include <QPointer>
#include <QSet>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <algorithm>
#include <utility>

namespace window_system {
namespace {

// A workspace window: the header row above the dock, in a native frame
// without the system caption. Closing asks the manager, which may refuse.
class WorkspaceQuickWindow final : public QQuickWindow {
public:
    WorkspaceQuickWindow(WindowManager& manager, quick::ContentHost& host, quick::ContentBars& bars, quick::TabDrag& drag, WindowId id)
        : manager_(manager), id_(id)
    {
        const auto options = manager.windowOptions(id);
        const auto theme = manager.interfaceTheme();
        setFlags(Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint);
        // Joined tab shapes and shoulders meet edges in cusps; they are drawn
        // aliased and rely on multisampling, which is fixed at creation.
        auto surfaceFormat = format();
        surfaceFormat.setSamples(4);
        setFormat(surfaceFormat);
        // The manager's shortcut dispatch finds the window by this.
        setProperty("windowId", id);
        setColor(theme.headerBackground);
        resize(options.size);
        chrome_ = new quick::WindowChromeItem(theme, contentItem());
        updateCaption();
        chrome_->setButtonOrder(manager.behavior().captionButtonOrder);
        chrome_->minimizeRequested = [this] { showMinimized(); };
        chrome_->maximizeRequested = [this] { windowStates() & (Qt::WindowMaximized | Qt::WindowFullScreen) ? showNormal() : showMaximized(); };
        chrome_->closeRequested = [this] { close(); };
        chrome_->carriesTabs = [this] {
            const auto* record = manager_.model().window(id_);
            return record && record->kind == WindowKind::Floating;
        };
        chrome_->carryRequested = [this, &drag](QPoint press) { drag.beginWindow(id_, press); };
        dock = new quick::DockSurfaceItem(manager, host, bars, id, contentItem());
        dock->setTabDrag(&drag);
        // As focus in a Widgets window does: the window the user turns to
        // becomes the manager's current one.
        connect(this, &QWindow::activeChanged, this, [this] {
            if (!isActive() || manager_.context().window == id_) return;
            if (const auto tab = manager_.context(id_).tab; !tab.isNull()) manager_.activateTab(tab);
        });
        connect(this, &QWindow::windowStateChanged, this, [this](Qt::WindowState state) {
            chrome_->setExpanded(state == Qt::WindowMaximized || state == Qt::WindowFullScreen);
            // As the Widgets window: leaving full screen some other way ends
            // the view mode.
            if (applyingViewState_ || state == Qt::WindowFullScreen || state == Qt::WindowMinimized) return;
            QTimer::singleShot(0, this, [this] {
                if (manager_.viewMode(id_) != ViewMode::Normal && !(windowStates() & (Qt::WindowFullScreen | Qt::WindowMinimized)))
                    manager_.setViewMode(id_, ViewMode::Normal);
            });
        });
#ifdef Q_OS_WIN
        NativeFrameClient client;
        client.minimumSize = [this] { return dock->minimumSize() + QSize(0, quick::WindowChromeItem::rowHeight()); };
        client.captionButtonAt = [this](QPointF position) -> QObject* { return chrome_->captionButtonAt(position); };
        client.setCaptionHovered = [](QObject* button, bool hovered, QPointF) { static_cast<quick::ButtonItem*>(button)->setHovered(hovered); };
        client.setCaptionPressed = [](QObject* button, bool pressed) { static_cast<quick::ButtonItem*>(button)->setPressed(pressed); };
        client.activateCaption = [](QObject* button) { static_cast<quick::ButtonItem*>(button)->click(); };
        setNativeFrameClient(this, std::move(client));
        applyNativeFrame(this);
#endif
        layoutChildren();
    }
    quick::DockSurfaceItem* dock = nullptr;

    // As the Widgets window applies its view state: any view mode is full
    // screen, and the header row, which holds the toolbars there, follows
    // the toolbars' visibility.
    // The rectangle to save: the window's own while neither maximized nor
    // full screen, else the last one it had then.
    QRect normalGeometry() const
    {
        return windowStates() & (Qt::WindowMaximized | Qt::WindowFullScreen | Qt::WindowMinimized) ? normalGeometry_ : geometry();
    }
    bool normallyMaximized() const
    {
        return windowStates() & Qt::WindowFullScreen ? bool(normalStates_ & Qt::WindowMaximized)
            : windowStates() & Qt::WindowMinimized ? maximizedBeforeMinimized_ : bool(windowStates() & Qt::WindowMaximized);
    }
    // Shows the window at a restored place.
    void place(QRect normal, bool maximized)
    {
        setGeometry(normal);
        normalGeometry_ = normal;
        if (maximized) showMaximized(); else showNormal();
    }

    // As the Widgets window: the active tab before the window's title, and
    // a floating window says so, also in its header.
    void updateCaption()
    {
        const auto* record = manager_.model().window(id_);
        const auto title = manager_.windowCaption(id_);
        if (this->title() != title) setTitle(title);
        chrome_->setTitle(record && record->kind == WindowKind::Floating ? title : manager_.windowOptions(id_).title);
    }

    void applyViewState()
    {
        const auto state = manager_.windowViewState(id_);
        if (chrome_->isVisible() != state.effective.toolbars) {
            chrome_->setVisible(state.effective.toolbars);
            layoutChildren();
        }
        const bool fullscreen = state.mode != ViewMode::Normal;
        if (fullscreen == bool(windowStates() & Qt::WindowFullScreen)) return;
        applyingViewState_ = true;
        if (fullscreen) {
            normalStates_ = windowStates() & Qt::WindowMaximized;
            showFullScreen();
        } else {
            setWindowStates(normalStates_);
        }
        applyingViewState_ = false;
    }

protected:
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::Close) {
            // If the manager agrees, the next sync deletes this window.
            event->ignore();
            manager_.requestCloseWindow(id_);
            return true;
        }
        if (event->type() == QEvent::Resize) layoutChildren();
        if ((event->type() == QEvent::Resize || event->type() == QEvent::Move)
            && !(windowStates() & (Qt::WindowMaximized | Qt::WindowFullScreen | Qt::WindowMinimized)) && isVisible())
            normalGeometry_ = geometry();
        if (event->type() == QEvent::WindowStateChange && (windowStates() & Qt::WindowMinimized))
            maximizedBeforeMinimized_ = static_cast<QWindowStateChangeEvent*>(event)->oldState() & Qt::WindowMaximized;
        return QQuickWindow::event(event);
    }
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray& type, void* message, qintptr* result) override
    {
        if (handleNativeFrame(this, message, result)) return true;
        return QQuickWindow::nativeEvent(type, message, result);
    }
#endif

private:
    void layoutChildren()
    {
        const int header = chrome_->isVisible() ? quick::WindowChromeItem::rowHeight() : 0;
        chrome_->setSize(QSizeF(width(), header));
        dock->setPosition(QPointF(0, header));
        dock->setSize(QSizeF(width(), std::max(0, height() - header)));
    }
    WindowManager& manager_;
    WindowId id_;
    quick::WindowChromeItem* chrome_;
    Qt::WindowStates normalStates_;
    bool applyingViewState_ = false;
    QRect normalGeometry_;
    bool maximizedBeforeMinimized_ = false;
};

} // namespace

struct QuickWorkspace::Private final : WindowPresenter {
    Private(QuickWorkspace& workspace, WindowManager& owner)
        : manager(owner), drag(owner, [this](WindowId id) -> quick::DockSurfaceItem* {
              const auto window = windows.value(id);
              return window ? window->dock : nullptr;
          }, [this, &workspace](WindowId id) -> QQuickWindow* {
              workspace.sync();
              return windows.value(id);
          }) {}
    WindowManager& manager;
    quick::ContentHost host;
    quick::ContentBars bars;
    QHash<WindowId, QPointer<WorkspaceQuickWindow>> windows;
    // Includes windows awaiting deleteLater after removal from the model.
    QSet<WorkspaceQuickWindow*> ownedWindows;
    quick::TabDrag drag;
    QSet<PaneId> panes;
    bool syncPending = false;
    bool projecting = false;
    bool syncAfterProjection = false;
    PaneId focusedPane;
    QPointer<QQuickWindow> focusedWindow;

    // As the Widgets manager shows its windows; a minimized window comes
    // back in the state it had, maximized or full screen.
    void showWindow(WindowId id) override
    {
        auto* window = windows.value(id).data();
        if (!window) return;
        if (window->windowStates() & Qt::WindowMinimized) window->setWindowStates(window->windowStates() & ~Qt::WindowMinimized);
        window->show();
        window->raise();
        window->requestActivate();
    }
    bool isWindowMinimized(WindowId id) const override
    {
        const auto* window = windows.value(id).data();
        return !window || !window->isVisible() || (window->windowStates() & Qt::WindowMinimized);
    }
    QRect normalGeometry(WindowId id) const override
    {
        const auto* window = windows.value(id).data();
        return window ? window->normalGeometry() : QRect{};
    }
    bool isWindowMaximized(WindowId id) const override
    {
        const auto* window = windows.value(id).data();
        return window && window->normallyMaximized();
    }
    void restorePlacement(WindowId id, QRect normal, bool maximized) override
    {
        if (auto* window = windows.value(id).data()) window->place(normal, maximized);
        else placements.insert(id, {normal, maximized});
    }
    // Restored places of windows not built yet.
    QHash<WindowId, std::pair<QRect, bool>> placements;
};

QuickWorkspace::QuickWorkspace(WindowManager& manager, QObject* parent)
    : QObject(parent), d(std::make_unique<Private>(*this, manager))
{
    Q_ASSERT(manager.presentation() == WindowManager::Presentation::External);
    connect(&d->host, &quick::ContentHost::paneFocused, this, [this](PaneId pane, QWindow* window) {
        // Reparenting a page can temporarily focus its old sibling. The
        // model's active pane remains authoritative during projection.
        if (!d->projecting) d->manager.contentFocused(pane, window);
    });
    const auto visibility = [this](bool enabled) {
        if (!enabled) { d->host.setVisibilityHandler({}); return; }
        d->host.setVisibilityHandler([this](const QVector<ContentVisibilityChange>& changes) {
            d->manager.contentVisibilityChanged(changes);
        });
    };
    connect(&manager, &WindowManager::contentVisibilityRequested, this, visibility);
    manager.setPresenter(d.get());
    connect(&manager, &WindowManager::contextMenuRequested, this,
        [this](CommandMenuSurface surface, const CommandContext& context, QPoint global, QObject* source) {
            if (const auto window = d->windows.value(context.window); window && d->manager.validCommandContext(context))
                window->dock->showContextMenu(surface, context, global, nullptr, qobject_cast<QQuickItem*>(source));
        });
    // Model changes often come from a strip's own input handler; rebuilding
    // there would replace the strip under it. One sync per event-loop turn.
    connect(&manager, &WindowManager::layoutChanged, this, &QuickWorkspace::requestSync);
    connect(&d->bars, &quick::ContentBars::changed, this, &QuickWorkspace::requestSync);
    connect(&manager, &WindowManager::hierarchyBarsRequested, this, [this](const QVector<QPair<TabId, PaneId>>& sources) {
        const QPointer<QuickWorkspace> self(this);
        const bool outerProjection = std::exchange(d->projecting, true);
        d->bars.transferToPanes(sources);
        if (self && !outerProjection) finishProjection();
    });
    sync();
}

QuickWorkspace::~QuickWorkspace()
{
    d->host.setVisibilityHandler({});
    d->manager.setPresenter(nullptr);
    d->drag.cancel();
    const auto windows = std::exchange(d->ownedWindows, {});
    for (auto* window : windows) delete window;
}

void QuickWorkspace::setContentFactory(ContentFactory factory)
{
    d->host.setFactory(std::move(factory));
}

bool QuickWorkspace::setTabBar(TabId tab, QQuickItem* bar, bool top)
{
    if (!d->manager.model().tab(tab)) return false;
    return d->bars.setTab(tab, bar, top);
}

bool QuickWorkspace::setPaneBar(PaneId pane, QQuickItem* bar, bool top)
{
    if (d->manager.model().tabForPane(pane).isNull()) return false;
    return d->bars.setPane(pane, bar, top);
}

QQuickWindow* QuickWorkspace::window(WindowId window) const { return d->windows.value(window); }
QQuickItem* QuickWorkspace::content(PaneId pane) const { return d->host.item(pane); }

void QuickWorkspace::requestSync()
{
    if (d->projecting) { d->syncAfterProjection = true; return; }
    if (std::exchange(d->syncPending, true)) return;
    QMetaObject::invokeMethod(this, &QuickWorkspace::sync, Qt::QueuedConnection);
}

void QuickWorkspace::finishProjection()
{
    d->projecting = false;
    if (std::exchange(d->syncAfterProjection, false)) requestSync();
}

void QuickWorkspace::sync()
{
    d->syncPending = false;
    if (d->projecting) { d->syncAfterProjection = true; return; }
    const QPointer<QuickWorkspace> self(this);
    d->projecting = true;
    const auto previous = d->host.item(d->manager.context().pane);
    const bool hadContentFocus = previous && previous->window()
        && !d->host.paneForItem(previous->window()->activeFocusItem()).isNull();
    const QPointer<QQuickItem> transferredFocus(d->bars.takeTransferredFocus());
    const QPointer<QQuickItem> previousBarFocus(transferredFocus ? transferredFocus.data()
        : qobject_cast<QQuickItem*>(QGuiApplication::focusObject()));
    const auto previousBarPane = d->bars.paneForItem(d->manager.model(), previousBarFocus);
    const auto& model = d->manager.model();
    d->bars.prune(model);
    if (!self) return;
    // Content first, so groups mount it when they build their pages.
    QSet<PaneId> live;
    for (auto it = model.tabs().cbegin(); it != model.tabs().cend(); ++it)
        for (const auto pane : model.panes(it.key())) {
            live.insert(pane);
            if (!d->panes.contains(pane)) {
                const bool created = d->host.create(pane, d->manager.paneOptions(pane));
                if (!self) return;
                if (created) d->panes.insert(pane);
            }
        }
    // Park cross-window content before either surface detaches its old page.
    // Otherwise mount() sees an already windowless item and misses the hidden
    // notification that must precede transferring the native input context.
    for (const auto pane : std::as_const(d->panes)) {
        const auto content = d->host.item(pane);
        const auto* group = model.group(model.groupForTab(model.tabForPane(pane)));
        if (content && content->window() && group && content->window() != d->windows.value(group->windowId)) {
            d->host.unmount(pane);
            if (!self) return; // A host may release the workspace in this notification.
        }
    }
    for (auto it = d->windows.begin(); it != d->windows.end();) {
        if (model.window(it.key())) { ++it; continue; }
        // A drag can empty a window while that window delivers its event.
        if (it.value()) { it.value()->hide(); it.value()->deleteLater(); }
        it = d->windows.erase(it);
    }
    for (const auto id : model.windowOrder()) {
        auto& window = d->windows[id];
        if (!window) {
            window = new WorkspaceQuickWindow(d->manager, d->host, d->bars, d->drag, id);
            connect(window->dock, &quick::DockSurfaceItem::paneMenuRequested, this,
                [this](TabId tab, PaneId pane, QPoint global, QQuickItem* source) { d->manager.showPaneMenu(tab, pane, global, source); });
            d->ownedWindows.insert(window);
            connect(window, &QObject::destroyed, this, [this, retired = window.data()] { d->ownedWindows.remove(retired); });
            connect(window, &QQuickWindow::activeFocusItemChanged, this, [this, observed = QPointer<QQuickWindow>(window)] {
                if (d->projecting || !observed) return;
                const auto pane = d->bars.paneForItem(d->manager.model(), observed->activeFocusItem());
                if (!pane.isNull()) d->manager.contentFocused(pane, observed);
            });
            if (const auto placement = d->placements.take(id); placement.first.isValid())
                window->place(placement.first, placement.second);
            else window->show();
        }
        window->dock->sync();
        window->updateCaption();
        window->applyViewState();
    }
    for (auto it = d->panes.begin(); it != d->panes.end();) {
        if (live.contains(*it)) { ++it; continue; }
        d->host.remove(*it);
        it = d->panes.erase(it);
    }
    finishProjection();
    const auto pane = d->manager.context().pane;
    const auto content = d->host.item(pane);
    if (!content || !content->window() || !content->isVisible()) return;
    const QPointer<QQuickWindow> window(content->window());
    // Commands select a pane; plain layout updates keep a toolbar/menu's
    // focus. A cross-window move restores the pane's surviving editor.
    const bool restoreBar = previousBarFocus && previousBarPane == pane && previousBarFocus->isVisible()
        && previousBarFocus->window() == window;
    if (pane != d->focusedPane || window != d->focusedWindow || hadContentFocus || !previousBarPane.isNull()) {
        d->focusedPane = pane;
        d->focusedWindow = window;
        if (restoreBar) previousBarFocus->forceActiveFocus(); else d->host.focus(pane);
        if (!self || !window) return;
    }
    auto focused = d->host.paneForItem(window->activeFocusItem());
    if (focused.isNull()) focused = d->bars.paneForItem(model, window->activeFocusItem());
    if (focused == pane) d->manager.contentFocused(focused, window, false);
}

} // namespace window_system
