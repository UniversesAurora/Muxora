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
    QHash<WindowId, WindowViewState> viewStates;
    // Updated only by committed chrome edits. Saving an older window must not
    // replace the most recently edited default for its window kind.
    QHash<WindowKind, QJsonObject> toolbarProfiles;
    QSet<WindowId> projectingViews;
    QHash<TabId, QPointer<TabContent>> tabs;
    QHash<TabId, LayoutTree> renderedTrees;
    QHash<PaneId, QPointer<QWidget>> panes;
    QHash<PaneId, QPointer<QWidget>> paneTopBars;
    QHash<PaneId, QPointer<QWidget>> paneBottomBars;
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
    int geometryUpdateDepth = 0;
    QSet<TabId> guardedTabs;
    QSet<WindowId> closeRequests;
    bool closeAllPending = false;
    QKeySequence pendingKeys;
    WindowId keyWindow;
    QTimer* keyTimer = nullptr;
    struct NavigationAnchor {
        WindowId window;
        TabId tab;
        bool panes = false;
        Qt::Orientation axis = Qt::Horizontal;
        int coordinate = 0;
    };
    std::optional<NavigationAnchor> navigationAnchor;
    bool directionalNavigation = false;
};

} // namespace window_system
