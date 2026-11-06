#pragma once
#include <window_system/window_manager.hpp>
#include "chrome/workspace_window.hpp"
#include "chrome/command_strip.hpp"
#include "docking/dock_surface.hpp"
#include "docking/drag_controller.hpp"
#include "panes/tab_content.hpp"
#include "input_method_scopes.hpp"
#include <QPointer>
#include <QTimer>
#include <QSet>
#include <QJsonArray>

namespace window_system {
struct WindowManager::Private {
    Presentation presentation = Presentation::Widgets;
    LayoutModel model;
    QHash<WindowId, QPointer<WorkspaceWindow>> windows;
    QHash<WindowId, WindowOptions> options;
    QHash<WindowId, WindowViewState> viewStates;
    // Updated only by committed chrome edits. Saving an older window must not
    // replace the most recently edited default for its window kind.
    QHash<WindowKind, QJsonObject> toolbarProfiles;
    QHash<WindowKind, BarVisibility> visibilityProfiles;
    QHash<WindowId, QHash<WindowKind, BarVisibility>> windowVisibility;
    QHash<WindowId, WindowKind> visibilityKinds;
    QSet<WindowId> projectingViews;
    QHash<TabId, QPointer<TabContent>> tabs;
    QHash<TabId, QPointer<QWidget>> pendingTabTopBars;
    QHash<TabId, QPointer<QWidget>> pendingTabBottomBars;
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
    InputMethodScopes inputMethods;
    QString defaultTabCommand;
    // The tab folder a tab opened by requestNewTab joins, while it runs.
    QUuid newTabFolder;
    QStringList tabCreationCommands;
    Theme theme = defaultTheme();
    Theme authoredTheme = defaultTheme();
    // Incremented by setTheme(); views built for an older theme are rebuilt.
    quint64 themeRevision = 0;
    TabAppearance appearance;
    QVector<CommandToolbar> customToolbars;
    QVector<StatusItem> customStatusItems;
    WindowId activeWindow;
    WindowPresenter* presenter = nullptr;
    // External: each window's saved toolbar layout per window kind, as the
    // presentation committed it, and the presentation's editor of a window.
    QHash<WindowId, QHash<WindowKind, QJsonObject>> toolbarLayouts;
    std::function<ToolbarEditor*(WindowId)> toolbarEditors;
    // The presentation's dialogs (External), or the Widgets ones; the open
    // ones by ID with what makes them close as the layout changes.
    DialogPresenter* dialogPresenter = nullptr;
    std::unique_ptr<DialogPresenter> widgetDialogs;
    QHash<quint64, std::function<bool()>> openDialogs;
    quint64 nextDialog = 0;
    bool watchingDialogs = false;
    // Each window's status message and transient message, for presentations
    // other than Widgets, which keep theirs in the status bar itself.
    QHash<WindowId, QString> statusMessages;
    QHash<WindowId, QString> transientStatus;
    bool refreshing = false;
    bool destroying = false;
    int geometryUpdateDepth = 0;
    QSet<TabId> guardedTabs;
    QSet<WindowId> closeRequests;
    bool closeAllPending = false;
    QKeySequence pendingKeys;
    // Set while a command runs from a shortcut; sticky tab protection tells
    // the close-tab shortcut from a menu or palette this way.
    bool dispatchingShortcut = false;
    WindowId keyWindow;
    QPointer<QObject> keyFocus;
    CommandContext keyContext;
    quint64 bindingsRevision = 0;
    // Keyboard dispatch reads candidates by first stroke instead of copying the
    // whole catalog per key event. Rebuilt lazily after binding/catalog edits.
    struct BindingCandidate {
        QString command;
        QKeySequence sequence;
        QVariantMap args;
        std::function<bool(const CommandContext&)> enabled;
    };
    QHash<int, QVector<BindingCandidate>> bindingIndex;
    std::optional<quint64> bindingIndexRevision;
    QVector<BindingCandidate> shortcutCandidates(QKeyCombination first);
    bool catalogRefreshPending = false;
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
    std::function<void(const QVector<ContentVisibilityChange>&)> contentVisibilityHandler;
    QHash<PaneId, bool> contentVisibility;
    QVector<PaneId> contentVisibilityOrder;
    bool contentVisibilitySnapshotRequested = false;
    bool contentVisibilityNotificationPending = false;
};

} // namespace window_system
