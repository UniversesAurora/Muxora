// SPDX-License-Identifier: MIT
#pragma once

#include "style/frame_motion.hpp"
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace window_system {

// Only the current label has selection elevation. Retaining one owner avoids
// scanning labels or allocating a UUID map when a group rebuilds its views.
struct TabSurfaceSnapshot {
    std::optional<QUuid> current;
    qreal selection = 0;
    qreal lift = 0;
    bool incoming = false;
};

// The adapter supplies attachment targets from the existing surface geometry
// and advances all channels once before sampling a frame. Mutators return true
// only when they start a new timed transition; immediate changes still need
// the adapter's usual invalidation. No clock or geometry is owned here.
class TabSurfaceMotion final {
public:
    [[nodiscard]] qreal selectionWeight() const { return current_ && !incoming_ ? selection_.value() : 0; }
    [[nodiscard]] qreal liftProgress() const { return lift_.value(); }
    [[nodiscard]] std::pair<qreal, qreal> attachmentValues() const
    {
        const qreal progress = attachment_.value();
        return {attachmentFrom_.first + (attachmentTo_.first - attachmentFrom_.first) * progress,
                attachmentFrom_.second + (attachmentTo_.second - attachmentFrom_.second) * progress};
    }
    [[nodiscard]] bool needsFrame() const { return selection_.needsFrame() || lift_.needsFrame() || attachment_.needsFrame(); }

    // A restarted shared Qt clock can use this upper bound and stop early
    // when needsFrame() becomes false. FrameMotion owns distance scaling.
    [[nodiscard]] int durationMs() const
    {
        int duration = 0;
        for (const auto* channel : {&selection_, &lift_, &attachment_})
            if (channel->needsFrame()) duration = std::max(duration, channel->durationMs());
        return duration;
    }

    bool advance(qint64 timestampNs)
    {
        bool changed = selection_.advance(timestampNs);
        changed = lift_.advance(timestampNs) || changed;
        changed = attachment_.advance(timestampNs) || changed;
        return changed;
    }

    // Adding/removing labels or assigning their metadata may change the
    // current ID without an activation. Keep the displayed weight then.
    void bindCurrent(std::optional<QUuid> current) { current_ = std::move(current); }

    bool select(std::optional<QUuid> current, bool moving, qint64 timestampNs)
    {
        current_ = std::move(current);
        // Changing ownership immediately removes the old label's elevation;
        // only the newly selected label rises. There is no outgoing fade.
        selection_.jump(0);
        selection_.animateTo(1, Motion::SurfaceChangeLift, moving && current_.has_value(), timestampNs);
        return selection_.needsFrame();
    }

    bool setLifted(bool lifted, bool moving, qint64 timestampNs)
    {
        const qreal target = lifted ? 1 : 0;
        if (lift_.target() == target && (lift_.needsFrame() || lift_.value() == target)) return false;
        lift_.animateTo(target, lifted ? Motion::SurfaceLift : Motion::SurfaceLower, moving, timestampNs);
        return lift_.needsFrame();
    }

    bool setIncoming(bool incoming, bool moving, qint64 timestampNs)
    {
        if (incoming_ == incoming) return false;
        incoming_ = incoming;
        // Incoming labels suppress local selection but retain its sampled
        // progress. Leaving resumes it without restarting selection.
        return setLifted(incoming, moving, timestampNs);
    }

    bool syncAttachment(QUuid owner, std::pair<qreal, qreal> target, bool moving, qint64 timestampNs)
    {
        if (!attachmentOwner_ || *attachmentOwner_ != owner) {
            attachmentOwner_ = owner;
            attachmentFrom_ = attachmentTo_ = target;
            attachment_.jump(1);
            return false;
        }
        if (attachmentTo_ == target) return false;
        attachmentFrom_ = attachmentValues(); attachmentTo_ = target;
        const qreal distance = std::max(std::abs(target.first - attachmentFrom_.first),
                                        std::abs(target.second - attachmentFrom_.second));
        attachment_.jump(0);
        attachment_.animateTo(1, target.first > attachmentFrom_.first ? Motion::SurfaceChangeLift : Motion::SurfaceChangeLower,
                              moving, timestampNs, distance);
        return attachment_.needsFrame();
    }

    [[nodiscard]] TabSurfaceSnapshot capture() const { return {current_, selectionWeight(), lift_.value(), incoming_}; }

    bool inherit(const TabSurfaceSnapshot& previous, std::optional<QUuid> current, bool moving, qint64 timestampNs)
    {
        current_ = std::move(current);
        const bool sameTab = previous.current == current_;
        const qreal start = previous.incoming ? 1 : sameTab ? previous.selection : 0;
        selection_.jump(start);
        selection_.animateTo(1, Motion::SurfaceChangeLift, moving, timestampNs);
        lift_.jump(sameTab || previous.incoming ? previous.lift : 0);
        lift_.animateTo(0, Motion::SurfaceLower, moving, timestampNs);
        return selection_.needsFrame() || lift_.needsFrame();
    }

private:
    std::optional<QUuid> current_;
    bool incoming_ = false;
    FrameMotion selection_{1};
    FrameMotion lift_;
    FrameMotion attachment_{1};
    std::optional<QUuid> attachmentOwner_;
    std::pair<qreal, qreal> attachmentFrom_{1, 0};
    std::pair<qreal, qreal> attachmentTo_{1, 0};
};

} // namespace window_system
