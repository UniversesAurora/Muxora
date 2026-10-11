#include "workspace_window.hpp"
#include "command_strip.hpp"
#include "docking/dock_surface.hpp"
#include "platform/windows/window_frame.hpp"
#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

namespace window_system {
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
void WorkspaceWindow::syncKind()
{
    const auto* record = manager_->model().window(id_);
    if (!record) return;
    if (chrome_ && kind_ == record->kind) { updateCaption(); return; }
    QJsonObject saved;
    if (strips_) saved = strips_->saveLayout();
    if (chrome_) { column_->removeWidget(chrome_); delete chrome_; }
    if (status_) { column_->removeWidget(status_); delete status_; }
    kind_ = record->kind;
    createChrome();
    if (strips_ && !saved.isEmpty()) strips_->restoreLayout(saved);
    updateCaption();
}
void WorkspaceWindow::createChrome()
{
    chrome_ = new QFrame(this); chrome_->setObjectName("WindowHeader");
    auto* row = new QHBoxLayout(chrome_); row->setContentsMargins(8, 0, 0, 0); row->setSpacing(6);
    chrome_->installEventFilter(this);
    if (kind_ == WindowKind::Workbench) {
        strips_ = new CommandStripHost(chrome_);
        auto* menu = new QMenuBar(strips_); menu->setObjectName("MenuStrip"); menu->setNativeMenuBar(false);
        const QVector<QPair<QString, QString>> categories = {
            {QStringLiteral("文件(&F)"), "文件"}, {QStringLiteral("视图(&V)"), "视图"},
            {QStringLiteral("布局(&L)"), "布局"}, {QStringLiteral("窗口(&W)"), "窗口"},
            {QStringLiteral("工具(&T)"), "工具"}, {QStringLiteral("帮助(&H)"), "帮助"}};
        for (const auto& category : categories) {
            auto* entry = menu->addMenu(category.first);
            for (const auto& command : manager_->commands().commands()) {
                if (command.category != category.second) continue;
                auto* action = entry->addAction(command.title);
                action->setData(command.id);
                if (!command.shortcut.isEmpty()) action->setText(command.title + "\t" + command.shortcut.toString(QKeySequence::NativeText));
                const auto id = command.id;
                QObject::connect(action, &QAction::triggered, manager_, [this, id] { manager_->execute(id, {}, id_); });
            }
            QObject::connect(entry, &QMenu::aboutToShow, manager_, [this, entry] {
                for (auto* action : entry->actions()) {
                    const auto* command = manager_->commands().find(action->data().toString());
                    action->setEnabled(command && (!command->enabled || command->enabled(manager_->context(id_))));
                }
            });
        }
        strips_->addStrip("menu", menu, 0);
        auto* tools = new QWidget(strips_);
        auto* toolbar = new QHBoxLayout(tools); toolbar->setContentsMargins(0, 0, 0, 0); toolbar->setSpacing(2);
        const QVector<QPair<QString, QString>> buttons = {{"host.newTab", "add"},
            {"workspace.splitGroup.right", "split-horizontal"}, {"pane.split.right", "split-vertical"},
            {"workspace.detachFloating", "detach"}, {"workbench.showCommandPalette", "command"},
            {"window.settings", "settings"}, {"layout.save", "save"}};
        for (const auto& item : buttons) {
            const auto* command = manager_->commands().find(item.first);
            if (!command) continue;
            auto* button = new QToolButton(tools); button->setIcon(iconFor(item.second, manager_->theme()));
            button->setToolTip(command->title); button->setAccessibleName(command->title); toolbar->addWidget(button);
            QObject::connect(button, &QToolButton::clicked, manager_, [this, id = item.first] { manager_->execute(id, {}, id_); });
        }
        auto* hint = new QLabel(QStringLiteral("  窗口工作区"), tools); hint->setObjectName("MutedText"); toolbar->addWidget(hint);
        strips_->addStrip("workspace", tools, 1);
        row->addWidget(strips_, 1);
        caption_ = new QLabel(options_.title, chrome_); caption_->setObjectName("CaptionTitle");
        caption_->setMaximumWidth(160); row->addWidget(caption_); caption_->installEventFilter(this);
        status_ = new QLabel(QStringLiteral("就绪  ·  窗口系统预览"), this); status_->setObjectName("StatusBar");
        status_->setContentsMargins(10, 3, 10, 3); column_->addWidget(status_);
    } else {
        strips_.clear();
        caption_ = new QLabel(chrome_); caption_->setObjectName("CaptionTitle");
        row->addWidget(caption_, 1); caption_->installEventFilter(this);
    }
    auto* palette = new QToolButton(chrome_); palette->setIcon(iconFor("search", manager_->theme()));
    palette->setToolTip(QStringLiteral("命令面板  Ctrl+Shift+P")); palette->setAccessibleName(palette->toolTip()); row->addWidget(palette);
    QObject::connect(palette, &QToolButton::clicked, manager_, [this] { manager_->showCommandPalette(id_); });
    auto addCaption = [&](const QString& icon, const QString& title, auto callback) {
        auto* button = new QToolButton(chrome_); button->setObjectName("CaptionButton");
        button->setProperty("role", icon); button->setIcon(iconFor(icon, manager_->theme()));
        button->setFixedSize(42, 32); button->setToolTip(title); button->setAccessibleName(title);
        row->addWidget(button); QObject::connect(button, &QToolButton::clicked, this, callback);
    };
    addCaption("minimize", QStringLiteral("最小化"), [this] { showMinimized(); });
    addCaption("maximize", QStringLiteral("最大化／还原"), [this] { isMaximized() ? showNormal() : showMaximized(); });
    addCaption("close", QStringLiteral("关闭窗口"), [this] { close(); });
    column_->insertWidget(0, chrome_);
}
void WorkspaceWindow::updateCaption()
{
    const auto ctx = manager_->context(id_);
    const auto* tab = manager_->model().tab(ctx.tab);
    const auto title = (tab ? tab->title + QStringLiteral(" — ") : QString()) + options_.title;
    setWindowTitle(title + (kind_ == WindowKind::Floating ? QStringLiteral(" · 浮动窗口") : QString()));
    if (caption_) caption_->setText(kind_ == WindowKind::Floating ? windowTitle() : options_.title);
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
    if (event->type() == QEvent::MouseButtonDblClick) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) { isMaximized() ? showNormal() : showMaximized(); return true; }
    }
    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            press_ = mouse->globalPosition().toPoint(); pendingTitleDrag_ = true;
            if (kind_ == WindowKind::Workbench && windowHandle()) { pendingTitleDrag_ = false; windowHandle()->startSystemMove(); }
        } else if (mouse->button() == Qt::RightButton) { manager_->showWindowMenu(id_, mouse->globalPosition().toPoint()); return true; }
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
