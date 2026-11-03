// SPDX-License-Identifier: MIT
#include <window_system/quick_workspace.hpp>
#include <window_system/window_manager.hpp>
#include "button_item.hpp"
#include "content_host.hpp"
#include "dock_surface_item.hpp"
#include "tab_drag.hpp"
#include "window_chrome_item.hpp"
#ifdef Q_OS_WIN
#include "platform/windows/window_frame.hpp"
#endif

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
    WorkspaceQuickWindow(WindowManager& manager, quick::ContentHost& host, quick::TabDrag& drag, WindowId id)
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
        setTitle(options.title);
        resize(options.size);
        chrome_ = new quick::WindowChromeItem(theme, contentItem());
        chrome_->setTitle(options.title);
        chrome_->setButtonOrder(manager.behavior().captionButtonOrder);
        chrome_->minimizeRequested = [this] { showMinimized(); };
        chrome_->maximizeRequested = [this] { windowStates() & (Qt::WindowMaximized | Qt::WindowFullScreen) ? showNormal() : showMaximized(); };
        chrome_->closeRequested = [this] { close(); };
        chrome_->carriesTabs = [this] {
            const auto* record = manager_.model().window(id_);
            return record && record->kind == WindowKind::Floating;
        };
        chrome_->carryRequested = [this, &drag](QPoint press) { drag.beginWindow(id_, press); };
        dock = new quick::DockSurfaceItem(manager, host, id, contentItem());
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
    QHash<WindowId, QPointer<WorkspaceQuickWindow>> windows;
    quick::TabDrag drag;
    QSet<PaneId> panes;
    bool syncPending = false;

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
};

QuickWorkspace::QuickWorkspace(WindowManager& manager, QObject* parent)
    : QObject(parent), d(std::make_unique<Private>(*this, manager))
{
    Q_ASSERT(manager.presentation() == WindowManager::Presentation::External);
    manager.setPresenter(d.get());
    // Model changes often come from a strip's own input handler; rebuilding
    // there would replace the strip under it. One sync per event-loop turn.
    connect(&manager, &WindowManager::layoutChanged, this, [this] {
        if (std::exchange(d->syncPending, true)) return;
        QMetaObject::invokeMethod(this, &QuickWorkspace::sync, Qt::QueuedConnection);
    });
    sync();
}

QuickWorkspace::~QuickWorkspace()
{
    d->manager.setPresenter(nullptr);
    d->drag.cancel();
    for (const auto& window : std::as_const(d->windows)) delete window.data();
}

void QuickWorkspace::setContentFactory(ContentFactory factory)
{
    d->host.setFactory(std::move(factory));
}

QQuickWindow* QuickWorkspace::window(WindowId window) const { return d->windows.value(window); }
QQuickItem* QuickWorkspace::content(PaneId pane) const { return d->host.item(pane); }

void QuickWorkspace::sync()
{
    d->syncPending = false;
    const auto& model = d->manager.model();
    // Content first, so groups mount it when they build their pages.
    QSet<PaneId> live;
    for (auto it = model.tabs().cbegin(); it != model.tabs().cend(); ++it)
        for (const auto pane : model.panes(it.key())) {
            live.insert(pane);
            if (!d->panes.contains(pane) && d->host.create(pane, d->manager.paneOptions(pane))) d->panes.insert(pane);
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
            window = new WorkspaceQuickWindow(d->manager, d->host, d->drag, id);
            window->show();
        }
        window->dock->sync();
        window->applyViewState();
    }
    for (auto it = d->panes.begin(); it != d->panes.end();) {
        if (live.contains(*it)) { ++it; continue; }
        d->host.remove(*it);
        it = d->panes.erase(it);
    }
}

} // namespace window_system
