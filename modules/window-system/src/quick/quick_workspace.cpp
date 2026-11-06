// SPDX-License-Identifier: MIT
#include <window_system/quick_workspace.hpp>
#include <window_system/window_manager.hpp>
#include "button_item.hpp"
#include "content_host.hpp"
#include "content_bars.hpp"
#include "dock_surface_item.hpp"
#include "popup_menu.hpp"
#include "tab_drag.hpp"
#include "window_chrome_item.hpp"
#include "command_strip_host_item.hpp"
#include "status_bar_item.hpp"
#include "command_panel.hpp"
#include "settings_page_model.hpp"
#include "path_item.hpp"
#include "dialog_window.hpp"
#include "commands/command_dialogs.hpp"
#include "window_manager_p.hpp"
#ifdef Q_OS_WIN
#include "platform/windows/window_frame.hpp"
#endif

#include <QCoreApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QPointer>
#include <QSet>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QtMath>
#include <algorithm>
#include <utility>

namespace window_system {
namespace {

QVector<MenuItemDescription> windowMenus(WindowManager* manager, WindowId id)
{
    if (!manager) return {};
    const auto* record = manager->model().window(id);
    if (!record) return {};
    return describeApplicationMenu(record->kind == WindowKind::Floating
        ? CommandMenuSurface::FloatingApplication : CommandMenuSurface::Application, manager, id);
}

// A workspace window: the header row above the dock, in a native frame
// without the system caption. Closing asks the manager, which may refuse.
class WorkspaceQuickWindow final : public QQuickWindow {
public:
    WorkspaceQuickWindow(WindowManager& manager, quick::ContentHost& host, quick::ContentBars& bars,
                         quick::TabDrag& drag, WindowId id, const QVector<MenuItemDescription>& menuItems)
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
        status_ = new quick::StatusBarItem(contentItem());
        status_->setTheme(theme);
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
        dock->dragPresentationChanged = [this] { layoutChildren(); };
        frameLine_ = new quick::PathItem(contentItem());
        frameLine_->setZ(1);
        frameLine_->setFill(theme.border);
        toolbars_ = new quick::CommandStripHostItem(contentItem());
        toolbars_->maximizeRequested = chrome_->maximizeRequested;
        toolbars_->carriesTabs = chrome_->carriesTabs;
        toolbars_->carryRequested = chrome_->carryRequested;
        toolbars_->triggered = [owner = QPointer<WindowManager>(&manager), id](const QString& command, const QVariantMap& arguments) {
            if (owner) owner->execute(command, arguments, id);
        };
        connect(toolbars_, &QQuickItem::implicitHeightChanged, this, [this] { layoutChildren(); }, Qt::QueuedConnection);
        connect(toolbars_->bottomArea(), &QQuickItem::implicitHeightChanged, this, [this] { layoutChildren(); }, Qt::QueuedConnection);
        connect(status_, &QQuickItem::implicitHeightChanged, this, [this] { layoutChildren(); }, Qt::QueuedConnection);
        status_->triggered = [owner = QPointer<WindowManager>(&manager), id](const QString& item, const QString& command) {
            if (!owner || !owner->model().window(id)) return;
            // Only an item still defined with that command acts.
            const auto items = owner->statusItems();
            if (std::any_of(items.cbegin(), items.cend(), [&](const auto& entry) { return entry.id == item && entry.command == command; }))
                owner->execute(command, {}, id);
        };
        status_->menuRequested = [this](QPoint global) { dock->showMenu(statusMenu(), manager_.context(id_), global); };
        createMenus(theme, menuItems);
        // As the Widgets header: the toolbar area's menu, then the window's.
        chrome_->menuRequested = [this](QPoint global) {
            auto items = describeToolbarAreaMenu(*toolbars_);
            const auto context = manager_.context(id_);
            const auto window = describeContextMenu(CommandMenuSurface::Window, &manager_, context);
            if (!window.isEmpty()) {
                MenuItemDescription separator;
                separator.kind = MenuItemDescription::Kind::Separator;
                items.append(std::move(separator));
                items += window;
            }
            dock->showMenu(items, context, global);
        };
        toolbars_->menuRequested = chrome_->menuRequested;
        // As focus in a Widgets window does: the window the user turns to
        // becomes the manager's current one.
        connect(this, &QWindow::activeChanged, this, [this] {
            if (!isActive() || manager_.context().window == id_) return;
            if (const auto tab = manager_.context(id_).tab; !tab.isNull()) manager_.activateTab(tab);
        });
        connect(this, &QWindow::windowStateChanged, this, [this](Qt::WindowState state) {
            chrome_->setExpanded(state == Qt::WindowMaximized || state == Qt::WindowFullScreen);
            layoutChildren();
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
        client.minimumSize = [this] {
            const auto body = dock->minimumSize();
            const int tools = toolbars_->isVisible() ? qCeil(toolbars_->implicitHeight() + toolbars_->bottomArea()->implicitHeight()) : 0;
            const int status = status_->isVisible() ? qCeil(status_->implicitHeight()) : 0;
            return QSize(std::max(body.width(), toolbars_->isVisible() ? toolbars_->minimumWidth() : 0), body.height() + tools + status);
        };
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

    void syncToolbars(const QVector<CommandToolbarDescription>& descriptions, const QJsonObject& layout)
    {
        const auto menuId = setMenuContent();
        toolbars_->sync(descriptions, layout, manager_.interfaceTheme(), manager_.behavior().hideToolbarOptionsWhenLocked);
        menus_->setExcludedRoots(toolbars_->excludedItems(menuId));
        layoutChildren();
    }
    ToolbarEditor* toolbarEditor() const { return toolbars_; }
    void setToolbarCommit(std::function<void(const QJsonObject&, const QJsonObject&)> commit)
    {
        toolbars_->committed = std::move(commit);
    }

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
        chrome_->setButtonOrder(manager_.behavior().captionButtonOrder);
        if (menus_) menus_->setPresentation(manager_.interfaceTheme());
        const auto* tab = manager_.model().tab(manager_.context(id_).tab);
        status_->setWorkspace(tab ? tab->title : QString(), int(manager_.model().orderedTabs(id_).size()));
    }

    void syncStatus(const QString& message, const QString& transient)
    {
        status_->setMessage(message, transient);
        status_->setItems(manager_.statusItems(), manager_.windowOptions(id_).hiddenStatusItems);
    }

    void applyViewState()
    {
        const auto state = manager_.windowViewState(id_);
        // Floating windows keep their compact chrome, without a status bar.
        const auto* record = manager_.model().window(id_);
        const bool status = state.effective.statusBar && record && record->kind == WindowKind::Workbench;
        const bool changed = toolbars_->isVisible() != state.effective.toolbars || status_->isVisible() != status;
        toolbars_->setVisible(state.effective.toolbars);
        toolbars_->bottomArea()->setVisible(state.effective.toolbars);
        status_->setVisible(status);
        if (changed) layoutChildren();
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
        // Qt posts font and DPR changes to QWindows. Invalidate header metrics
        // at that boundary, without filtering every application input event.
        if (event->type() == QEvent::ApplicationFontChange || event->type() == QEvent::DevicePixelRatioChange)
            QCoreApplication::sendEvent(chrome_, event);
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
    void createMenus(const Theme& theme, const QVector<MenuItemDescription>& menuItems)
    {
        menus_ = new quick::PopupMenu(theme, this);
        menuBar_ = menus_->createMenuBar(contentItem(), menuItems);
        setMenuContent();
        toolbars_->setContent(QStringLiteral("title"), QCoreApplication::translate("WindowSystem", "Window title"),
            chrome_, {true, true, 96, true}, StripSizing::Elastic);
        toolbars_->setContent(QStringLiteral("caption-controls"), QCoreApplication::translate("WindowSystem", "Window controls"),
            chrome_->captionContent(), {false, false, 12, false, true});
        const QPointer<WindowManager> owner(&manager_);
        menus_->setRefreshHandler([owner, id = id_] { return windowMenus(owner, id); });
        connect(&manager_, &WindowManager::commandsChanged, menus_, &quick::PopupMenu::requestCatalogRefresh);
        connect(&manager_, &WindowManager::layoutChanged, menus_, &quick::PopupMenu::requestRefresh);
        connect(menus_, &quick::PopupMenu::presentationChanged, chrome_, [this] {
            const QPointer<WorkspaceQuickWindow> self(this);
            setColor(menus_->theme().headerBackground);
            if (!self) return;
            chrome_->setTheme(menus_->theme());
            status_->setTheme(menus_->theme());
            frameLine_->setFill(menus_->theme().border);
            layoutChildren();
        }, Qt::QueuedConnection);
        connect(menus_, &quick::PopupMenu::contextMenuRequested, dock, [this](QPoint global) {
            dock->showContextMenu(CommandMenuSurface::Group, manager_.context(id_), global);
        });
        connect(menus_, &quick::PopupMenu::triggered, menus_, [owner, id = id_](const MenuItemDescription& item) {
            if (!owner) return;
            auto context = owner->context(id);
            context.args = item.arguments;
            owner->executeInContext(item.commandId, context);
        });
    }

    // The menu strip's ID follows the window kind; its items are the roots.
    QString setMenuContent()
    {
        const auto* record = manager_.model().window(id_);
        Q_ASSERT(record);
        const auto menuId = record->kind == WindowKind::Floating ? QStringLiteral("floating-menu") : QStringLiteral("menu");
        toolbars_->setContent(menuId, QCoreApplication::translate("WindowSystem", "Menu"), menuBar_, {true, true, 24},
            StripSizing::Content, [menus = QPointer<quick::PopupMenu>(menus_)] { return menus ? menus->menuBarItemIds() : QStringList{}; },
            true);
        return menuId;
    }

    // As the Widgets column: toolbars, the dock, the bottom toolbars, then
    // the status bar along the window's bottom edge.
    void layoutChildren()
    {
        // As the Widgets window: inside the native frame a normal window
        // keeps a border line along its top; an expanded or carried window
        // has none.
        const bool framed = !(windowStates() & (Qt::WindowMaximized | Qt::WindowFullScreen))
            && manager_.viewMode(id_) == ViewMode::Normal && !dock->dragPresentation();
        const int inset = framed ? 1 : 0;
        frameLine_->setVisible(framed);
        if (framed) {
            frameLine_->setSize(QSizeF(width(), inset));
            QPainterPath line;
            line.addRect(QRectF(0, 0, width(), inset));
            frameLine_->setPath(line);
        }
        const int top = toolbars_->isVisible() ? qCeil(toolbars_->implicitHeight()) : 0;
        const int bottom = toolbars_->bottomArea()->isVisible() ? qCeil(toolbars_->bottomArea()->implicitHeight()) : 0;
        const int status = status_->isVisible() ? qCeil(status_->implicitHeight()) : 0;
        toolbars_->setPosition({0, qreal(inset)});
        toolbars_->setSize({qreal(width()), qreal(top)});
        toolbars_->bottomArea()->setPosition({0, qreal(height() - status - bottom)});
        status_->setPosition({0, qreal(height() - status)});
        status_->setSize({qreal(width()), qreal(status)});
        dock->setPosition(QPointF(0, inset + top));
        dock->setSize(QSizeF(width(), std::max(0, height() - inset - top - bottom - status)));
    }

    // Each status entry with whether it shows, as the Widgets bar's menu.
    QVector<MenuItemDescription> statusMenu() const
    {
        QVector<QPair<QString, QString>> entries{
            {QStringLiteral("message"), QCoreApplication::translate("WindowSystem", "Status message")},
            {QStringLiteral("workspace"), QCoreApplication::translate("WindowSystem", "Workspace information")}};
        for (const auto& item : manager_.statusItems()) {
            auto title = item.text;
            if (title.isEmpty() && !item.command.isEmpty())
                if (const auto* command = manager_.commands().find(item.command)) title = command->title;
            entries.append({item.id, title.isEmpty() ? item.id : title});
        }
        QVector<MenuItemDescription> result;
        for (const auto& [id, title] : entries) {
            const bool visible = manager_.statusItemVisible(id_, id);
            MenuItemDescription entry;
            entry.id = QStringLiteral("StatusItemToggle/") + id;
            entry.commandId = QStringLiteral("window.statusItem.setVisible");
            entry.action.title = title;
            entry.action.checkable = true;
            entry.action.checked = visible;
            entry.arguments = {{"item", id}, {"visible", !visible}};
            result.append(std::move(entry));
        }
        return result;
    }
    WindowManager& manager_;
    WindowId id_;
    quick::WindowChromeItem* chrome_;
    quick::CommandStripHostItem* toolbars_;
    quick::StatusBarItem* status_;
    quick::PathItem* frameLine_ = nullptr;
    quick::PopupMenu* menus_ = nullptr;
    QPointer<QQuickItem> menuBar_;
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
    std::unique_ptr<quick::QuickDialogs> dialogs;

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
    connect(&manager, &WindowManager::commandPaletteRequested, this, [this](const CommandContext& context, const FloatingPanelOptions& options) {
        const QPointer<WindowManager> owner(&d->manager);
        quick::CommandPanel::Setup setup;
        setup.name = QStringLiteral("CommandPalette");
        setup.title = QCoreApplication::translate("WindowSystem", "Command palette");
        setup.placeholder = QCoreApplication::translate("WindowSystem", "Type a command name, category, or ID…");
        setup.acceptText = QCoreApplication::translate("WindowSystem", "Run");
        setup.rejectText = QCoreApplication::translate("WindowSystem", "Close");
        setup.columns = {QCoreApplication::translate("WindowSystem", "Command"), QCoreApplication::translate("WindowSystem", "Category"),
                         QCoreApplication::translate("WindowSystem", "Shortcut")};
        setup.size = {780, 450};
        setup.hint = [](const PanelRow* row) {
            return row ? row->id : QCoreApplication::translate("WindowSystem", "No matching command is available.");
        };
        setup.rows = [owner, context](const QString& query) { return owner ? commandPaletteRows(owner, context, query) : QVector<PanelRow>{}; };
        setup.acceptable = [owner, context](const PanelRow& row) { return owner && canRunFromPalette(owner, row.id, context); };
        setup.accepted = [owner, context](const QString& id) {
            if (!owner) return;
            const bool executed = owner->executeInContext(id, context);
            if (!executed && owner && owner->model().window(context.window))
                owner->setStatus(context.window, QCoreApplication::translate("WindowSystem", "Command was not executed: %1").arg(id));
        };
        new quick::CommandPanel(d->windows.value(context.window), d->manager.interfaceTheme(), options, std::move(setup));
    });
    connect(&manager, &WindowManager::windowSelectorRequested, this, [this](WindowId source, const FloatingPanelOptions& options) {
        const QPointer<WindowManager> owner(&d->manager);
        quick::CommandPanel::Setup setup;
        setup.name = QStringLiteral("WindowSelector");
        setup.title = QCoreApplication::translate("WindowSystem", "Switch window");
        setup.placeholder = QCoreApplication::translate("WindowSystem", "Search windows, owners, or window types…");
        setup.acceptText = QCoreApplication::translate("WindowSystem", "Switch");
        setup.rejectText = QCoreApplication::translate("WindowSystem", "Cancel");
        setup.columns = {QCoreApplication::translate("WindowSystem", "Window"), QCoreApplication::translate("WindowSystem", "Type"),
                         QCoreApplication::translate("WindowSystem", "Owner workbench"), QCoreApplication::translate("WindowSystem", "Tab")};
        setup.size = {780, 410};
        setup.initial = source.toString(QUuid::WithoutBraces);
        setup.hint = [](const PanelRow*) {
            return QCoreApplication::translate("WindowSystem", "Includes all workbench and floating windows. Enter to switch; Esc to cancel.");
        };
        setup.rows = [owner](const QString& query) { return owner ? windowSelectorRows(owner, query) : QVector<PanelRow>{}; };
        setup.acceptable = [owner](const PanelRow& row) { return owner && owner->model().window(WindowId(row.id)); };
        setup.accepted = [owner](const QString& id) { if (owner) owner->activateWindow(WindowId(id)); };
        auto* panel = new quick::CommandPanel(d->windows.value(source), d->manager.interfaceTheme(), options, std::move(setup));
        // Windows opened or closed meanwhile appear in the list.
        connect(&d->manager, &WindowManager::layoutChanged, panel, &quick::CommandPanel::refresh);
    });
    connect(&manager, &WindowManager::statusChanged, this, [this](WindowId changed) {
        for (auto it = d->windows.cbegin(); it != d->windows.cend(); ++it)
            if (it.value() && (changed.isNull() || it.key() == changed))
                it.value()->syncStatus(d->manager.d->statusMessages.value(it.key(), QCoreApplication::translate("WindowSystem", "Ready")),
                                       d->manager.d->transientStatus.value(it.key()));
    });
    d->dialogs = std::make_unique<quick::QuickDialogs>(this, [this](WindowId window) -> QWindow* { return d->windows.value(window); },
        [this] { return d->manager.interfaceTheme(); });
    manager.d->dialogPresenter = d->dialogs.get();
    manager.d->toolbarEditors = [this](WindowId window) -> ToolbarEditor* {
        const auto view = d->windows.value(window);
        return view ? view->toolbarEditor() : nullptr;
    };
    // Choices for a command run without arguments: picking one runs it again
    // with the choice's arguments, as the Widgets pickers do.
    connect(&manager, &WindowManager::choicesOffered, this,
        [this](const CommandContext& context, const QString& command, const WindowManager::CommandChoices& choices) {
            const auto view = d->windows.value(context.window);
            if (!view) return;
            QVector<MenuItemDescription> items;
            for (const auto& [path, arguments] : choices) {
                auto* level = &items;
                for (int depth = 0; depth + 1 < path.size(); ++depth) {
                    auto found = std::find_if(level->begin(), level->end(), [&](const auto& item) {
                        return item.kind == MenuItemDescription::Kind::Submenu && item.action.title == path.at(depth);
                    });
                    if (found == level->end()) {
                        MenuItemDescription submenu;
                        submenu.kind = MenuItemDescription::Kind::Submenu;
                        submenu.id = path.mid(0, depth + 1).join(QLatin1Char('/'));
                        submenu.action.title = path.at(depth);
                        level->append(std::move(submenu));
                        found = level->end() - 1;
                    }
                    level = &found->children;
                }
                MenuItemDescription item;
                item.id = path.join(QLatin1Char('/'));
                item.commandId = command;
                item.action.title = path.constLast();
                item.arguments = arguments;
                level->append(std::move(item));
            }
            view->dock->showMenu(items, context, QCursor::pos());
        });
    connect(&manager, &WindowManager::contextMenuRequested, this,
        [this](CommandMenuSurface surface, const CommandContext& context, QPoint global, QObject* source) {
            if (const auto window = d->windows.value(context.window); window && d->manager.validCommandContext(context))
                window->dock->showContextMenu(surface, context, global, nullptr, qobject_cast<QQuickItem*>(source));
        });
    // Model changes often come from a strip's own input handler; rebuilding
    // there would replace the strip under it. One sync per event-loop turn.
    connect(&manager, &WindowManager::layoutChanged, this, &QuickWorkspace::requestSync);
    connect(&manager, &WindowManager::commandsChanged, this, &QuickWorkspace::requestSync);
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
    d->manager.d->toolbarEditors = {};
    d->manager.d->dialogPresenter = nullptr;
    d->dialogs.reset();
    d->host.setVisibilityHandler({});
    d->manager.setPresenter(nullptr);
    d->drag.cancel();
    const auto windows = std::exchange(d->ownedWindows, {});
    for (auto* window : windows) delete window;
}

void QuickWorkspace::setContentFactory(ContentFactory factory)
{
    d->host.setFactory([this, factory = std::move(factory)](const TabOptions& options, PaneId pane) -> QQuickItem* {
        if (isInternalSettingsContentType(options.contentType)) return quick::createSettingsContent(d->manager, options);
        return factory ? factory(options, pane) : nullptr;
    });
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
        const auto toolbars = describeCommandToolbars(&d->manager, id);
        if (!self) return;
        if (!model.window(id)) continue;
        auto& window = d->windows[id];
        const bool creating = !window;
        if (!window) {
            // Queries can call host code. Resolve before constructing a native
            // window, then recheck the workspace and model before using them.
            const auto menus = windowMenus(&d->manager, id);
            if (!self) return;
            if (!model.window(id)) continue;
            window = new WorkspaceQuickWindow(d->manager, d->host, d->bars, d->drag, id, menus);
            connect(window->dock, &quick::DockSurfaceItem::paneMenuRequested, this,
                [this](TabId tab, PaneId pane, QPoint global, QQuickItem* source) { d->manager.showPaneMenu(tab, pane, global, source); });
            d->ownedWindows.insert(window);
            connect(window, &QObject::destroyed, this, [this, retired = window.data()] { d->ownedWindows.remove(retired); });
            connect(window, &QQuickWindow::activeFocusItemChanged, this, [this, observed = QPointer<QQuickWindow>(window)] {
                if (d->projecting || !observed) return;
                const auto pane = d->bars.paneForItem(d->manager.model(), observed->activeFocusItem());
                if (!pane.isNull()) d->manager.contentFocused(pane, observed);
            });
            window->setToolbarCommit([this, id](const QJsonObject& before, const QJsonObject& after) {
                d->manager.commitToolbarLayout(id, before, after);
            });
            window->syncToolbars(toolbars, d->manager.toolbarLayout(id));
            if (!self) return;
            window->syncStatus(d->manager.d->statusMessages.value(id, QCoreApplication::translate("WindowSystem", "Ready")),
                               d->manager.d->transientStatus.value(id));
            if (!model.window(id)) continue;
        }
        if (!creating) window->syncToolbars(toolbars, d->manager.toolbarLayout(id));
        if (!self) return;
        window->dock->sync();
        window->updateCaption();
        window->applyViewState();
        // A new window shows once its groups are laid out, as a widget window
        // shows its laid-out children. Showing renders the first frame at once
        // on some platforms; before the groups exist it would be a layout in
        // progress, which later changes would then animate away from.
        if (creating && window) {
            if (const auto placement = d->placements.take(id); placement.first.isValid())
                window->place(placement.first, placement.second);
            else window->show();
        }
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
