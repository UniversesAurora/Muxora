#pragma once

#include <window_system/command_registry.hpp>
#include <window_system/theme.hpp>
#include <window_system/toolbar_layout.hpp>
#include <window_system/custom_chrome.hpp>
#include <window_system/view_state.hpp>
#include <window_system/floating_panel.hpp>
#include <window_system/tab_appearance.hpp>
#include <QJsonObject>
#include <QObject>
#include <QSize>
#include <memory>

class QWidget;
class QMenu;

namespace window_system {

struct WindowOptions {
    QString title = QStringLiteral("WindowSystem");
    QSize size = QSize(1280, 800);
    bool tabBarsVisible = true; // Central groups only; sidebar controls stay visible.
    bool sideBarsVisible = true; // Auto-hide rails only; pinned groups stay visible.
    bool toolbarsVisible = true;
    bool statusBarVisible = true;
    QStringList hiddenStatusItems;
    // False lets an embedding host opt out of the last edited kind defaults.
    bool inheritVisibilityProfile = true;
};

enum class PaneBarSharing { Shared, PerPane };

struct TabOptions {
    QString title;
    QString contentType;
    QVariantMap state;
    bool showIcon = true;
    PaneBarSharing topBarSharing = PaneBarSharing::Shared;
    PaneBarSharing bottomBarSharing = PaneBarSharing::Shared;
};

struct ContentVisibilityChange {
    PaneId pane;
    bool visible = false;
};

enum class NewTabPosition { Beginning, BeforeActive, AfterActive, End };

// Windows: minimize, maximize, close, aligned to the strip's trailing edge.
// Reversed (close, maximize, minimize) and MacOS (close, minimize, maximize)
// align to the leading edge, so the controls can sit in the top-left corner.
enum class CaptionButtonOrder { Windows, Reversed, MacOS };

// Window includes its floating descendants; FloatingWindow separates each
// top-level window. Input states are process-local and never serialized.
enum class InputMethodScope { Application, Window, FloatingWindow, TabGroup, Tab, Pane };
[[nodiscard]] QString inputMethodScopeKey(InputMethodScope scope);
[[nodiscard]] std::optional<InputMethodScope> inputMethodScopeFromKey(const QString& key);

struct WindowBehavior {
    InputMethodScope inputMethodScope = InputMethodScope::Tab;
    WindowKind dragOutsideKind = WindowKind::Floating;
    bool closeEmptyWorkbench = false;
    bool revealDuringDrag = true;
    int revealDelayMs = 500;
    NewTabPosition newTabPosition = NewTabPosition::AfterActive;
    bool hideSingleTabWorkbench = false;
    bool hideSingleTabFloating = true;
    bool hideToolbarOptionsWhenLocked = false;
    CaptionButtonOrder captionButtonOrder = CaptionButtonOrder::Windows;
    ViewMode zenFullscreenMode = ViewMode::WindowFullscreen;
};

class WorkspaceWindow;
class DragController;
class DockSurface;
class TabContent;

// All UI methods are called on the application's GUI thread. A successful add
// adopts the widget. Failed calls leave caller ownership unchanged. IDs survive
// movement and window-kind changes; view widgets never enter the layout snapshot.
class WindowManager final : public QObject {
    Q_OBJECT
public:
    using ContentFactory = std::function<QWidget*(const TabOptions&, PaneId)>;
    using CloseGuard = std::function<bool(const QVector<TabId>&)>;

    explicit WindowManager(QObject* parent = nullptr);
    ~WindowManager() override;

