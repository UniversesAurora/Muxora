// SPDX-License-Identifier: MIT
#include "workspace_window.hpp"
#include "command_strip.hpp"
#include "window_title_label.hpp"
#include "status_bar.hpp"
#include "commands/command_menus.hpp"
#include "commands/popup_menu.hpp"
#include "docking/dock_surface.hpp"
#include "docking/drag_presentation.hpp"
#include "docking/tab_strip.hpp"
#include "platform/windows/window_frame.hpp"
#include "platform/window_placement.hpp"
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
#include <QScreen>
#include <QSet>
#include <QScopeGuard>
#include <QStyle>
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

QJsonObject mergeToolbarEdit(QJsonObject latest, const QJsonObject& before, const QJsonObject& after)
{
    if (latest.isEmpty()) return after;
    // A row transfer changes both areas as one operation. Unrelated edits in
    // an older live window must retain the newer profile's ordering.
    if (before.value("rows") != after.value("rows") || before.value("bottomRows") != after.value("bottomRows")) {
        for (const auto* field : {"rows", "bottomRows"}) {
            if (after.contains(field)) latest.insert(field, after.value(field));
            else latest.remove(field);
        }
    }
    if (before.value("locked") != after.value("locked")) latest.insert("locked", after.value("locked"));
    auto settings = latest.value("strips").toObject();
    const auto oldSettings = before.value("strips").toObject();
    const auto newSettings = after.value("strips").toObject();
    for (auto strip = newSettings.begin(); strip != newSettings.end(); ++strip) {
        const auto oldFields = oldSettings.value(strip.key()).toObject();
        const auto newFields = strip->toObject();
        auto merged = settings.value(strip.key()).toObject();
        QSet<QString> fields;
        for (auto field = oldFields.begin(); field != oldFields.end(); ++field) fields.insert(field.key());
        for (auto field = newFields.begin(); field != newFields.end(); ++field) fields.insert(field.key());
        for (const auto& field : fields) if (oldFields.value(field) != newFields.value(field)) {
            if (field == "excluded") {
                auto excluded = merged.value(field).toArray();
                const auto oldExcluded = oldFields.value(field).toArray(), newExcluded = newFields.value(field).toArray();
                for (const auto& id : oldExcluded) if (!newExcluded.contains(id))
                    for (qsizetype index = excluded.size(); index-- > 0;) if (excluded[index] == id) excluded.removeAt(index);
                for (const auto& id : newExcluded) if (!oldExcluded.contains(id) && !excluded.contains(id)) excluded.append(id);
                merged.insert(field, excluded); continue;
            }
            if (newFields.contains(field)) merged.insert(field, newFields.value(field));
            else merged.remove(field);
        }
        if (!merged.isEmpty()) settings.insert(strip.key(), merged);
    }
    latest.insert("strips", settings);
    return latest;
}

} // namespace

WorkspaceWindow::WorkspaceWindow(WindowManager* manager, WindowId id, const WindowOptions& options)
    : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint | Qt::WindowMinMaxButtonsHint
        | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint), manager_(manager), id_(id),
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
    const bool wasChangingPresentation = changingDragPresentation_;
    changingDragPresentation_ = true;
    const auto finishPresentation = qScopeGuard([retained, wasChangingPresentation] {
        if (retained) retained->changingDragPresentation_ = wasChangingPresentation;
    });
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
    updateFrameMargins();
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
        if(tab) {
            const auto appearance=manager_->tabAppearance();
            const auto* group=manager_->model().group(context.group);
            host->configureLabel(manager_->tabColorPreset(tab->colorId),
                appearance.showSidebarIcons && manager_->tabOptions(context.tab).showIcon ? iconFor("window",manager_->theme()) : QIcon{},
                appearance.showSidebarNumbers && group ? group->tabs.indexOf(context.tab)+1 : 0);
        }
        dragPresentationHost_ = host;
        column_->addWidget(host, 1); host->show(); surface_->show();
    }
    applyTheme(this, manager_->theme());
#ifdef Q_OS_WIN
    applyNativeFrame(this);
#endif
    column_->activate();
    if (dragPresentationHost_) dragPresentationHost_->layout()->activate();
    changingDragPresentation_ = false;
    updateDragMask();
    if (!retained) return;
#ifdef Q_OS_WIN
    if (!compact && windowHandle()) {
        // Reassert the empty QWindow mask after layout/native frame updates.
        // QWidget's empty cache alone does not prove the HWND region is clear.
        windowHandle()->setMask({});
        if (!retained) return;
        applyNativeFrame(this);
    }
#endif
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
    if (changingDragPresentation_) return;
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
    const QPointer<WorkspaceWindow> retained(this);
    if (mask() != visible) setMask(visible);
    if (!retained) return;
