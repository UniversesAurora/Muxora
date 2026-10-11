#pragma once
#include <window_system/window_manager.hpp>
#include "chrome/workspace_window.hpp"
#include "chrome/command_strip.hpp"
#include "docking/dock_surface.hpp"
#include "docking/drag_controller.hpp"
#include "panes/tab_content.hpp"
#include <QPointer>
#include <QTimer>
#include <QSet>

namespace window_system {
struct WindowManager::Private {
    LayoutModel model;
    QHash<WindowId, QPointer<WorkspaceWindow>> windows;
    QHash<WindowId, WindowOptions> options;
    QHash<TabId, QPointer<TabContent>> tabs;
    QHash<TabId, LayoutTree> renderedTrees;
    QHash<PaneId, QPointer<QWidget>> panes;
    QHash<PaneId, TabOptions> descriptors;
    QWidget* storage = nullptr;
    DragController* drag = nullptr;
    CommandRegistry commands;
    ContentFactory factory;
    CloseGuard closeGuard;
    WindowBehavior behavior;
    Theme theme = defaultTheme();
    WindowId activeWindow;
    bool refreshing = false;
    bool destroying = false;
    QSet<TabId> guardedTabs;
    QSet<WindowId> closeRequests;
    QKeySequence pendingKeys;
    WindowId keyWindow;
    QTimer* keyTimer = nullptr;
};

} // namespace window_system
