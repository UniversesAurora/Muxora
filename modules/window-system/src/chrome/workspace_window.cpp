// SPDX-License-Identifier: MIT
#include "workspace_window.hpp"
#include "command_strip.hpp"
#include "window_title_label.hpp"
#include "commands/command_menus.hpp"
#include "docking/dock_surface.hpp"
#include "docking/drag_presentation.hpp"
#include "docking/tab_strip.hpp"
#include "platform/windows/window_frame.hpp"
#include "window_manager_p.hpp"
#include <QApplication>
#include <QAction>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QMenuBar>
#include <QMenu>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QSet>
#include <QScopeGuard>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

namespace window_system {
namespace {

qsizetype newStripInsertion(const QJsonArray& merged, const QJsonArray& current, qsizetype index)
{
    for (qsizetype before = index; before-- > 0;)
        for (qsizetype position = 0; position < merged.size(); ++position)
            if (merged[position] == current[before]) return position + 1;
    for (qsizetype after = index + 1; after < current.size(); ++after)
        for (qsizetype position = 0; position < merged.size(); ++position)
            if (merged[position] == current[after]) return position;
    return merged.size();
}

QJsonArray mergeStripRow(const QJsonArray& previous, const QJsonArray& current,
                        const QSet<QString>& present, const QSet<QString>& cachedIds)
{
    QJsonArray currentExisting;
    for (const auto& entry : current)
        if (cachedIds.contains(entry.toString())) currentExisting.append(entry);
    QJsonArray merged;
    qsizetype next = 0;
    // Reuse shared slots while keeping dormant Workbench strips in place.
    for (const auto& entry : previous) {
        if (!present.contains(entry.toString())) merged.append(entry);
        else if (next < currentExisting.size()) merged.append(currentExisting[next++]);
    }
    while (next < currentExisting.size()) merged.append(currentExisting[next++]);
    // New presentations are inserted beside their renderer neighbours after
    // merging. They cannot consume a shared strip's original slot.
    for (qsizetype index = 0; index < current.size(); ++index) {
        const auto entry = current[index];
        if (!cachedIds.contains(entry.toString())) merged.insert(newStripInsertion(merged, current, index), entry);
    }
    return merged;
}

QJsonObject mergeRenderedStripLayout(const QJsonObject& cached, const QJsonObject& rendered)
{
    if (cached.isEmpty()) return rendered;
    QSet<QString> present;
    QSet<QString> cachedIds;
    for (const auto* field : {"rows", "bottomRows"})
        for (const auto& row : cached.value(field).toArray())
            for (const auto& entry : row.toArray()) cachedIds.insert(entry.toString());
    auto settings = cached.value("strips").toObject();
    const auto renderedSettings = rendered.value("strips").toObject();
    for (auto entry = renderedSettings.begin(); entry != renderedSettings.end(); ++entry) {
        present.insert(entry.key()); settings.insert(entry.key(), entry.value());
    }
    auto result = cached;
    for (const auto* field : {"rows", "bottomRows"}) {
        const auto cachedRows = cached.value(field).toArray();
        const auto renderedRows = rendered.value(field).toArray();
        QJsonArray rows;
        for (qsizetype row = 0; row < qMax(cachedRows.size(), renderedRows.size()); ++row) {
            const auto current = row < renderedRows.size() ? renderedRows.at(row).toArray() : QJsonArray{};
            const auto previous = row < cachedRows.size() ? cachedRows.at(row).toArray() : QJsonArray{};
            const auto merged = mergeStripRow(previous, current, present, cachedIds);
            if (!merged.isEmpty()) rows.append(merged);
        }
        if (QString::fromLatin1(field) == "rows") {
            while (rows.size() < 2) rows.append(QJsonArray{});
            result.insert(field, rows);
        } else if (rows.isEmpty()) result.remove(field);
        else result.insert(field, rows);
    }
    result.insert("locked", rendered.value("locked"));
    result.insert("strips", settings);
    return result;
}

QJsonObject floatingMenuLayout(QJsonObject layout)
{
    const QString menuId = QStringLiteral("floating-menu");
    const char* targetArea = "rows";
    qsizetype targetRow = 0;
    bool foundTitle = false;
    for (const auto* field : {"rows", "bottomRows"}) {
        auto rows = layout.value(field).toArray();
        for (qsizetype row = 0; row < rows.size(); ++row) {
            auto entries = rows[row].toArray();
            for (qsizetype index = entries.size(); index-- > 0;)
                if (entries[index].toString() == menuId) entries.removeAt(index);
            if (!foundTitle && entries.contains(QStringLiteral("title"))) {
                targetArea = field; targetRow = row; foundTitle = true;
            }
            rows[row] = entries;
        }
        if (!rows.isEmpty() || layout.contains(field)) layout.insert(field, rows);
    }
    auto rows = layout.value(targetArea).toArray();
    while (rows.size() <= targetRow) rows.append(QJsonArray{});
    auto entries = rows[targetRow].toArray();
    entries.insert(0, menuId); rows[targetRow] = entries;
    layout.insert(targetArea, rows);
    return layout;
}

} // namespace

WorkspaceWindow::WorkspaceWindow(WindowManager* manager, WindowId id, const WindowOptions& options)
    : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint), manager_(manager), id_(id),
      options_(options), column_(new QVBoxLayout(this)), surface_(new DockSurface(manager, id, this)),
      placementTimer_(new QTimer(this))
{
    setObjectName("WorkspaceWindow"); setProperty("windowId", id);
    setMinimumSize(480, 320); resize(options.size);
    normalPlacement_ = geometry();
    placementTimer_->setSingleShot(true);
    connect(placementTimer_, &QTimer::timeout, this, [this] { notifyPlacementChanged(); });
    column_->setContentsMargins(1, 1, 1, 1); column_->setSpacing(0);
    column_->addWidget(surface_, 1); syncKind();
    applyTheme(this, manager_->theme());
    // Prepare the hidden HWND before its first ShowWindow, so the compositor
    // can use the same native transitions for full and detached windows.
#ifdef Q_OS_WIN
    applyNativeFrame(this);
#endif
}
WindowId WorkspaceWindow::id() const { return id_; }
DockSurface* WorkspaceWindow::surface() const { return surface_; }
CommandStripHost* WorkspaceWindow::strips() const { return strips_; }
void WorkspaceWindow::setDragPresentation(DragPresentation presentation)
{
    if (dragPresentation_ == presentation) return;
    const QPointer<WorkspaceWindow> retained(this);
    const bool wasUpdating = updatesEnabled();
    setUpdatesEnabled(false);
    const auto restoreUpdates = qScopeGuard([retained, wasUpdating] {
        if (retained) { retained->setUpdatesEnabled(wasUpdating); if (wasUpdating) retained->update(); }
    });
    QPointer<QWidget> focused = QApplication::focusWidget();
    if (focused && !surface_->isAncestorOf(focused)) focused.clear();
    clearMask();
    if (dragPresentationHost_) {
        surface_->setParent(this);
        column_->insertWidget(chrome_ ? 1 : 0, surface_, 1);
        surface_->show();
        delete dragPresentationHost_;
    }
    dragPresentation_ = presentation;
    const bool compact = presentation != DragPresentation::Window;
    const bool edge = presentation >= DragPresentation::Left;
    setProperty("windowSystemDragPresentation", compact);
    column_->setContentsMargins(compact ? 0 : 1, compact ? 0 : 1, compact ? 0 : 1, compact ? 0 : 1);
    if (chrome_) chrome_->setVisible(!compact && viewState_.effective.toolbars);
    if (bottomChrome_) bottomChrome_->setVisible(!compact && viewState_.effective.toolbars);
    if (status_) status_->setVisible(!compact && viewState_.effective.statusBar);
    surface_->setDragPresentation(compact, edge);
    if (edge) {
        column_->removeWidget(surface_);
        const auto context = manager_->context(id_);
        const auto* tab = manager_->model().tab(context.tab);
        auto* host = new EdgeDragPresentation(tab ? tab->title : windowTitle(),
            Edge(int(presentation) - int(DragPresentation::Left)), manager_->theme(), surface_, this);
        dragPresentationHost_ = host;
        column_->addWidget(host, 1); host->show(); surface_->show();
    }
    applyTheme(this, manager_->theme());
#ifdef Q_OS_WIN
    applyNativeFrame(this);
#endif
    column_->activate();
    if (dragPresentationHost_) dragPresentationHost_->layout()->activate();
    updateDragMask();
    if (focused && focused->isVisible()) focused->setFocus(Qt::OtherFocusReason);
}
QPoint WorkspaceWindow::dragLabelAnchor() const
{
    if (const auto* host = dynamic_cast<EdgeDragPresentation*>(dragPresentationHost_.data()))
        return host->mapTo(this, host->labelAnchor());
    for (auto* widget : surface_->findChildren<QWidget*>()) if (auto* group = dynamic_cast<GroupTabs*>(widget)) {
        const auto* bar = group->tabBar();
        if (bar->count() && bar->isVisible()) return bar->mapTo(this, bar->tabRect(0).center());
    }
    return {80, 13};
}
void WorkspaceWindow::updateDragMask()
{
    if (dragPresentation_ == DragPresentation::Window) return;
    QRegion visible;
    if (const auto* host = dynamic_cast<EdgeDragPresentation*>(dragPresentationHost_.data())) {
        visible = host->visibleRegion().translated(host->pos());
    } else {
        for (auto* widget : surface_->findChildren<QWidget*>()) if (auto* group = dynamic_cast<GroupTabs*>(widget); group && group->isVisible()) {
            const auto origin = group->mapTo(this, QPoint());
            const auto strip = group->viewportRectGlobal();
            const int header = strip.height();
            visible += QRect(origin + QPoint(0, header), QSize(group->width(), qMax(0, group->height() - header)));
            const auto* bar = group->tabBar();
            for (int i = 0; i < bar->count(); ++i)
                visible += QRect(bar->mapTo(this, bar->tabRect(i).topLeft()), bar->tabRect(i).size());
        }
    }
    if (visible.isEmpty()) return;
    visible &= QRegion(rect());
    if (mask() != visible) setMask(visible);
#ifdef Q_OS_WIN
    // Qt's native mask update can disable DWM non-client composition. Restore
    // the custom frame only after that update, including compact resizes.
    applyNativeFrame(this);
#endif
}
QJsonObject WorkspaceWindow::saveStripLayout() const
{
    return strips_ ? mergeRenderedStripLayout(savedStripLayout_, strips_->saveLayout()) : savedStripLayout_;
}
void WorkspaceWindow::restoreStripLayout(const QJsonObject& layout)
{
    savedStripLayout_ = layout;
    if (strips_) strips_->restoreLayout(kind_ == WindowKind::Floating ? floatingMenuLayout(layout) : layout);
}
void WorkspaceWindow::syncKind()
{
    const auto* record = manager_->model().window(id_);
    if (!record) return;
    if (chrome_ && kind_ == record->kind) { updateCaption(); return; }
    if (strips_) savedStripLayout_ = saveStripLayout();
    if (chrome_) {
        // QWidget child teardown sends events before QObject clears QPointer.
        // Stop observing the old chrome before its internal state is destroyed.
        if (strips_) for (auto* area : strips_->windowDragAreas()) area->removeEventFilter(this);
        if (caption_) caption_->removeEventFilter(this);
        chrome_->removeEventFilter(this);
        auto* oldChrome = chrome_.data();
        column_->removeWidget(oldChrome);
        strips_.clear(); caption_.clear(); maximizeButton_.clear(); chrome_.clear();
        delete oldChrome;
    }
    bottomChrome_.clear();
    if (status_) { column_->removeWidget(status_); delete status_; }
    kind_ = record->kind;
    createChrome();
    if (strips_ && !savedStripLayout_.isEmpty()) restoreStripLayout(savedStripLayout_);
    updateCaption();
}
void WorkspaceWindow::createChrome()
{
    commandActions_.clear();
    chrome_ = new QFrame(this); chrome_->setObjectName("WindowHeader");
    auto* row = new QHBoxLayout(chrome_); row->setContentsMargins(0, 0, 0, 0); row->setSpacing(0);
    chrome_->installEventFilter(this);
    strips_ = new CommandStripHost(chrome_);
    strips_->setLayoutChangedCallback([this] {
        for (auto* area : strips_->windowDragAreas()) area->installEventFilter(this);
        emit manager_->layoutChanged();
    });
    strips_->setSizingChangeRequest([this](const QString& strip, CommandStripHost::StripSizing sizing) {
        manager_->execute("window.toolbar.setSizing", {{"strip", strip},
            {"sizing", sizing == CommandStripHost::StripSizing::Elastic ? "elastic" : "content"}}, id_);
    });
    strips_->setWidthResetRequest([this](const QString& strip) {
        manager_->execute("window.toolbar.resetSize", {{"strip", strip}}, id_);
    });
    strips_->setContextMenuCustomizer([this](QMenu* menu) { addWindowMenuActions(menu); });
    auto* menu = new QMenuBar(strips_); menu->setObjectName("MenuStrip"); menu->setNativeMenuBar(false);
    const bool floating = kind_ == WindowKind::Floating;
    menus_ = new ApplicationCommandMenus(menu, manager_, id_, floating
        ? CommandMenuSurface::FloatingApplication : CommandMenuSurface::Application);
    menus_->structureChanged = [this] { if (strips_) strips_->refreshLayout(); };
    strips_->addStrip(floating ? "floating-menu" : "menu", QStringLiteral("菜单"), menu, 0);
    if (kind_ == WindowKind::Workbench) {
        QList<QAction*> toolActions;
        const QVector<QPair<QString, QString>> buttons = {{"host.newTab", "add"},
            {"workspace.splitGroup.right", "split-horizontal"}, {"pane.split.right", "split-vertical"},
            {"workspace.detachFloating", "detach"}, {"workbench.showCommandPalette", "command"},
            {"window.settings", "settings"}, {"layout.save", "save"}};
        for (const auto& item : buttons) {
            const auto* command = manager_->commands().find(item.first);
            if (!command) continue;
            auto* action = new QAction(iconFor(item.second, manager_->theme()), command->title, strips_);
            action->setObjectName(item.first); action->setData(item.first);
            action->setProperty("stripCommand", true);
            action->setToolTip(command->title + (command->shortcut.isEmpty() ? QString() : "  " + command->shortcut.toString(QKeySequence::NativeText)));
            toolActions.append(action); commandActions_.append(action);
            QObject::connect(action, &QAction::triggered, manager_, [manager = manager_, window = id_, id = item.first] {
                QTimer::singleShot(0, manager, [manager, window, id] { manager->execute(id, {}, window); });
            });
        }
        strips_->addActionStrip("workspace", QStringLiteral("工作区"), toolActions, 1);
        status_ = new QLabel(QStringLiteral("就绪  ·  窗口系统预览"), this); status_->setObjectName("StatusBar");
        status_->setContentsMargins(10, 3, 10, 3); column_->addWidget(status_);
    }
    caption_ = new WindowTitleLabel(options_.title, strips_); caption_->setObjectName("CaptionTitle");
    caption_->setProperty("windowDragArea", true);
    strips_->addStrip("title", QStringLiteral("窗口标题"), caption_, 0, CommandStripHost::StripSizing::Elastic);
    strips_->setStripOptionsEnabled("title", false);
    strips_->setStripConstraints("title", {true, true, 96, true});
    auto* controls = new QWidget(strips_);
    controls->setObjectName("CaptionControls");
    controls->setMinimumHeight(32);
    controls->setProperty("windowDragArea", true);
    auto* controlRow = new QHBoxLayout(controls); controlRow->setContentsMargins(0, 0, 0, 0); controlRow->setSpacing(0);
    controlRow->addStretch();
    auto addCaption = [&](const QString& icon, const QString& title, auto callback) {
        auto* button = new QToolButton(controls); button->setObjectName("CaptionButton");
        button->setProperty("role", icon); button->setIcon(iconFor(icon, manager_->theme()));
        button->setFixedSize(42, 32); button->setToolTip(title); button->setAccessibleName(title);
        // The strip itself is one 32-pixel row. Let its layout fill that row;
        // AlignTop would make Qt use the styled size hint (21 pixels) after
        // stylesheet polishing instead of the caption's full hit rectangle.
        controlRow->addWidget(button); QObject::connect(button, &QToolButton::clicked, this, callback);
        return button;
    };
    addCaption("minimize", QStringLiteral("最小化"), [this] { showMinimized(); });
    maximizeButton_ = addCaption("maximize", QStringLiteral("最大化"), [this] {
        if (viewState_.mode != ViewMode::Normal) manager_->setViewMode(id_, ViewMode::Normal);
        else isMaximized() ? showNormal() : showMaximized();
    });
    addCaption("close", QStringLiteral("关闭窗口"), [this] { close(); });
    strips_->addStrip("caption-controls", QStringLiteral("窗口控制"), controls, 0);
    strips_->setStripOptionsEnabled("caption-controls", false);
    strips_->setStripConstraints("caption-controls", {false, false});
    for (auto* area : strips_->windowDragAreas()) area->installEventFilter(this);
    row->addWidget(strips_, 1);
    column_->insertWidget(0, chrome_);
    bottomChrome_ = strips_->bottomArea();
    bottomChrome_->setParent(this);
    column_->insertWidget(column_->indexOf(surface_) + 1, bottomChrome_);
    bottomChrome_->installEventFilter(this);
    bottomChrome_->show();
    updateWindowControlState();
    updateCommandStates();
}
void WorkspaceWindow::updateWindowControlState()
{
    if (!maximizeButton_) return;
    const bool expanded = isMaximized() || viewState_.mode != ViewMode::Normal;
    const auto role = expanded ? QStringLiteral("restore") : QStringLiteral("maximize");
    const auto title = expanded ? QStringLiteral("还原") : QStringLiteral("最大化");
    maximizeButton_->setProperty("role", role);
    maximizeButton_->setIcon(iconFor(role, manager_->theme()));
    maximizeButton_->setToolTip(title);
    maximizeButton_->setAccessibleName(title);
}
void WorkspaceWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::WindowStateChange && !isVisible()) {
        const auto* stateEvent = static_cast<QWindowStateChangeEvent*>(event);
        constexpr auto expanded = Qt::WindowMinimized | Qt::WindowMaximized | Qt::WindowFullScreen;
        // Hidden QWidget geometry changes defer their move/resize events until
        // show. Capture that requested normal rectangle before the first show
        // in an expanded state can replace it with the monitor's geometry.
        if (!stateEvent->oldState().testAnyFlags(expanded) && windowState().testAnyFlags(expanded))
            normalPlacement_ = geometry();
    }
    QWidget::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        if (!isMinimized()) maximizedPlacement_ = isMaximized();
        else {
            const auto* stateEvent = static_cast<QWindowStateChangeEvent*>(event);
            maximizedPlacement_ = stateEvent->oldState().testFlag(Qt::WindowMaximized);
        }
        updateWindowControlState();
        queuePlacementNotification();
        if (!applyingViewState_ && viewState_.mode != ViewMode::Normal && !isFullScreen() && !isMinimized())
            QTimer::singleShot(0, this, [this] {
                if (viewState_.mode != ViewMode::Normal && !isFullScreen() && !isMinimized())
                    manager_->setViewMode(id_, ViewMode::Normal);
            });
    }
}
void WorkspaceWindow::addWindowMenuActions(QMenu* menu)
{
    populateContextMenu(menu, CommandMenuSurface::Window, manager_, manager_->context(id_));
    for (auto* action : menu->findChildren<QAction*>()) if (!action->property("windowSystemCommand").toString().isEmpty())
        connect(action, &QAction::triggered, manager_, [manager = manager_, window = id_, id = action->data().toString()] {
            QTimer::singleShot(0, manager, [manager, window, id] { manager->execute(id, {}, window); });
        });
}
void WorkspaceWindow::updateCommandStates()
{
    if (menus_) menus_->refresh();
    const auto context = manager_->context(id_);
    for (const auto& action : commandActions_) if (action) refreshCommandAction(manager_, action, context);
}
void WorkspaceWindow::updateCaption()
{
    const auto ctx = manager_->context(id_);
    const auto* tab = manager_->model().tab(ctx.tab);
    const auto title = (tab ? tab->title + QStringLiteral(" — ") : QString()) + options_.title;
    setWindowTitle(title + (kind_ == WindowKind::Floating ? QStringLiteral(" · 浮动窗口") : QString()));
    if (caption_) {
        const auto caption = kind_ == WindowKind::Floating ? windowTitle() : options_.title;
        caption_->setText(caption);
        caption_->setAccessibleName(caption);
        caption_->setToolTip(caption);
    }
    updateCommandStates();
    if (strips_) strips_->refreshLayout();
}
void WorkspaceWindow::setOptions(const WindowOptions& options) { options_ = options; }
void WorkspaceWindow::setStatus(const QString& text) { if (status_) status_->setText(text); }
QRect WorkspaceWindow::normalPlacementGeometry() const
{
    // Qt's normalGeometry subtracts the current maximized frame margins from
    // WINDOWPLACEMENT, although this window has no margins in normal state.
    // Keep the logical normal rectangle independently of the native frame.
    return hasNormalPlacement() ? geometry() : normalPlacement_;
}
bool WorkspaceWindow::hasNormalPlacement() const
{
    constexpr auto expanded = Qt::WindowMinimized | Qt::WindowMaximized | Qt::WindowFullScreen;
    if (windowState().testAnyFlags(expanded)
        || (windowHandle() && windowHandle()->windowStates().testAnyFlags(expanded))) return false;
#ifdef Q_OS_WIN
    return hasNormalNativePlacement(this);
#else
    return true;
#endif
}
bool WorkspaceWindow::placementMaximized() const
{
    if (viewState_.mode != ViewMode::Normal) return viewNormalMaximized_;
    return isMinimized() ? maximizedPlacement_ : isMaximized();
}
void WorkspaceWindow::restorePlacement(const QRect& geometry, bool maximized)
{
    restoringPlacement_ = true;
    placementTimer_->stop();
    if (isMaximized() || isMinimized() || isFullScreen()) showNormal();
    setGeometry(geometry);
    normalPlacement_ = this->geometry();
    maximizedPlacement_ = maximized;
    maximized ? showMaximized() : showNormal();
    notifiedPlacement_ = normalPlacementGeometry();
    notifiedMaximized_ = placementMaximized();
    restoringPlacement_ = false;
}
void WorkspaceWindow::queuePlacementNotification()
{
    if (!restoringPlacement_ && hasNormalPlacement()) normalPlacement_ = geometry();
    if (!isVisible() || restoringPlacement_ || manager_->d->destroying) return;
    if (!placementTimer_->isActive()) placementTimer_->start(0);
}
void WorkspaceWindow::notifyPlacementChanged()
{
    if (!isVisible() || restoringPlacement_ || manager_->d->destroying ||
        !manager_->model().window(id_)) return;
    // Drag previews and native state changes may deliver several geometries in
    // one event turn. Publish only the final placement after the transaction.
    if (manager_->d->refreshing || manager_->d->drag->isActive()) {
        placementTimer_->start(50); return;
    }
    const auto placement = normalPlacementGeometry();
    const bool maximized = placementMaximized();
    if (notifiedPlacement_ == placement && notifiedMaximized_ == maximized) return;
    normalPlacement_ = placement;
    notifiedPlacement_ = placement;
    notifiedMaximized_ = maximized;
    emit manager_->layoutChanged();
}
void WorkspaceWindow::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event); queuePlacementNotification();
}
void WorkspaceWindow::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event); queuePlacementNotification(); updateDragMask();
}
void WorkspaceWindow::closeEvent(QCloseEvent* event)
{
    if (allowClose) { event->accept(); return; }
    event->ignore(); manager_->requestCloseWindow(id_);
}
bool WorkspaceWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (strips_ && strips_->isDragging()) return QWidget::eventFilter(watched, event);
    if (strips_ && event->type() == QEvent::ContextMenu) {
        auto* context = static_cast<QContextMenuEvent*>(event);
        QPointer<QMenu> menu = strips_->createContextMenu(this);
        menu->exec(context->globalPos()); delete menu;
        return true;
    }
    if (event->type() == QEvent::MouseButtonDblClick) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            if (viewState_.mode != ViewMode::Normal) manager_->setViewMode(id_, ViewMode::Normal);
            else isMaximized() ? showNormal() : showMaximized();
            return true;
        }
    }
    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            press_ = mouse->globalPosition().toPoint(); pendingTitleDrag_ = true;
            if (kind_ == WindowKind::Workbench && windowHandle()) { pendingTitleDrag_ = false; windowHandle()->startSystemMove(); }
        } else if (mouse->button() == Qt::RightButton) {
            if (!strips_) { manager_->showWindowMenu(id_, mouse->globalPosition().toPoint()); return true; }
        }
    }
    if (event->type() == QEvent::MouseMove && pendingTitleDrag_) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->buttons().testFlag(Qt::LeftButton) && (mouse->globalPosition().toPoint() - press_).manhattanLength() >= QApplication::startDragDistance()) {
            pendingTitleDrag_ = false; manager_->beginWindowDrag(id_, press_); return true;
        }
    }
    if (event->type() == QEvent::MouseButtonRelease) pendingTitleDrag_ = false;
    return QWidget::eventFilter(watched, event);
}
void WorkspaceWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
#ifdef Q_OS_WIN
    applyNativeFrame(this);
#endif
    queuePlacementNotification();
}
bool WorkspaceWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
#ifdef Q_OS_WIN
    if (handleNativeFrame(this, message, result)) return true;
#endif
    return QWidget::nativeEvent(eventType, message, result);
}
} // namespace window_system
