// SPDX-License-Identifier: MIT
#include <QCoreApplication>
#include "status_bar.hpp"
#include "commands/popup_menu.hpp"

#include <window_system/window_manager.hpp>
#include <QAction>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QMenu>
#include <QTimer>

namespace window_system {

WorkspaceStatusBar::WorkspaceStatusBar(WindowManager* manager, WindowId window, QWidget* parent)
    : QLabel(parent), manager_(manager), window_(window),
      message_(new QLabel(QCoreApplication::translate("WindowSystem", "就绪"), this)), workspace_(new QLabel(this))
{
    setObjectName(QStringLiteral("StatusBar"));
    message_->setObjectName(QStringLiteral("StatusMessage"));
    workspace_->setObjectName(QStringLiteral("StatusWorkspace"));
    message_->setAccessibleName(QCoreApplication::translate("WindowSystem", "状态消息"));
    workspace_->setAccessibleName(QCoreApplication::translate("WindowSystem", "工作区信息"));
    message_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    workspace_->setMaximumWidth(320);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(10, 3, 10, 3); row->setSpacing(10);
    row->addWidget(message_, 1); row->addWidget(workspace_);
    setMinimumHeight(fontMetrics().height() + 6);
}

QStringList WorkspaceStatusBar::itemIds() { return {QStringLiteral("message"), QStringLiteral("workspace")}; }
void WorkspaceStatusBar::setMessage(const QString& text) { message_->setText(text); message_->setToolTip(text); }

void WorkspaceStatusBar::setWorkspace(const QString& title, int tabCount)
{
    workspace_->setText(title.isEmpty() ? QCoreApplication::translate("WindowSystem", "%1 个标签").arg(tabCount)
        : QCoreApplication::translate("WindowSystem", "%1  ·  %2 个标签").arg(title).arg(tabCount));
    workspace_->setToolTip(workspace_->text());
}

void WorkspaceStatusBar::setHiddenItems(const QStringList& hidden)
{
    const bool messageVisible = !hidden.contains(QStringLiteral("message"));
    const bool workspaceVisible = !hidden.contains(QStringLiteral("workspace"));
    const QPointer<WorkspaceStatusBar> retained(this);
    message_->setVisible(messageVisible);
    if (!retained) return;
    workspace_->setVisible(workspaceVisible);
}

QMenu* WorkspaceStatusBar::createContextMenu(QWidget* parent) const
{
    auto* menu = new DismissOnlyMenu(parent);
    menu->setObjectName(QStringLiteral("StatusBarMenu"));
    if (!manager_) return menu;
    applyTheme(menu, manager_->theme());
    for (const auto& entry : {std::pair{QStringLiteral("message"), QCoreApplication::translate("WindowSystem", "状态消息")},
                             std::pair{QStringLiteral("workspace"), QCoreApplication::translate("WindowSystem", "工作区信息")}}) {
        auto* action = menu->addAction(entry.second);
        action->setObjectName(QStringLiteral("StatusItemToggle"));
        action->setData(entry.first);
        action->setCheckable(true);
        action->setChecked(manager_->statusItemVisible(window_, entry.first));
        connect(action, &QAction::triggered, menu,
            [manager = manager_, window = window_, id = entry.first](bool visible) {
                if (!manager) return;
                QTimer::singleShot(0, manager, [manager, window, id, visible] {
                    if (manager) manager->execute(QStringLiteral("window.statusItem.setVisible"),
                        {{QStringLiteral("item"), id}, {QStringLiteral("visible"), visible}}, window);
                });
            });
    }
    return menu;
}

void WorkspaceStatusBar::contextMenuEvent(QContextMenuEvent* event)
{
    const QPointer<WorkspaceStatusBar> retained(this);
    const QPointer<QMenu> menu(createContextMenu(this));
    menu->exec(event->globalPos());
    if (menu) delete menu.data();
    if (retained) event->accept();
}

} // namespace window_system
