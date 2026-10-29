// SPDX-License-Identifier: MIT
#pragma once

#include "style/frame_motion.hpp"
#include <algorithm>

namespace window_system {

// Layout and surface channels are sampled together before the adapter applies
// either result. Completion also publishes layout to release size constraints.
class TabGroupMotion final {
public:
    struct Change { bool layout; bool surface; };

    [[nodiscard]] qreal expansion() const { return expansion_.value(); }
    [[nodiscard]] qreal peek() const { return peek_.value(); }
    [[nodiscard]] qreal focus() const { return focus_.value(); }
    [[nodiscard]] bool focused() const { return focused_; }
    [[nodiscard]] bool columnRunning() const { return expansion_.needsFrame(); }
    [[nodiscard]] bool layoutRunning() const { return expansion_.needsFrame() || peek_.needsFrame(); }
    [[nodiscard]] bool needsFrame() const { return layoutRunning() || focus_.needsFrame(); }
    [[nodiscard]] int durationMs() const
    {
        int duration = 0;
        for (const auto* channel : {&expansion_, &peek_, &focus_})
            if (channel->needsFrame()) duration = std::max(duration, channel->durationMs());
        return duration;
    }

    Change advance(qint64 timestampNs)
    {
        const bool wasLayout = layoutRunning();
        bool layout = expansion_.advance(timestampNs);
        layout = peek_.advance(timestampNs) || layout;
        return {layout || (wasLayout && !layoutRunning()), focus_.advance(timestampNs)};
    }

    // Hidden, compact and actively resized columns apply targets immediately.
    // Otherwise retain existing targets; changing animation preferences alone
    // does not restart a transition that is already in progress.
    bool syncColumn(bool icons, bool peeking, bool transition, bool enabled, qint64 timestampNs)
    {
        const auto sync = [&](FrameMotion& channel, qreal target) {
            if (!transition) { channel.jump(target); return false; }
            if (channel.target() == target) return false;
            channel.animateTo(target, Motion::Reveal, enabled, timestampNs);
            return channel.needsFrame();
        };
        const bool expansion = sync(expansion_, icons ? 0 : 1);
        return sync(peek_, icons && peeking ? 1 : 0) || expansion;
    }

    bool setFocused(bool focused, bool enabled, qint64 timestampNs)
    {
        focused_ = focused;
        focus_.animateTo(focused ? 1 : 0, focused ? Motion::SurfaceLift : Motion::SurfaceLower, enabled, timestampNs);
        return focus_.needsFrame();
    }

    bool inheritFocus(qreal value, bool focused, bool enabled, qint64 timestampNs)
    {
        focus_.jump(value);
        return setFocused(focused, enabled, timestampNs);
    }

    void restorePeek() { peek_.jump(1); }

private:
    FrameMotion expansion_{1};
    FrameMotion peek_;
    FrameMotion focus_;
    bool focused_ = false;
};

} // namespace window_system
