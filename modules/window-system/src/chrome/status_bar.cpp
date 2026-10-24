// SPDX-License-Identifier: MIT
#include <QCoreApplication>
#include "status_bar.hpp"
#include "commands/popup_menu.hpp"

#include <window_system/window_manager.hpp>
#include <QAction>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QMenu>
#include <QSet>
#include <QTimer>
#include <QToolButton>
#include <algorithm>

namespace window_system {

WorkspaceStatusBar::WorkspaceStatusBar(WindowManager* manager, WindowId window, QWidget* parent)
    : QLabel(parent), manager_(manager), window_(window),
      message_(new QLabel(QCoreApplication::translate("WindowSystem", "Ready"), this)), workspace_(new QLabel(this))
{
    setObjectName(QStringLiteral("StatusBar"));
    message_->setObjectName(QStringLiteral("StatusMessage"));
    messageText_ = message_->text();
    workspace_->setObjectName(QStringLiteral("StatusWorkspace"));
    message_->setAccessibleName(QCoreApplication::translate("WindowSystem", "Status message"));
    workspace_->setAccessibleName(QCoreApplication::translate("WindowSystem", "Workspace information"));
    message_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    workspace_->setMaximumWidth(320);
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(10, 3, 10, 3); row->setSpacing(10);
    row->addWidget(message_, 1); row->addWidget(workspace_);
    updateHeight();
}

void WorkspaceStatusBar::updateHeight()
{
    // Matches VS Code's 22 px status bar; a larger font still expands it.
    const int metric = manager_ ? manager_->theme().metrics.statusHeight : ThemeMetrics{}.statusHeight;
    setFixedHeight(std::max(metric, fontMetrics().height() + 6));
}

void WorkspaceStatusBar::changeEvent(QEvent* event)
{
    QLabel::changeEvent(event);
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) updateHeight();
}

QStringList WorkspaceStatusBar::itemIds() { return {QStringLiteral("message"), QStringLiteral("workspace")}; }
void WorkspaceStatusBar::setMessage(const QString& text)
{
    messageText_ = text;
    const auto displayed = transientMessage_.isEmpty() ? messageText_ : transientMessage_;
    message_->setText(displayed); message_->setToolTip(displayed);
}

void WorkspaceStatusBar::setTransientMessage(const QString& text)
{
    transientMessage_ = text;
    const auto displayed = transientMessage_.isEmpty() ? messageText_ : transientMessage_;
    message_->setText(displayed); message_->setToolTip(displayed);
}

void WorkspaceStatusBar::setWorkspace(const QString& title, int tabCount)
{
    workspace_->setText(title.isEmpty() ? QCoreApplication::translate("WindowSystem", "%n tabs", nullptr, tabCount)
        : QCoreApplication::translate("WindowSystem", "%1  ·  %n tabs", nullptr, tabCount).arg(title));
    workspace_->setToolTip(workspace_->text());
}

void WorkspaceStatusBar::setHiddenItems(const QStringList& hidden)
{
    hiddenItems_ = hidden;
    const bool messageVisible = !hidden.contains(QStringLiteral("message"));
    const bool workspaceVisible = !hidden.contains(QStringLiteral("workspace"));
    const QPointer<WorkspaceStatusBar> retained(this);
    message_->setVisible(messageVisible);
    if (!retained) return;
    workspace_->setVisible(workspaceVisible);
    if (!retained) return;
    const auto items = customItems_;
    for (auto it = items.cbegin(); it != items.cend(); ++it) {
        if (it.value()) {
            const auto* button = qobject_cast<QToolButton*>(it.value().data());
            it.value()->setVisible(!hidden.contains(it.key()) && (!button || !button->text().isEmpty()));
        }
        if (!retained) return;
    }
}

void WorkspaceStatusBar::setCustomItems(const QVector<StatusItem>& items)
{
    customDefinitions_ = items;
    const QPointer<WorkspaceStatusBar> retained(this);
    QSet<QString> live;
    for (const auto& item : items) live.insert(item.id);
    for (auto it = customItems_.begin(); it != customItems_.end();) {
        if (live.contains(it.key())) { ++it; continue; }
        auto label = it.value(); it = customItems_.erase(it);
        if (label) delete label.data();
        if (!retained) return;
    }
    auto* row = qobject_cast<QHBoxLayout*>(layout());
    for (const auto& item : items) {
        auto widget = customItems_.value(item.id);
        if (!widget) {
            if (item.command.isEmpty()) {
                auto* label = new QLabel(this);
                label->setTextFormat(Qt::PlainText);
                label->setObjectName(QStringLiteral("CustomStatusItem"));
                widget = label;
            } else {
                auto* button = new QToolButton(this);
                button->setObjectName(QStringLiteral("CustomStatusCommandItem"));
                button->setFocusPolicy(Qt::StrongFocus);
                button->setCursor(Qt::PointingHandCursor);
                widget = button;
                connect(button, &QToolButton::clicked, this,
                    [this, button, id = item.id, command = item.command] {
                        const auto manager = manager_;
                        const auto window = window_;
                        QTimer::singleShot(0, button, [manager, window, id, command] {
                            if (!manager || !manager->model().window(window)) return;
                            const auto definitions = manager->statusItems();
                            const auto found = std::find_if(definitions.cbegin(), definitions.cend(),
                                [&](const auto& definition) { return definition.id == id && definition.command == command; });
                            if (found != definitions.cend()) manager->execute(command, {}, window);
                        });
                    });
            }
            widget->setProperty("statusItemId", item.id);
            customItems_.insert(item.id, widget);
            row->insertWidget(row->count() - 1, widget);
        }
        if (auto* label = qobject_cast<QLabel*>(widget.data())) label->setText(item.text);
        else if (auto* button = qobject_cast<QToolButton*>(widget.data())) button->setText(item.text);
        widget->setToolTip(item.tooltip);
        widget->setAccessibleName(item.text);
        widget->setVisible(!hiddenItems_.contains(item.id) && (item.command.isEmpty() || !item.text.isEmpty()));
        if (!retained) return;
    }
}

QMenu* WorkspaceStatusBar::createContextMenu(QWidget* parent) const
{
    auto* menu = new DismissOnlyMenu(parent);
    menu->setObjectName(QStringLiteral("StatusBarMenu"));
    if (!manager_) return menu;
    applyTheme(menu, manager_->theme());
    QVector<QPair<QString, QString>> entries{{QStringLiteral("message"), QCoreApplication::translate("WindowSystem", "Status message")},
        {QStringLiteral("workspace"), QCoreApplication::translate("WindowSystem", "Workspace information")}};
    for (const auto& item : customDefinitions_) {
        auto title = item.text;
        if (title.isEmpty() && !item.command.isEmpty()) {
            if (const auto* command = manager_->commands().find(item.command)) title = command->title;
        }
        entries.append({item.id, title.isEmpty() ? item.id : title});
    }
    for (const auto& entry : entries) {
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
