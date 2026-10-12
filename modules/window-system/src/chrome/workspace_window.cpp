// SPDX-License-Identifier: MIT
#include "workspace_window.hpp"
#include "command_strip.hpp"
#include "window_title_label.hpp"
#include "docking/dock_surface.hpp"
#include "platform/windows/window_frame.hpp"
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
#include <QSet>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

namespace window_system {
namespace {

QJsonObject mergeRenderedStripLayout(const QJsonObject& cached, const QJsonObject& rendered)
{
    if (cached.isEmpty()) return rendered;
    const auto cachedRows = cached.value("rows").toArray();
    const auto renderedRows = rendered.value("rows").toArray();
    if (cachedRows.size() != 2 || renderedRows.size() != 2) return rendered;
    QSet<QString> present;
    auto settings = cached.value("strips").toObject();
    const auto renderedSettings = rendered.value("strips").toObject();
    for (auto entry = renderedSettings.begin(); entry != renderedSettings.end(); ++entry) {
        present.insert(entry.key()); settings.insert(entry.key(), entry.value());
    }
    QJsonArray rows;
    for (int row = 0; row < 2; ++row) {
        const auto current = renderedRows[row].toArray();
        QJsonArray merged;
        qsizetype next = 0;
        // Reuse the shared strips' slots while leaving absent Workbench strips
        // in place. The floating chrome never retains their live widgets.
        for (const auto& entry : cachedRows[row].toArray()) {
            if (!present.contains(entry.toString())) merged.append(entry);
            else if (next < current.size()) merged.append(current[next++]);
        }
        while (next < current.size()) merged.append(current[next++]);
        rows.append(merged);
    }
    auto result = cached;
    result.insert("locked", rendered.value("locked"));
    result.insert("rows", rows);
    result.insert("strips", settings);
    return result;
}

} // namespace

WorkspaceWindow::WorkspaceWindow(WindowManager* manager, WindowId id, const WindowOptions& options)
    : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint), manager_(manager), id_(id),
      options_(options), column_(new QVBoxLayout(this)), surface_(new DockSurface(manager, id, this))
{
    setObjectName("WorkspaceWindow"); setProperty("windowId", id);
    setMinimumSize(480, 320); resize(options.size);
    column_->setContentsMargins(1, 1, 1, 1); column_->setSpacing(0);
    column_->addWidget(surface_, 1); syncKind();
    applyTheme(this, manager_->theme());
}
WindowId WorkspaceWindow::id() const { return id_; }
DockSurface* WorkspaceWindow::surface() const { return surface_; }
CommandStripHost* WorkspaceWindow::strips() const { return strips_; }
QJsonObject WorkspaceWindow::saveStripLayout() const
{
    return strips_ ? mergeRenderedStripLayout(savedStripLayout_, strips_->saveLayout()) : savedStripLayout_;
}
void WorkspaceWindow::restoreStripLayout(const QJsonObject& layout)
{
    savedStripLayout_ = layout;
    if (strips_) strips_->restoreLayout(layout);
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
    if (status_) { column_->removeWidget(status_); delete status_; }
    kind_ = record->kind;
    createChrome();
    if (strips_ && !savedStripLayout_.isEmpty()) strips_->restoreLayout(savedStripLayout_);
    updateCaption();
}
void WorkspaceWindow::createChrome()
{
    commandActions_.clear();
    chrome_ = new QFrame(this); chrome_->setObjectName("WindowHeader");
    auto* row = new QHBoxLayout(chrome_); row->setContentsMargins(0, 0, 0, 0); row->setSpacing(0);
    chrome_->installEventFilter(this);
    strips_ = new CommandStripHost(chrome_);
    strips_->setLayoutChangedCallback([this] { emit manager_->layoutChanged(); });
    strips_->setSizingChangeRequest([this](const QString& strip, CommandStripHost::StripSizing sizing) {
        manager_->execute("window.toolbar.setSizing", {{"strip", strip},
            {"sizing", sizing == CommandStripHost::StripSizing::Elastic ? "elastic" : "content"}}, id_);
    });
    strips_->setWidthResetRequest([this](const QString& strip) {
        manager_->execute("window.toolbar.resetSize", {{"strip", strip}}, id_);
    });
    strips_->setContextMenuCustomizer([this](QMenu* menu) { addWindowMenuActions(menu); });
    if (kind_ == WindowKind::Workbench) {
        auto* menu = new QMenuBar(strips_); menu->setObjectName("MenuStrip"); menu->setNativeMenuBar(false);
        const QVector<QPair<QString, QString>> categories = {
            {QStringLiteral("文件(&F)"), "文件"}, {QStringLiteral("视图(&V)"), "视图"},
            {QStringLiteral("布局(&L)"), "布局"}, {QStringLiteral("窗口(&W)"), "窗口"},
            {QStringLiteral("工具(&T)"), "工具"}, {QStringLiteral("帮助(&H)"), "帮助"}};
        for (const auto& category : categories) {
            auto* entry = menu->addMenu(category.first);
            entry->menuAction()->setObjectName(category.second);
            for (const auto& command : manager_->commands().commands()) {
                if (command.category != category.second) continue;
                auto* action = entry->addAction(command.title);
                action->setData(command.id);
                action->setObjectName(command.id);
                commandActions_.append(action);
                if (!command.shortcut.isEmpty()) action->setText(command.title + "\t" + command.shortcut.toString(QKeySequence::NativeText));
                const auto id = command.id;
                QObject::connect(action, &QAction::triggered, manager_, [manager = manager_, window = id_, id] {
                    QTimer::singleShot(0, manager, [manager, window, id] { manager->execute(id, {}, window); });
                });
            }
            QObject::connect(entry, &QMenu::aboutToShow, this, [this] { updateCommandStates(); });
        }
        strips_->addStrip("menu", QStringLiteral("菜单"), menu, 0);
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
    strips_->setStripConstraints("title", {true, true, 96});
    auto* controls = new QWidget(strips_);
    controls->setObjectName("CaptionControls");
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
    maximizeButton_ = addCaption("maximize", QStringLiteral("最大化"), [this] { isMaximized() ? showNormal() : showMaximized(); });
    addCaption("close", QStringLiteral("关闭窗口"), [this] { close(); });
    strips_->addStrip("caption-controls", QStringLiteral("窗口控制"), controls, 0);
    strips_->setStripOptionsEnabled("caption-controls", false);
    strips_->setStripConstraints("caption-controls", {false, false});
    for (auto* area : strips_->windowDragAreas()) area->installEventFilter(this);
    row->addWidget(strips_, 1);
    column_->insertWidget(0, chrome_);
    updateWindowControlState();
    updateCommandStates();
}
void WorkspaceWindow::updateWindowControlState()
{
    if (!maximizeButton_) return;
    const auto role = isMaximized() ? QStringLiteral("restore") : QStringLiteral("maximize");
    const auto title = isMaximized() ? QStringLiteral("还原") : QStringLiteral("最大化");
    maximizeButton_->setProperty("role", role);
    maximizeButton_->setIcon(iconFor(role, manager_->theme()));
    maximizeButton_->setToolTip(title);
    maximizeButton_->setAccessibleName(title);
}
void WorkspaceWindow::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) updateWindowControlState();
}
void WorkspaceWindow::addWindowMenuActions(QMenu* menu)
{
    const QStringList ids = kind_ == WindowKind::Workbench
        ? QStringList{"window.demote", "window.moveAllTabs", "window.select", "window.settings", "window.close"}
        : QStringList{"window.promote", "window.reassignFamily", "window.redock", "window.moveAllTabs", "window.select", "window.settings", "window.close"};
    for (const auto& id : ids) {
        const auto* command = manager_->commands().find(id);
        if (!command) continue;
        const auto context = manager_->context(id_);
        // Window-kind/family operations are absent when there is no valid
        // target. Existing content operations retain their disabled state.
        if ((id == "window.demote" || id == "window.redock" || id == "window.reassignFamily") &&
            command->enabled && !command->enabled(context)) continue;
        auto* action = menu->addAction(command->title);
        action->setData(id);
        action->setEnabled(!command->enabled || command->enabled(context));
        connect(action, &QAction::triggered, manager_, [manager = manager_, window = id_, id] {
            QTimer::singleShot(0, manager, [manager, window, id] { manager->execute(id, {}, window); });
        });
    }
}
void WorkspaceWindow::updateCommandStates()
{
    const auto context = manager_->context(id_);
    const auto* group = manager_->model().group(context.group);
    for (const auto& action : commandActions_) {
        if (!action) continue;
        const auto id = action->data().toString();
        const auto* command = manager_->commands().find(id);
        const bool enabled = command && (!command->enabled || command->enabled(context));
        if (command) {
            const auto shortcut = command->shortcut.toString(QKeySequence::NativeText);
            action->setText(command->title + (!action->property("stripCommand").toBool() && !shortcut.isEmpty() ? "\t" + shortcut : QString()));
            action->setToolTip(command->title + (shortcut.isEmpty() ? QString() : "  " + shortcut));
        }
        bool visible = true;
        if (id == "window.promote") visible = kind_ == WindowKind::Floating;
        if (id == "window.redock" || id == "window.reassignFamily") visible = kind_ == WindowKind::Floating && enabled;
        if (id == "window.demote") visible = kind_ == WindowKind::Workbench && enabled;
        if (id.startsWith("window.drag.")) visible = enabled;
        if (id == "workspace.pinTab" || id == "workspace.pinGroup") visible = group && group->location == GroupLocation::AutoHide;
        if (id == "workspace.unpinTab" || id == "workspace.unpinGroup") visible = group && group->location == GroupLocation::Pinned;
        if (id == "workspace.pinEdge" || id == "workspace.unpinEdge" || id == "workspace.restoreGroupToCenter")
            visible = group && group->location != GroupLocation::Central;
        action->setVisible(visible); action->setEnabled(enabled);
    }
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
        if (mouse->button() == Qt::LeftButton) { isMaximized() ? showNormal() : showMaximized(); return true; }
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
}
bool WorkspaceWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
#ifdef Q_OS_WIN
    if (handleNativeFrame(this, message, result)) return true;
#endif
    return QWidget::nativeEvent(eventType, message, result);
}
} // namespace window_system
