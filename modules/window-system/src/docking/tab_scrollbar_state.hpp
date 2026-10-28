// SPDX-License-Identifier: MIT
#pragma once

#include "tab_layout.hpp"
#include "style/frame_motion.hpp"
#include <algorithm>
#include <optional>

namespace window_system {

struct TabScrollbarMetrics {
    QPoint origin;
    int thickness = 0;
    // Vertical lanes put the scrollbar next to the content. A strip at the
    // right edge has content on its left; wrapped horizontal rows do not.
    bool contentAtLeft = false;
};

[[nodiscard]] inline QRect tabScrollbarTrack(const TabLayoutLane& lane, const TabScrollbarMetrics& metrics)
{
    const auto viewport = lane.viewport.translated(metrics.origin);
    const int thickness = metrics.thickness;
    if (!lane.vertical)
        return QRect(viewport.left() + lane.frozen, viewport.bottom() - thickness + 1, viewport.width() - lane.frozen, thickness);
    return QRect(metrics.contentAtLeft ? viewport.left() + 1 : viewport.right() - thickness,
        viewport.top(), thickness, viewport.height());
}

struct TabScrollbarGeometry {
    QRect track;
    QRect thumb;
    bool vertical = false;
    int maximumOffset = 0;

    // A click outside the thumb centers it at the pointer. Return no offset
    // for a thumb press so the adapter does not interrupt its current scroll.
    [[nodiscard]] std::optional<int> jumpOffset(QPoint point) const
    {
        if (thumb.contains(point)) return {};
        const int travel = std::max(1, vertical ? track.height() - thumb.height() : track.width() - thumb.width());
        const int along = vertical ? point.y() - track.top() - thumb.height() / 2
            : point.x() - track.left() - thumb.width() / 2;
        return int(qint64(std::clamp(along, 0, travel)) * maximumOffset / travel);
    }

    [[nodiscard]] int draggedOffset(QPoint start, QPoint point, int originOffset) const
    {
        const int travel = std::max(1, vertical ? track.height() - thumb.height() : track.width() - thumb.width());
        const int moved = vertical ? point.y() - start.y() : point.x() - start.x();
        // The lane application path owns clamping, including a pointer that
        // leaves the track while its input adapter keeps mouse capture.
        return originOffset + int(qint64(moved) * maximumOffset / travel);
    }
};

[[nodiscard]] inline TabScrollbarGeometry tabScrollbarGeometry(const TabLayoutLane& lane, const TabScrollbarMetrics& metrics)
{
    TabScrollbarGeometry result{tabScrollbarTrack(lane, metrics), {}, lane.vertical, lane.maximumOffset()};
    if (result.maximumOffset <= 0 || lane.extent <= 0) return result;
    const int length = lane.vertical ? result.track.height() : result.track.width();
    const int visible = lane.vertical ? lane.viewport.height() : lane.viewport.width() - lane.frozen;
    const int thumb = std::max(16, int(qint64(length) * visible / std::max(1, lane.extent - (lane.vertical ? 0 : lane.frozen))));
    const int position = int(qint64(length - thumb) * lane.offset / std::max(1, result.maximumOffset));
    result.thumb = lane.vertical ? QRect(result.track.left(), result.track.top() + position, result.track.width(), thumb)
        : QRect(result.track.left() + position, result.track.top(), thumb, result.track.height());
    return result;
}

struct TabScrollbarOffset {
    int lane = -1;
    int offset = 0;
};

// Geometry and one capture session use strip-local coordinates, including
// the strip origin. Native capture, forwarding and hide timers stay with
// the adapter; opacity is sampled here. Wheel routing and boundary guards use
// controller. No lane or layout is retained across frames.
class TabScrollbarState final {
public:
    static constexpr int hideDelayMs = 700;
    [[nodiscard]] int shownLane() const { return shownLane_; }
    [[nodiscard]] int dragLane() const { return dragLane_; }
    [[nodiscard]] bool dragging() const { return dragLane_ >= 0; }
    void reveal(int lane) { shownLane_ = lane; }
    [[nodiscard]] qreal opacity() const { return opacity_.value(); }
    [[nodiscard]] bool needsFrame() const { return opacity_.needsFrame(); }
    [[nodiscard]] int durationMs() const { return opacity_.durationMs(); }
    bool advance(qint64 timestampNs) { return opacity_.advance(timestampNs); }
    bool show(bool shown, bool moving, qint64 timestampNs)
    {
        const qreal target = shown ? 1 : 0;
        if (opacity_.target() == target && (opacity_.needsFrame() || opacity_.value() == target)) return false;
        opacity_.animateTo(target, Motion::Presence, moving, timestampNs);
        return true;
    }

    [[nodiscard]] QRect hitRect(const TabLayout& layout, const TabScrollbarMetrics& metrics,
                                QRect bounds, bool visible) const
    {
        if (!visible || opacity() <= 0.05 || shownLane_ < 0 || shownLane_ >= layout.laneList.size()) return {};
        const auto& lane = layout.laneList.at(shownLane_);
        if (lane.maximumOffset() <= 0) return {};
        const auto track = tabScrollbarTrack(lane, metrics);
        return (lane.vertical ? track.adjusted(-2, 0, 2, 0) : track.adjusted(0, -2, 0, 2)).intersected(bounds);
    }

    // Apply a jump first, then supply the lane's resulting offset. This keeps
    // the grab origin correct after clamping or a synchronous layout change.
    void beginDrag(int lane, QPoint point, int offset)
    {
        dragLane_ = lane; dragStart_ = point; dragOrigin_ = offset;
    }

    [[nodiscard]] std::optional<TabScrollbarOffset> move(const TabLayout& layout, const TabScrollbarMetrics& metrics,
                                                        QPoint point) const
    {
        if (dragLane_ < 0 || dragLane_ >= layout.laneList.size()) return {};
        const auto geometry = tabScrollbarGeometry(layout.laneList.at(dragLane_), metrics);
        return TabScrollbarOffset{dragLane_, geometry.draggedOffset(dragStart_, point, dragOrigin_)};
    }

    bool endDrag()
    {
        if (!dragging()) return false;
        dragLane_ = -1; return true;
    }

private:
    int shownLane_ = -1;
    int dragLane_ = -1;
    int dragOrigin_ = 0;
    QPoint dragStart_;
    FrameMotion opacity_;
};

} // namespace window_system
