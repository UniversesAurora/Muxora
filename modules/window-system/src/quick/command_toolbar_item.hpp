// SPDX-License-Identifier: MIT
#pragma once

#include "chrome/toolbar_description.hpp"
#include <window_system/theme.hpp>
#include <QPointer>
#include <QQuickItem>
#include <functional>

namespace window_system::quick {

class ButtonItem;
class PopupMenu;
class PathStrokeItem;

// Command-only toolbar content. Window strip layout owns its position and
// requested width; commands and overflow retain the same descriptions.
class CommandToolbarItem final : public QQuickItem {
    Q_OBJECT
public:
    explicit CommandToolbarItem(QQuickItem* parent = nullptr);
    ~CommandToolbarItem() override;
    void sync(const QVector<MenuItemDescription>& actions, const Theme& theme, const QStringList& excluded, bool hideEmptyOptions);
    std::function<void(const QString&)> triggered;
protected:
    void geometryChange(const QRectF& now, const QRectF& previous) override;
private:
    void relayout();
    void showOverflow();
    void updateArrows();
    struct Action {
        MenuItemDescription description;
        QPointer<ButtonItem> button;
        bool included = true;
    };
    Theme theme_;
    QVector<Action> actions_;
    QVector<MenuItemDescription> overflow_;
    ButtonItem* more_;
    PathStrokeItem* arrows_;
    QPointer<PopupMenu> popup_;
    bool hideEmptyOptions_ = false;
    int contentWidth_ = 0;
    QSizeF arrowSize_;
    qreal arrowScale_ = -1;
    QColor arrowColor_;
    bool arrowsOverflow_ = false;
};

} // namespace window_system::quick
