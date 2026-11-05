// SPDX-License-Identifier: MIT
#pragma once

#include "chrome/strip_layout_state.hpp"
#include "chrome/strip_constraints.hpp"
#include "chrome/toolbar_description.hpp"
#include <window_system/theme.hpp>
#include <QQuickItem>
#include "window_drag_area_item.hpp"
#include <QPointer>
#include <functional>

namespace window_system::quick {
class CommandStripItem;

// Command strips in both dock areas share one registry and row allocator.
class CommandStripHostItem final : public WindowDragAreaItem {
    Q_OBJECT
public:
    explicit CommandStripHostItem(QQuickItem* parent);
    ~CommandStripHostItem() override;
    void sync(const QVector<CommandToolbarDescription>& descriptions, const QJsonObject& saved, const Theme& theme, bool hideOptionsWhenLocked);
    void setContent(QString id, QQuickItem* content, StripConstraints constraints, StripSizing sizing = StripSizing::Content);
    QQuickItem* bottomArea() const { return bottom_; }
    int minimumWidth() const { return minimumWidth_; }
    std::function<void(const QString&)> triggered;
protected:
    void geometryChange(const QRectF& now, const QRectF& previous) override;
private:
    void restoreLayout(const QVector<CommandToolbarDescription>& descriptions, const QJsonObject& saved);
    void normalizeLayout(const QHash<QString, StripLayoutPreferences>& defaults);
    void syncViews(const QVector<CommandToolbarDescription>& descriptions, const Theme& theme, bool hideOptionsWhenLocked);
    void relayout();
    void requestLayout();
    StripConstraints constraintsFor(const QString& id) const;
    struct Content {
        QString id;
        QPointer<QQuickItem> item;
        StripConstraints constraints;
        StripSizing sizing;
    };
    void watchStrip(CommandStripItem* strip);
    QHash<QString, QPointer<CommandStripItem>> strips_;
    QVector<Content> contents_;
    StripLayoutSnapshot layout_;
    QQuickItem* bottom_;
    int minimumWidth_ = 0;
    bool arranging_ = false;
    bool layoutPending_ = false;
};

} // namespace window_system::quick
