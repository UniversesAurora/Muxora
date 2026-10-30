// SPDX-License-Identifier: MIT
#include <window_system/quick_workspace.hpp>
#include <window_system/window_manager.hpp>
#include "button_item.hpp"
#include "content_host.hpp"
#include "dock_surface_item.hpp"
#include "window_chrome_item.hpp"
#ifdef Q_OS_WIN
#include "platform/windows/window_frame.hpp"
#endif

#include <QPointer>
#include <QSet>
#include <QQuickItem>
#include <QQuickWindow>
#include <algorithm>
#include <utility>

namespace window_system {
namespace {

// A workspace window: the header row above the dock, in a native frame
// without the system caption. Closing asks the manager, which may refuse.
class WorkspaceQuickWindow final : public QQuickWindow {
public:
    WorkspaceQuickWindow(WindowManager& manager, quick::ContentHost& host, WindowId id) : manager_(manager), id_(id)
    {
        const auto options = manager.windowOptions(id);
        const auto theme = manager.interfaceTheme();
        setFlags(Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint);
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
        dock = new quick::DockSurfaceItem(manager, host, id, contentItem());
        connect(this, &QWindow::windowStateChanged, this, [this](Qt::WindowState state) {
            chrome_->setExpanded(state == Qt::WindowMaximized || state == Qt::WindowFullScreen);
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
        const int header = quick::WindowChromeItem::rowHeight();
        chrome_->setSize(QSizeF(width(), header));
        dock->setPosition(QPointF(0, header));
        dock->setSize(QSizeF(width(), std::max(0, height() - header)));
    }
    WindowManager& manager_;
    WindowId id_;
    quick::WindowChromeItem* chrome_;
};

} // namespace

struct QuickWorkspace::Private {
    explicit Private(WindowManager& owner) : manager(owner) {}
    WindowManager& manager;
    quick::ContentHost host;
    QHash<WindowId, QPointer<WorkspaceQuickWindow>> windows;
    QSet<PaneId> panes;
    bool syncPending = false;
};

QuickWorkspace::QuickWorkspace(WindowManager& manager, QObject* parent)
    : QObject(parent), d(std::make_unique<Private>(manager))
{
    Q_ASSERT(manager.presentation() == WindowManager::Presentation::External);
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
        delete it.value().data();
        it = d->windows.erase(it);
    }
    for (const auto id : model.windowOrder()) {
        auto& window = d->windows[id];
        if (!window) {
            window = new WorkspaceQuickWindow(d->manager, d->host, id);
            window->show();
        }
        window->dock->sync();
    }
    for (auto it = d->panes.begin(); it != d->panes.end();) {
        if (live.contains(*it)) { ++it; continue; }
        d->host.remove(*it);
        it = d->panes.erase(it);
    }
}

} // namespace window_system
