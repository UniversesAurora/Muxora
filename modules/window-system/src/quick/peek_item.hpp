// SPDX-License-Identifier: MIT
#pragma once

#include "docking/drag_start.hpp"
#include <window_system/layout_model.hpp>
#include <window_system/theme.hpp>

#include <QFont>
#include <QPointer>
#include <QQuickItem>
#include <QVector>
#include <functional>
#include <optional>

namespace window_system::quick {

class ButtonItem;
class PathItem;
class TabLabelTextItem;

// The panel an auto-hide tab opens over its window, as the Widgets
// PeekPanel: an accent frame, a title row with pin and close actions, and
// the tab's page below it. The page stays owned by the dock surface. Thin
// handles inside its free edges resize it, and the title row drags the tab.
class PeekItem final : public QQuickItem {
    Q_OBJECT
public:
    PeekItem(const Theme& theme, QQuickItem* parent = nullptr);
    void setTitle(const QString& title, const QFont& font);
    void setPage(QQuickItem* page);
    [[nodiscard]] QQuickItem* page() const { return page_; }
    // The side of its rail: the panel grows away from it, along it, or
    // both at the corner.
    void setEdge(Edge edge);
    std::function<void()> pinRequested;
    std::function<void()> closeRequested;
    std::function<PeekSizePolicy()> requestedSize;
    std::function<void(PeekSizePolicy)> resizeRequested;
    std::function<void(DragStart)> dragRequested;
    std::function<void(QPoint)> contextMenuRequested;

protected:
    bool event(QEvent* event) override;
    void geometryChange(const QRectF& now, const QRectF& previous) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void relayout();
    [[nodiscard]] int headerHeight() const;
    std::optional<Edge> edge_;
    QVector<QQuickItem*> handles_;
    bool pressed_ = false;
    DragStart start_;
    Theme theme_;
    PathItem* surface_;
    PathItem* frame_;
    TabLabelTextItem* title_;
    ButtonItem* pin_;
    ButtonItem* close_;
    QPointer<QQuickItem> page_;
    QString titleText_;
    QFont font_;
};

} // namespace window_system::quick
