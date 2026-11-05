// SPDX-License-Identifier: MIT
#pragma once

#include "chrome/toolbar_description.hpp"
#include "chrome/strip_layout_state.hpp"
#include "chrome/strip_constraints.hpp"
#include <window_system/theme.hpp>
#include <QPointer>
#include <QQuickItem>
#include <functional>

namespace window_system::quick {

class ButtonItem;
class PopupMenu;
class PathStrokeItem;
class PathItem;

// One strip frame for command buttons or existing Quick content. The content
// keeps its QObject owner; this frame only attaches it to a clipped viewport.
class CommandStripItem final : public QQuickItem {
    Q_OBJECT
public:
    explicit CommandStripItem(QQuickItem* parent = nullptr, bool optionsEnabled = true);
    ~CommandStripItem() override;
    void sync(const QVector<MenuItemDescription>& actions, const Theme& theme, const StripLayoutPreferences& preferences, bool hideEmptyOptions);
    void setContent(QQuickItem* content, StripConstraints constraints);
    void syncContent(const Theme& theme, const StripLayoutPreferences& preferences, bool hideEmptyOptions);
    void setLocked(bool locked);
    int minimumWidth() const;
    std::function<void(const QString&)> triggered;
protected:
    void geometryChange(const QRectF& now, const QRectF& previous) override;
private:
    void relayout();
    void showOverflow();
    void updateArrows();
    void updateMetrics();
    void updateHandle();
    int chromeMinimumWidth() const;
    struct Action {
        MenuItemDescription description;
        QPointer<ButtonItem> button;
        bool included = true;
    };
    Theme theme_;
    QVector<Action> actions_;
    QVector<MenuItemDescription> overflow_;
    QQuickItem* viewport_;
    QPointer<QQuickItem> content_;
    StripConstraints constraints_;
    ButtonItem* more_;
    PathStrokeItem* arrows_;
    PathItem* handle_;
    QPointer<PopupMenu> popup_;
    bool hideEmptyOptions_ = false;
    int contentWidth_ = 0;
    QSizeF arrowSize_;
    qreal arrowScale_ = -1;
    QColor arrowColor_;
    bool arrowsOverflow_ = false;
    bool optionsEnabled_ = true;
    bool contentVisible_ = true;
    bool locked_ = true;
    QSizeF handleSize_;
    qreal handleScale_ = -1;
    QColor handleColor_;
};

} // namespace window_system::quick
