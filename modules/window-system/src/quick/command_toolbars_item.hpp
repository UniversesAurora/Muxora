// SPDX-License-Identifier: MIT
#pragma once

#include "chrome/strip_layout_state.hpp"
#include "chrome/toolbar_description.hpp"
#include <window_system/theme.hpp>
#include <QQuickItem>
#include <QPointer>
#include <functional>

namespace window_system::quick {
class CommandToolbarItem;

// Command strips in both dock areas share one registry and row allocator.
class CommandToolbarsItem final : public QQuickItem {
    Q_OBJECT
public:
    explicit CommandToolbarsItem(QQuickItem* parent);
    ~CommandToolbarsItem() override;
    void sync(const QVector<CommandToolbarDescription>& descriptions, const QJsonObject& saved, const Theme& theme, bool hideOptionsWhenLocked);
    QQuickItem* bottomArea() const { return bottom_; }
    int minimumWidth() const { return minimumWidth_; }
    std::function<void(const QString&)> triggered;
protected:
    void geometryChange(const QRectF& now, const QRectF& previous) override;
private:
    void relayout();
    QHash<QString, QPointer<CommandToolbarItem>> strips_;
    StripLayoutSnapshot layout_;
    QQuickItem* bottom_;
    int minimumWidth_ = 0;
    bool arranging_ = false;
};

} // namespace window_system::quick