    WindowId createWindow(WindowKind kind = WindowKind::Workbench,
                          const WindowOptions& options = {}, WindowId owner = {});
    TabId addTab(WindowId window, QWidget* content, const TabOptions& options,
                 GroupId group = {}, int insertionIndex = -1);
    // Creates the sibling group without activating a temporary tab in the
    // original group. Preparation failure returns null without adopting any
    // surviving content or changing the model. Once committed, the stable ID
    // is returned even if a synchronous callback closes it or destroys us.
    TabId addTabToNewGroup(WindowId window, QWidget* content, const TabOptions& options,
                          GroupId reference, Placement placement);
    // Host command IDs define available content types. Creation always uses
    // the same command handlers as menus, keyboard and the command palette.
    bool setTabCreationCommands(QString defaultCommand, QStringList choices);
    bool requestNewTab(WindowId window, GroupId group = {}, QString command = {});
    void setContentFactory(ContentFactory factory);
    void setCloseGuard(CloseGuard guard);
    // The current visibility of every live pane is reported when the handler
    // is installed. Later changes are coalesced once per event-loop turn.
    // Cross-window moves report affected panes as hidden before reparenting.
    void setContentVisibilityHandler(std::function<void(const QVector<ContentVisibilityChange>&)> handler);
    // The manager owns a bar it accepts. Replacing or clearing one hides the
    // previous bar and deletes it from the event loop, so a host may replace a
    // bar from that bar's own signal.
    bool setTabTopBar(TabId tab, QWidget* bar);
    bool setTabBottomBar(TabId tab, QWidget* bar);
    bool setPaneTopBar(PaneId pane, QWidget* bar);
    bool setPaneBottomBar(PaneId pane, QWidget* bar);
    bool setTabTopBarSharing(TabId tab, PaneBarSharing sharing);
    bool setTabBottomBarSharing(TabId tab, PaneBarSharing sharing);
    // Central group headers only. Pinned panels, auto-hide rails and peek
    // headers retain their own controls and drop targets.
    bool setTabBarsVisible(WindowId window, bool visible);
    [[nodiscard]] bool tabBarsVisible(WindowId window) const;
    bool setSideBarsVisible(WindowId window, bool visible);
    [[nodiscard]] bool sideBarsVisible(WindowId window) const;
    bool setToolbarsVisible(WindowId window, bool visible);
    [[nodiscard]] bool toolbarsVisible(WindowId window) const;
    bool setStatusBarVisible(WindowId window, bool visible);
    [[nodiscard]] bool statusBarVisible(WindowId window) const;
    [[nodiscard]] BarVisibility preferredBarVisibility(WindowId window) const;
    [[nodiscard]] BarVisibility barVisibility(WindowId window) const;
    bool setViewMode(WindowId window, ViewMode mode);
    [[nodiscard]] ViewMode viewMode(WindowId window) const;
    bool setZenMode(WindowId window, bool enabled);
    [[nodiscard]] bool zenMode(WindowId window) const;
    [[nodiscard]] WindowViewState windowViewState(WindowId window) const;
    bool setTabIconVisible(TabId tab, bool visible);
    bool setTabColor(TabId tab, QString colorId);
    bool sortTabs(GroupId group, TabSortOrder order);
    // A sticky tab stays at the front of its group's tab bar and survives
    // closing other tabs. Auto-hide rails cannot hold sticky tabs.
    bool setTabSticky(TabId tab, bool sticky);
    [[nodiscard]] bool isTabSticky(TabId tab) const;
    bool swapGroups(GroupId first, GroupId second);
    bool swapGroupDirection(GroupId group, Placement direction);
    bool mergeGroups(GroupId source, GroupId target, int insertionIndex = -1);
    bool mergeGroupDirection(GroupId group, Placement direction);
    bool moveGroup(GroupId group, GroupId target, Placement placement);
    bool moveGroupToAdjacentGroup(GroupId group, Placement direction);
    bool moveGroupToWindow(GroupId group, WindowId target, Placement placement = Placement::Right);
    bool moveGroupToWindowCycle(GroupId group, bool forward);
    bool moveGroupToEdge(GroupId group, WindowId target, Edge edge, bool pinned);
    WindowId detachGroup(GroupId group, WindowKind kind = WindowKind::Floating);
    bool closeGroup(GroupId group);
    // Hierarchy conversions retain Pane IDs, live content and business objects.
    TabId convertGroupToTab(GroupId group, Placement placement = Placement::Right);
    bool moveGroupIntoTab(GroupId group, TabId targetTab, PaneId targetPane = {}, Placement placement = Placement::Right);
    bool moveTabIntoTab(TabId sourceTab, TabId targetTab, PaneId targetPane = {}, Placement placement = Placement::Right);
    GroupId convertTabToGroup(TabId tab, Placement placement = Placement::Right);
    TabId extractPaneToTab(TabId tab, PaneId pane);
    void showGroupMenu(GroupId group, QPoint globalPosition);
    [[nodiscard]] TabOptions tabOptions(TabId tab) const;
    PaneId splitPane(TabId tab, PaneId pane, QWidget* content, Placement placement);
    bool closeTab(TabId tab);
    bool renameTab(TabId tab, QString title);
    bool closePane(TabId tab, PaneId pane);
    bool closeTabs(const QVector<TabId>& tabs);
    bool requestCloseWindow(WindowId window);
    // Approve the complete workspace once, then close it atomically. The
    // final-save notification observes every original window and content ID.
    bool requestCloseAllWindows();
    bool moveTab(TabId tab, GroupId group, Placement placement = Placement::Center,
                 int insertionIndex = -1);
    bool moveTabToWindow(TabId tab, WindowId target,
                         Placement placement = Placement::Center);
    bool reorderTab(TabId tab, bool forward);
    bool moveTabToAdjacentGroup(TabId tab, Placement direction);
    bool moveTabToWindowCycle(TabId tab, bool forward);
    bool movePane(TabId sourceTab, PaneId pane, TabId targetTab,
                   PaneId targetPane = {}, Placement placement = Placement::Right);
    bool movePaneToAdjacentTab(TabId tab, PaneId pane, bool forward);
    bool movePaneToAdjacentGroup(TabId tab, PaneId pane, Placement direction);
    bool movePaneToWindow(TabId tab, PaneId pane, WindowId target,
                          Placement placement = Placement::Right);
    bool movePaneToWindowCycle(TabId tab, PaneId pane, bool forward);
    bool swapPanes(TabId tab, PaneId first, PaneId second);
    bool swapPaneDirection(TabId tab, PaneId pane, Placement direction);
    WindowId detachTab(TabId tab, WindowKind kind = WindowKind::Floating);
    bool moveAllTabs(WindowId source, GroupId group,
                     Placement placement = Placement::Center, int insertionIndex = -1);
    bool moveTabToEdge(TabId tab, Edge edge, bool pinned = false);
    bool moveTabToEdgeBar(TabId tab, WindowId target, Edge edge, int insertionIndex = -1);
    bool moveAllTabsToEdgeBar(WindowId source, WindowId target, Edge edge, int insertionIndex = -1);
    bool moveAllTabsToEdge(WindowId source, WindowId target, Edge edge, bool pinned = false);
    bool pinGroup(GroupId group, bool pinned);
    bool pinTab(TabId tab, bool pinned);
    bool pinEdge(WindowId window, Edge edge, bool pinned);
    // Each auto-hide tab remembers separate left/right and top/bottom policies.
    // The edge is mandatory; resetting one orientation leaves the other intact.
    // A smaller viewport limits presentation without replacing this request.
    bool setPeekSize(TabId tab, Edge edge, QSize size);
    bool setPeekSizePolicy(TabId tab, Edge edge, const PeekSizePolicy& policy);
    bool resetPeekSize(TabId tab, Edge edge);
    [[nodiscard]] QSize peekSize(TabId tab, Edge edge) const;
    [[nodiscard]] PeekSizePolicy peekSizePolicy(TabId tab, Edge edge) const;
    bool moveGroupToCenter(GroupId group);
    bool promoteWindow(WindowId window);
    bool demoteWindow(WindowId window, WindowId targetWorkbench);
    bool reassignFamily(WindowId window, WindowId targetWorkbench);
    TabId extractPane(TabId tab, PaneId pane, WindowKind kind = WindowKind::Floating);

