// SPDX-License-Identifier: MIT
#pragma once

#include "tab_hover_controller.hpp"
#include "style/frame_motion.hpp"

namespace window_system {

// Retains the last action owner while it fades out. The hover controller
// decides whether entering a label starts a fade or shows it immediately.
class TabActionMotion final {
public:
    [[nodiscard]] int index() const { return index_; }
    [[nodiscard]] qreal opacity() const { return opacity_.value(); }
    [[nodiscard]] bool needsFrame() const { return opacity_.needsFrame(); }
    [[nodiscard]] int durationMs() const { return opacity_.durationMs(); }

    // Immediate changes still need the adapter's usual invalidation. Return
    // true only for a new timed transition, so an unchanged target keeps time.
    bool apply(TabHoverTransition::Fade fade, int index, bool animated, qint64 timestampNs)
    {
        using Fade = TabHoverTransition::Fade;
        if (fade == Fade::None) return false;
        if (fade == Fade::In || fade == Fade::Show) index_ = index;
        if (fade == Fade::Show) { opacity_.jump(1); return false; }
        const qreal target = fade == Fade::In ? 1 : 0;
        const bool continuing = opacity_.needsFrame() && opacity_.target() == target;
        opacity_.animateTo(target, Motion::Hover, animated, timestampNs);
        return !continuing && opacity_.needsFrame();
    }

    bool advance(qint64 timestampNs) { return opacity_.advance(timestampNs); }

    void finish()
    {
        if (opacity_.value() == 0) index_ = -1;
    }

    void removedTab(int index)
    {
        if (index_ == index) index_ = -1;
        else if (index_ > index) --index_;
    }

private:
    int index_ = -1;
    FrameMotion opacity_;
};

} // namespace window_system
