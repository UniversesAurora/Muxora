// SPDX-License-Identifier: MIT
#pragma once

#include "style/frame_motion.hpp"

namespace window_system {

// One expansion session serves ordinary and separately pinned rows. The
// adapter supplies input and clock ticks; Quick can advance at window cadence
// while Widgets keeps using Qt's shared animation driver.
class TabRowsState final {
public:
    static constexpr int collapseDelayMs = 400;
    [[nodiscard]] bool expanded() const { return expanded_; }
    [[nodiscard]] bool dragExpanded() const { return dragExpanded_; }
    [[nodiscard]] bool temporaryOverlay() const { return temporaryOverlay_; }
    [[nodiscard]] bool open() const { return expanded_ || dragExpanded_; }
    [[nodiscard]] bool collapseOnLeave() const { return expandedByWheel_; }
    [[nodiscard]] qreal value() const { return reveal_.value(); }
    [[nodiscard]] bool needsFrame() const { return reveal_.needsFrame(); }

    void setExpanded(bool expanded, bool byWheel) { expanded_ = expanded; expandedByWheel_ = expanded && byWheel; }
    void setDragExpanded(bool expanded, bool moving)
    {
        dragExpanded_ = expanded;
        if (expanded && !expanded_) temporaryOverlay_ = true;
        if (!expanded && (!moving || expanded_)) temporaryOverlay_ = false;
    }
    void resetArrangement()
    {
        expanded_ = false; expandedByWheel_ = false; temporaryOverlay_ = false; reveal_.jump(0);
    }
    void jumpToTarget() { reveal_.jump(open() ? 1 : 0); }

    // Returns whether an adapter must restart its clock. Reaffirming the same
    // target leaves an in-flight animation at its displayed progress.
    bool begin(bool moving, qint64 timestampNs)
    {
        const qreal target = open() ? 1 : 0;
        const bool restart = moving && reveal_.target() != target;
        if (!moving) reveal_.jump(target);
        else reveal_.animateTo(target, Motion::Reveal, true, timestampNs);
        if (moving && !reveal_.needsFrame() && !dragExpanded_) temporaryOverlay_ = false;
        return restart && reveal_.needsFrame();
    }
    bool advance(qint64 timestampNs) { return reveal_.advance(timestampNs); }
    void finish()
    {
        reveal_.jump(open() ? 1 : 0);
        if (!dragExpanded_) temporaryOverlay_ = false;
    }
    [[nodiscard]] bool watchesOutsideInput(bool moving, bool overlaySetting) const
    {
        return (open() || (moving && value() > 0)) && (temporaryOverlay_ || overlaySetting);
    }
    [[nodiscard]] bool dismissesOutsideInput(bool overlaySetting) const { return expanded_ && overlaySetting; }

private:
    bool expanded_ = false;
    bool expandedByWheel_ = false;
    bool dragExpanded_ = false;
    bool temporaryOverlay_ = false;
    FrameMotion reveal_;
};

} // namespace window_system
