#pragma once

#include <window_system/command_registry.hpp>
#include <window_system/theme.hpp>
#include <QJsonObject>
#include <QObject>
#include <QSize>
#include <memory>

class QWidget;

namespace window_system {

struct WindowOptions {
    QString title = QStringLiteral("WindowSystem");
    QSize size = QSize(1280, 800);
};

struct TabOptions {
    QString title;
    QString contentType;
    QVariantMap state;
    bool showIcon = true;
};

struct WindowBehavior {
    WindowKind dragOutsideKind = WindowKind::Floating;
    bool closeEmptyWorkbench = false;
    bool revealDuringDrag = true;
    int revealDelayMs = 500;
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
                 GroupId group = {});
    void setContentFactory(ContentFactory factory);
    void setCloseGuard(CloseGuard guard);
    bool setTabTopBar(TabId tab, QWidget* bar);
    bool setTabBottomBar(TabId tab, QWidget* bar);
    bool setPaneTopBar(PaneId pane, QWidget* bar);
    bool setPaneBottomBar(PaneId pane, QWidget* bar);
    bool setTabIconVisible(TabId tab, bool visible);
    [[nodiscard]] TabOptions tabOptions(TabId tab) const;
    PaneId splitPane(TabId tab, PaneId pane, QWidget* content, Placement placement);
    bool closeTab(TabId tab);
    bool closePane(TabId tab, PaneId pane);
    bool closeTabs(const QVector<TabId>& tabs);
    bool requestCloseWindow(WindowId window);
    bool moveTab(TabId tab, GroupId group, Placement placement = Placement::Center,
                 int insertionIndex = -1);
    WindowId detachTab(TabId tab, WindowKind kind = WindowKind::Floating);
    bool moveAllTabs(WindowId source, GroupId group,
                     Placement placement = Placement::Center, int insertionIndex = -1);
    bool moveTabToEdge(TabId tab, Edge edge, bool pinned = false);
    bool moveAllTabsToEdge(WindowId source, WindowId target, Edge edge, bool pinned = false);
    bool pinGroup(GroupId group, bool pinned);
    bool pinTab(TabId tab, bool pinned);
    bool pinEdge(WindowId window, Edge edge, bool pinned);
    bool moveGroupToCenter(GroupId group);
    bool promoteWindow(WindowId window);
    bool demoteWindow(WindowId window, WindowId targetWorkbench);
    bool reassignFamily(WindowId window, WindowId targetWorkbench);
    TabId extractPane(TabId tab, PaneId pane, WindowKind kind = WindowKind::Floating);

    void showWindow(WindowId window);
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
    CommandRegistry& commands();
    bool execute(const QString& commandId, const QVariantMap& args = {}, WindowId window = {});
    void showCommandPalette(WindowId window = {});
    void showWindowSelector(WindowId window = {});
    void showSettings(WindowId window = {});
    [[nodiscard]] QJsonObject saveLayout() const;
    OperationResult restoreLayout(const QJsonObject& snapshot);
    void setStatus(WindowId window, const QString& text);

signals:
    void layoutChanged();
    void commandFailed(const QString& message);
    void allWindowsClosed();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    friend class WorkspaceWindow;
    friend class DockSurface;
    friend class DragController;
    friend class TabContent;
    struct Private;
    std::unique_ptr<Private> d;
    void refresh();
    void registerWindowCommands();
    bool applyResult(const OperationResult& result);
    bool approveClose(const QVector<TabId>& tabs);
    bool setPaneBar(PaneId pane, QWidget* bar, bool top);
    QWidget* createContent(const TabOptions& options, PaneId pane);
    bool focusGroup(GroupId group);
    bool navigate(Placement direction, bool panes);
    void resizeActive(Placement direction, bool panes);
    void focusWindowCycle(bool forward, const QString& scope);
    void beginTabDrag(TabId tab, QPoint globalPosition);
    void beginWindowDrag(WindowId window, QPoint globalPosition);
    void showTabMenu(TabId tab, QPoint globalPosition);
    void showPaneMenu(TabId tab, PaneId pane, QPoint globalPosition, QWidget* content);
    void showWindowMenu(WindowId window, QPoint globalPosition);
    void updateActive(WindowId window, GroupId group, TabId tab);
};

} // namespace window_system
