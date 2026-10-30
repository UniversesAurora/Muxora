// SPDX-License-Identifier: MIT
#pragma once

#include "docking/tab_layout.hpp"
#include "style/scroll_motion_state.hpp"
#include <window_system/layout_model.hpp>
#include <window_system/theme.hpp>

#include <QElapsedTimer>
#include <QPointer>
#include <QQuickItem>
#include <QTimer>
#include <functional>
#include <optional>

namespace window_system::quick {

class ButtonItem;
class PathItem;
class RailButtonItem;

// A sidebar rail of auto-hide tabs, as EdgeStrip lays it out: buttons keep
// their identity and order along a scrolling track, the all-pin action stays
// at the trailing end, and a drag opens an empty gap but never changes the
// model. Global coordinates match the drag controller's.
class RailItem final : public QQuickItem {
    Q_OBJECT
public:
    RailItem(Edge edge, const Theme& theme, QQuickItem* parent = nullptr);
    void setTheme(const Theme& theme);
    [[nodiscard]] Edge edge() const { return edge_; }
    [[nodiscard]] bool vertical() const { return vertical_; }
    // The thickness the rail needs across, at least the theme's.
    [[nodiscard]] int thickness() const { return thickness_; }

    void addButton(RailButtonItem* button);
    [[nodiscard]] const QVector<RailButtonItem*>& buttons() const { return buttons_; }
    void setTail(ButtonItem* tail);
    [[nodiscard]] QRect viewportRectGlobal() const;
    [[nodiscard]] int insertionIndex(QPoint global, QRect carriedGlobal = {}) const;
    void setInsertionPreview(int index, int extent);
    [[nodiscard]] QRect insertionPreviewRect() const;
    void setDraggedTab(TabId tab);
    void setDragHover(QPoint global);
    [[nodiscard]] int scrollOffset() const { return offset_; }
    void restoreScrollOffset(int offset);
    struct ScrollAnchor {
        int offset = 0;
        TabId tab;
        int shift = 0;
        bool active = false;
    };
    // The checked tab while fully shown, otherwise the tab at the leading edge.
    [[nodiscard]] ScrollAnchor scrollAnchor() const;
    void restoreScrollAnchor(const ScrollAnchor& anchor);
    void scrollByWheel(QPoint delta, bool smooth = true);
    std::function<void()> hoverGeometryChanged;
    std::function<void()> thicknessChanged;

protected:
    void geometryChange(const QRectF& now, const QRectF& previous) override;
    void wheelEvent(QWheelEvent* event) override;
    void hoverEnterEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;

private:
    void layoutTrack();
    void setOffset(int offset);
    [[nodiscard]] int viewportExtent() const;
    [[nodiscard]] int maximumOffset() const;
    [[nodiscard]] int axis(QPointF point) const { return int(vertical_ ? point.y() : point.x()); }
    void syncScrollbar();
    void scheduleMotion();
    void rebuildReorderLayout() const;

    Edge edge_;
    bool vertical_;
    Theme theme_;
    QQuickItem* viewport_;
    QQuickItem* track_;
    PathItem* thumb_;
    QPointer<ButtonItem> tail_;
    QVector<RailButtonItem*> buttons_;
    QPointer<RailButtonItem> dragged_;
    QRect gap_;
    int thickness_ = 0;
    int trackExtent_ = 0;
    int offset_ = 0;
    int previewIndex_ = -1;
    int previewExtent_ = 0;
    int scrollDirection_ = 0;
    bool hovered_ = false;
    QTimer autoScroll_;
    ScrollMotionState motion_;
    QElapsedTimer clock_;
    QMetaObject::Connection frameConnection_;
    mutable TabLayoutInput reorderInput_;
    mutable TabLayout reorderLayout_;
    mutable bool reorderLayoutValid_ = false;
};

} // namespace window_system::quick
