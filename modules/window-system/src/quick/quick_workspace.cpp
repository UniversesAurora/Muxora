// SPDX-License-Identifier: MIT
#include <window_system/quick_workspace.hpp>
#include <window_system/window_manager.hpp>
#include "content_host.hpp"
#include "dock_surface_item.hpp"

#include <QPointer>
#include <QSet>
#include <QQuickItem>
#include <QQuickWindow>
#include <utility>

namespace window_system {
namespace {

// A workspace window: closing asks the manager, which may refuse.
class WorkspaceQuickWindow final : public QQuickWindow {
public:
    WorkspaceQuickWindow(WindowManager& manager, WindowId id) : manager_(manager), id_(id) {}
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
        if (event->type() == QEvent::Resize && dock) dock->setSize(size());
        return QQuickWindow::event(event);
    }

private:
    WindowManager& manager_;
    WindowId id_;
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
    const auto theme = d->manager.interfaceTheme();
    for (const auto id : model.windowOrder()) {
        auto& window = d->windows[id];
        if (!window) {
            window = new WorkspaceQuickWindow(d->manager, id);
            window->setColor(theme.headerBackground);
            window->resize(1280, 800);
            window->dock = new quick::DockSurfaceItem(d->manager, d->host, id, window->contentItem());
            window->dock->setSize(window->size());
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