#ifdef Q_OS_WIN
    // QWidget caches the shape before calling the platform, whereas QWindow
    // publishes its cache after SetWindowRgn's synchronous placement messages.
    // Commit the final shape through QWindow as well: a callback replaying its
    // old mask must not leave only the QWidget cache shaped at transaction end.
    if (windowHandle()) windowHandle()->setMask(visible);
    if (!retained) return;
    // Qt's native mask update can disable DWM non-client composition. Restore
    // the custom frame only after that update, including compact resizes.
    applyNativeFrame(this);
#endif
}
QJsonObject WorkspaceWindow::saveStripLayout() const
{
    return strips_ ? mergeRenderedStripLayout(savedStripLayout_, strips_->saveLayout()) : savedStripLayout_;
}
QJsonObject WorkspaceWindow::saveInactiveStripLayouts() const
{
    QJsonObject result;
    for (const auto kind : {WindowKind::Workbench, WindowKind::Floating})
        if (kind != kind_ && savedStripLayouts_.contains(kind))
            result.insert(kind == WindowKind::Workbench ? "workbench" : "floating", savedStripLayouts_.value(kind));
    return result;
}
void WorkspaceWindow::restoreStripLayout(const QJsonObject& layout)
{
    savedStripLayout_ = layout;
    savedStripLayouts_.insert(kind_, layout);
    if (strips_) strips_->restoreLayout(layout);
    committedStripLayout_ = saveStripLayout();
}
void WorkspaceWindow::restoreStripLayouts(const QJsonObject& inactive, const QJsonObject& current)
{
    savedStripLayouts_.clear();
    for (const auto kind : {WindowKind::Workbench, WindowKind::Floating}) {
        const auto key = kind == WindowKind::Workbench ? "workbench" : "floating";
        if (inactive.contains(key)) savedStripLayouts_.insert(kind, inactive.value(key).toObject());
    }
    restoreStripLayout(current);
}
void WorkspaceWindow::syncKind()
{
    const auto* record = manager_->model().window(id_);
    if (!record) return;
    if (chrome_ && kind_ == record->kind) { updateCaption(); return; }
    if (strips_) savedStripLayouts_.insert(kind_, saveStripLayout());
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
    savedStripLayout_ = savedStripLayouts_.value(kind_, manager_->d->toolbarProfiles.value(kind_));
    createChrome();
    if (strips_ && !savedStripLayout_.isEmpty()) restoreStripLayout(savedStripLayout_);
    updateCaption();
    committedStripLayout_ = saveStripLayout();
}
void WorkspaceWindow::createChrome()
{
    commandActions_.clear();
    chrome_ = new QFrame(this); chrome_->setObjectName("WindowHeader");
    auto* row = new QHBoxLayout(chrome_); row->setContentsMargins(0, 0, 0, 0); row->setSpacing(0);
    chrome_->installEventFilter(this);
    strips_ = new CommandStripHost(chrome_);
    strips_->setLayoutChangedCallback([this] {
        if (!manager_) return;
        for (auto* area : strips_->windowDragAreas()) area->installEventFilter(this);
        const auto committed = saveStripLayout();
        savedStripLayouts_.insert(kind_, committed);
        manager_->d->toolbarProfiles.insert(kind_, mergeToolbarEdit(
            manager_->d->toolbarProfiles.value(kind_), committedStripLayout_, committed));
        committedStripLayout_ = committed;
        emit manager_->layoutChanged();
    });
    strips_->setSizingChangeRequest([this](const QString& strip, CommandStripHost::StripSizing sizing) {
        if (!manager_) return;
        manager_->execute("window.toolbar.setSizing", {{"strip", strip},
            {"sizing", sizing == CommandStripHost::StripSizing::Elastic ? "elastic" : "content"}}, id_);
    });
    strips_->setWidthResetRequest([this](const QString& strip) {
        if (!manager_) return;
        manager_->execute("window.toolbar.resetSize", {{"strip", strip}}, id_);
    });
    strips_->setContextMenuCustomizer([this](QMenu* menu) { addWindowMenuActions(menu); });
    auto* menu = new QMenuBar(strips_); menu->setObjectName("MenuStrip"); menu->setNativeMenuBar(false);
    menu->installEventFilter(this);
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
        auto* status = new WorkspaceStatusBar(manager_, id_, this);
        status->setHiddenItems(options_.hiddenStatusItems);
        status_ = status; column_->addWidget(status_);
    }
    caption_ = new WindowTitleLabel(options_.title, strips_); caption_->setObjectName("CaptionTitle");
    caption_->setProperty("windowDragArea", true);
    strips_->addStrip("title", QStringLiteral("窗口标题"), caption_, 0, CommandStripHost::StripSizing::Elastic);
    if (auto* strip = strips_->stripWidget("title"))
        if (auto* handle = strip->findChild<QWidget*>("StripHandle")) handle->setToolTip({});
    strips_->setStripOptionsEnabled("title", false);
    strips_->setStripConstraints("title", {true, true, titleMinimumWidth, true});
    auto* controls = new QWidget(strips_);
    controls->setObjectName("CaptionControls");
    controls->setMinimumHeight(32);
    controls->setProperty("windowDragArea", true);
    auto* controlRow = new QHBoxLayout(controls); controlRow->setContentsMargins(0, 0, 0, 0); controlRow->setSpacing(0);
    controlRow->addStretch();
    auto addCaption = [&](const QString& icon, const QString& title, auto callback) {
        auto* button = new QToolButton(controls); button->setObjectName("CaptionButton");
        button->setProperty("role", icon); button->setIcon(iconFor(icon, manager_->theme()));
        button->setFixedSize(captionButtonWidth, 32); button->setToolTip(title); button->setAccessibleName(title);
        // The strip itself is one 32-pixel row. Let its layout fill that row;
        // AlignTop would make Qt use the styled size hint (21 pixels) after
        // stylesheet polishing instead of the caption's full hit rectangle.
        controlRow->addWidget(button); QObject::connect(button, &QToolButton::clicked, this, callback);
        return button;
    };
    addCaption("minimize", QStringLiteral("最小化"), [this] { showMinimized(); });
    maximizeButton_ = addCaption("maximize", QStringLiteral("最大化"), [this] {
        if (!manager_) return;
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
    if (!manager_ || !maximizeButton_) return;
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
        updateFrameMargins();
#ifdef Q_OS_WIN
        applyNativeFrame(this);
#endif
        if (!isMinimized()) maximizedPlacement_ = isMaximized();
        else {
            const auto* stateEvent = static_cast<QWindowStateChangeEvent*>(event);
            maximizedPlacement_ = stateEvent->oldState().testFlag(Qt::WindowMaximized);
        }
        updateWindowControlState();
        queuePlacementNotification();
        if (!applyingViewState_ && viewState_.mode != ViewMode::Normal && !isFullScreen() && !isMinimized())
            QTimer::singleShot(0, this, [this] {
                if (manager_ && viewState_.mode != ViewMode::Normal && !isFullScreen() && !isMinimized())
                    manager_->setViewMode(id_, ViewMode::Normal);
            });
    }
}
void WorkspaceWindow::updateFrameMargins()
{
    const bool expanded = isMaximized() || isFullScreen() || viewState_.mode != ViewMode::Normal;
    const bool frameless = expanded || dragPresentation_ != DragPresentation::Window;
    const int margin = frameless ? 0 : 1;
    column_->setContentsMargins(margin, margin, margin, margin);
    if (property("windowSystemExpanded").toBool() != expanded) {
        setProperty("windowSystemExpanded", expanded);
        style()->unpolish(this); style()->polish(this); update();
    }
}
void WorkspaceWindow::showTabPresentationMenu(const QPoint& global)
{
    if (!manager_) return;
    const auto captured = manager_->context(id_);
    const auto appearance = manager_->tabAppearance();
    auto* menu = new DismissOnlyMenu(this);
    menu->setObjectName(QStringLiteral("TabPresentationMenu"));
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const QPointer<QMenu> retained(menu);
    const auto add = [retained, owner = manager_, captured](const QString& id, const QString& title,
                                                      bool checked, bool toggle) {
        if (!retained || !owner) return false;
        const auto* definition = owner->commands().find(id);
        const auto enabled = definition ? definition->enabled : std::function<bool(const CommandContext&)>{};
        const bool available = definition && (!enabled || enabled(captured));
        if (!retained || !owner) return false;
        auto* action = retained->addAction(title); action->setObjectName(id); action->setData(id);
        action->setCheckable(toggle); if (toggle) action->setChecked(checked);
        action->setEnabled(available);
        QObject::connect(action, &QAction::triggered, owner, [owner, id, captured] {
            if (owner) QTimer::singleShot(0, owner, [owner, id, captured] {
                if (owner) owner->executeInContext(id, captured);
            });
        });
        return true;
    };
    if (!add(QStringLiteral("presentation.toggleIcons"), QStringLiteral("显示标签图标"), appearance.showIcons, true)) return;
    if (!add(QStringLiteral("presentation.toggleNewTabButton"), QStringLiteral("显示新建标签按钮"), appearance.showNewTabButton, true)) return;
    if (!add(QStringLiteral("presentation.toggleNumbers"), QStringLiteral("显示标签编号"), appearance.showNumbers, true)) return;
    menu->addSeparator();
    if (!add(QStringLiteral("group.close"), QStringLiteral("关闭此标签组"), false, false)) return;
    menu->popup(global);
}
void WorkspaceWindow::addWindowMenuActions(QMenu* menu)
{
    if (!manager_) return;
    populateContextMenu(menu, CommandMenuSurface::Window, manager_, manager_->context(id_));
    for (auto* action : menu->findChildren<QAction*>()) if (!action->property("windowSystemCommand").toString().isEmpty())
        connect(action, &QAction::triggered, manager_, [manager = manager_, window = id_, id = action->data().toString()] {
            QTimer::singleShot(0, manager, [manager, window, id] { manager->execute(id, {}, window); });
        });
}
void WorkspaceWindow::updateCommandStates()
{
    if (!manager_) return;
    if (menus_) menus_->refresh();
    const auto context = manager_->context(id_);
    for (const auto& action : commandActions_) if (action) refreshCommandAction(manager_, action, context);
}
void WorkspaceWindow::updateCaption()
{
    if (!manager_) return;
    const auto ctx = manager_->context(id_);
    const auto* tab = manager_->model().tab(ctx.tab);
    const auto title = (tab ? tab->title + QStringLiteral(" — ") : QString()) + options_.title;
    setWindowTitle(title + (kind_ == WindowKind::Floating ? QStringLiteral(" · 浮动窗口") : QString()));
    if (status_) static_cast<WorkspaceStatusBar*>(status_.data())->setWorkspace(
        tab ? tab->title : QString(), manager_->model().orderedTabs(id_).size());
    if (caption_) {
        const auto caption = kind_ == WindowKind::Floating ? windowTitle() : options_.title;
        caption_->setText(caption);
        caption_->setAccessibleName(caption);
        caption_->setToolTip({});
    }
    updateCommandStates();
    if (strips_) strips_->refreshLayout();
}
void WorkspaceWindow::setOptions(const WindowOptions& options)
{
    options_ = options;
    if (status_) static_cast<WorkspaceStatusBar*>(status_.data())->setHiddenItems(options.hiddenStatusItems);
}
void WorkspaceWindow::setStatus(const QString& text)
{
    if (status_) static_cast<WorkspaceStatusBar*>(status_.data())->setMessage(text);
}
QStringList WorkspaceWindow::statusItemIds() { return WorkspaceStatusBar::itemIds(); }
bool WorkspaceWindow::statusItemVisible(const QString& id) const
{
    return statusItemIds().contains(id) && !options_.hiddenStatusItems.contains(id);
}
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
    initialPlacementChecked_ = true;
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
    if (!manager_) return;
    if (!restoringPlacement_ && hasNormalPlacement()) normalPlacement_ = geometry();
    if (!isVisible() || restoringPlacement_ || manager_->d->destroying) return;
    if (!placementTimer_->isActive()) placementTimer_->start(0);
}
void WorkspaceWindow::notifyPlacementChanged()
{
    if (!manager_) return;
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
    if (!manager_) return QWidget::eventFilter(watched, event);
    if (qobject_cast<QMenuBar*>(watched)) {
        if (event->type() == QEvent::ContextMenu) {
            auto* context = static_cast<QContextMenuEvent*>(event);
            showTabPresentationMenu(context->globalPos()); context->accept(); return true;
        }
        return QWidget::eventFilter(watched, event);
    }
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
void WorkspaceWindow::fitInitialPlacement()
{
    if (initialPlacementChecked_) return;
    // Restored layouts have already been normalized against the complete
    // desktop. Drag carriers deliberately follow the pointer across screens.
    if (kind_ != WindowKind::Workbench || restoringPlacement_
        || dragPresentation_ != DragPresentation::Window) { initialPlacementChecked_ = true; return; }
    if (!hasNormalPlacement() || !screen()) return;
    initialPlacementChecked_ = true;
    const auto available = screen()->availableGeometry();
    // The custom frame's NCCALCSIZE makes the entire native window a client
    // area, so its complete logical rectangle is the client rectangle.
    const auto original = geometry();
    const QSize fittingMinimum(std::min(minimumWidth(), available.width()),
                               std::min(minimumHeight(), available.height()));
    if (minimumSize() != fittingMinimum) setMinimumSize(fittingMinimum.expandedTo(QSize(1, 1)));
    const auto fitted = normalizeWindowPlacement(original, {available}, minimumSize());
    if (fitted != original) setGeometry(fitted);
}
void WorkspaceWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    updateFrameMargins();
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
