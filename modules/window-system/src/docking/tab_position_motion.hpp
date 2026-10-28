// SPDX-License-Identifier: MIT
#pragma once

#include "style/frame_motion.hpp"
#include <QHash>
#include <QRect>
#include <QUuid>
#include <cmath>
#include <optional>

namespace window_system {

struct TabPositionWidth {
    bool managedColumn = false;
    bool columnTransition = false;
    bool rearrangingTiles = false;
};

// Position and width share one frame value. Adapters advance the monotonic
// clock before sampling; a Quick adapter can then sample every label from
// the same window frame without owning a timer for each label.
class TabPositionMotion final {
public:
    void setMotion(Motion motion) { motion_ = motion; }
    void start(qint64 timestampNs)
    {
        progress_.jump(0); progress_.animateTo(1, motion_, true, timestampNs);
    }
    void advance(qint64 timestampNs) { progress_.advance(timestampNs); }
    [[nodiscard]] bool needsFrame() const { return !slides_.isEmpty() && progress_.needsFrame(); }
    [[nodiscard]] bool isEmpty() const { return slides_.isEmpty(); }
    [[nodiscard]] bool contains(const QUuid& id) const { return slides_.contains(id); }
    [[nodiscard]] bool resizesWidth(const QUuid& id) const
    {
        const auto slide = slides_.constFind(id); return slide != slides_.cend() && slide->resizeWidth;
    }
    void slide(const QUuid& id, QRect from, bool resizeWidth) { slides_.insert(id, {from, resizeWidth}); }
    void setFrom(const QUuid& id, QRect from) { slides_[id].from = from; }
    void settle(const QUuid& id, int index, QRect from) { slide(id, from, true); settling_.insert(id, index); }
    [[nodiscard]] const QHash<QUuid, int>& settling() const { return settling_; }
    [[nodiscard]] bool isSettling(const QUuid& id) const { return settling_.contains(id); }
    void clear() { slides_.clear(); settling_.clear(); progress_.jump(1); }

    [[nodiscard]] QRect rect(const QUuid& id, QRect target, TabPositionWidth width,
                              std::optional<qreal> planarProgress = {}) const
    {
        const auto slide = slides_.constFind(id);
        if (slide == slides_.cend()) return target;
        const qreal progress = planarProgress.value_or(progress_.value());
        const auto mix = [progress](int a, int b) { return int(std::lround(a + (b - a) * progress)); };
        QRect result(QPoint(mix(slide->from.left(), target.left()), mix(slide->from.top(), target.top())),
            QSize(mix(slide->from.width(), target.width()), mix(slide->from.height(), target.height())));
        if (width.managedColumn && !width.rearrangingTiles && (width.columnTransition || !slide->resizeWidth)
            && !isSettling(id)) result.setWidth(target.width());
        return result;
    }
private:
    struct Slide { QRect from; bool resizeWidth = false; };
    QHash<QUuid, Slide> slides_;
    QHash<QUuid, int> settling_;
    FrameMotion progress_{1};
    Motion motion_ = Motion::Relocate;
};

} // namespace window_system