    void showWindow(WindowId window);
    bool activateWindow(WindowId window);
    bool setWindowTitle(WindowId window, QString title);
    bool activateTab(TabId tab);
    bool focusPane(TabId tab, PaneId pane);
    [[nodiscard]] QWidget* windowWidget(WindowId window) const;
    [[nodiscard]] QWidget* paneWidget(PaneId pane) const;
    [[nodiscard]] const LayoutModel& model() const;
    [[nodiscard]] CommandContext context(WindowId window = {}) const;
    [[nodiscard]] WindowBehavior behavior() const;
    void setBehavior(const WindowBehavior& behavior);
    void setTheme(const Theme& theme);
    [[nodiscard]] Theme theme() const;
    [[nodiscard]] TabAppearance tabAppearance() const;
    bool setTabAppearance(const TabAppearance& appearance);
    [[nodiscard]] std::optional<TabColorPreset> tabColorPreset(const QString& id) const;
    CommandRegistry& commands();
    bool execute(const QString& commandId, const QVariantMap& args = {}, WindowId window = {});
    bool executeInContext(const QString& commandId, const CommandContext& context);
    void showCommandPalette(WindowId window = {}, const FloatingPanelOptions& options = {});
    void showWindowSelector(WindowId window = {}, const FloatingPanelOptions& options = {});
    void showSettings(WindowId window = {});
    // Restore a chrome strip's natural width, preserving its position and the
    // width preferences of other strips. Sizing policy is unchanged.
    bool resetToolbarWidth(WindowId window, const QString& stripId);
    // Move an existing strip without replacing its content. Row is a saved
    // logical slot or the next slot after the last row; index -1 appends.
    bool moveToolbar(WindowId window, const QString& stripId, ToolbarArea area,
                     int row, int index = -1);
    // Dynamic definitions apply to existing and future windows. IDs must not
    // collide with built-in chrome. Removal releases presentations and their
    // saved preferences; preserve toolbarState explicitly across replacement.
    bool registerToolbar(CommandToolbar toolbar);
    bool updateToolbar(CommandToolbar toolbar); // Same ID; preserves each window's layout.
    bool removeToolbar(const QString& id);
    [[nodiscard]] QVector<CommandToolbar> toolbars() const;
    [[nodiscard]] std::optional<ToolbarState> toolbarState(WindowId window, const QString& id) const;
    bool setToolbarVisible(WindowId window, const QString& id, bool visible);
    bool setToolbarWidth(WindowId window, const QString& id, int width);
    bool setToolbarElastic(WindowId window, const QString& id, bool elastic);
    bool setToolbarItemVisible(WindowId window, const QString& id, const QString& item, bool visible);
    bool setToolbarsLocked(WindowId window, bool locked);
    // Floating windows retain their existing compact chrome policy (no global
    // status bar); status definitions survive conversion to a full window.
    bool registerStatusItem(StatusItem item);
    bool removeStatusItem(const QString& id);
    [[nodiscard]] QVector<StatusItem> statusItems() const;
    [[nodiscard]] QStringList statusItemIds() const;
    bool setStatusItemText(const QString& id, QString text, QString tooltip = {});
    [[nodiscard]] QJsonObject saveLayout() const;
    OperationResult restoreLayout(const QJsonObject& snapshot);
    void setStatus(WindowId window, const QString& text);
    bool setStatusItemVisible(WindowId window, const QString& item, bool visible);
    [[nodiscard]] bool statusItemVisible(WindowId window, const QString& item) const;

signals:
    void commandsChanged();
    void layoutChanged();
    void commandFailed(const QString& message);
    void allWindowsClosed();
    // Emitted before an approved close removes the final window. The layout
    // is still available for the host to persist synchronously.
    void lastWindowAboutToClose();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    friend class WorkspaceWindow;
    friend class DockSurface;
    friend class DragController;
    friend class TabContent;
    struct Private;
    std::unique_ptr<Private> d;
    void refresh(bool preserveDrag = false);
    // saveLayout() without the model: window placement, options, toolbars,
    // pane descriptors and profiles of the live (not drag-stable) state.
    QJsonObject saveViewLayout() const;
    void registerWindowCommands();
    void registerTabGroupCommands();
    void registerHierarchyCommands();
    bool prepareHierarchyBars(const QVector<QPair<TabId, PaneId>>& tabs);
    void usePerPaneBars(TabId tab);
    void registerCustomChromeCommands();
    bool applyResult(const OperationResult& result);
    TabContent* ensureTabView(TabId tab);
    void scheduleContentVisibilityNotification();
    void reportPanesHiddenBeforeReparent(const QVector<PaneId>& panes);
    bool approveClose(const QVector<TabId>& tabs);
    bool setPaneBar(PaneId pane, QWidget* bar, bool top);
    bool setTabBar(TabId tab, QWidget* bar, bool top);
    bool setTabBarSharing(TabId tab, PaneBarSharing sharing, bool top);
    QWidget* createContent(const TabOptions& options, PaneId pane);
    bool focusGroup(GroupId group);
    bool navigate(Placement direction, bool panes, const CommandContext& source);
    GroupId adjacentGroup(TabId tab, Placement direction, std::optional<int> anchor = {}) const;
    GroupId adjacentVisibleGroup(GroupId group, Placement direction, bool forMove = false) const;
    PaneId adjacentPane(TabId tab, PaneId pane, Placement direction, std::optional<int> anchor = {}) const;
    WindowId adjacentWindow(WindowId window, bool forward) const;
    void resizeActive(Placement direction, bool panes, const CommandContext& target);
    void focusWindowCycle(bool forward, const QString& scope);
    void cancelPendingShortcut();
    void beginTabDrag(TabId tab, QPoint globalPosition);
    WindowId detachTabImpl(TabId tab, WindowKind kind, const std::function<void(QWidget*)>& beforeShow, bool preserveDrag = false);
    void beginWindowDrag(WindowId window, QPoint globalPosition);
    void showTabMenu(TabId tab, QPoint globalPosition);
    void showPaneMenu(TabId tab, PaneId pane, QPoint globalPosition, QWidget* content);
    void showWindowMenu(WindowId window, QPoint globalPosition);
    void updateActive(WindowId window, GroupId group, TabId tab);
    bool setBarVisible(WindowId window, WindowBar bar, bool visible);
    void applyWindowViewState(WindowId window);
    void applyWindowViewStates();
    void syncVisibilityKind(WindowId window);
    void reconcileWindowViewState(WindowId window);
    void leaveFocusScopeFor(TabId tab, PaneId pane);
    bool executeResolved(const QString& commandId, CommandContext context, bool requireGroup = false);
    bool validCommandContext(const CommandContext& context) const;
    QMenu* createTabCreationMenu(WindowId window, GroupId group, QWidget* parent);
};

} // namespace window_system
