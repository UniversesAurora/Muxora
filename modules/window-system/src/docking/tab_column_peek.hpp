// SPDX-License-Identifier: MIT
#pragma once

namespace window_system {

struct TabColumnPeekAction {
    enum class Timer { Keep, Start, Stop };
    Timer timer = Timer::Keep;
    bool relayout = false;
    bool animate = true;
};

// A host supplies hover, popup ownership, scrollbar capture and timer expiry.
// State changes precede layout notifications; no host callback runs here.
class TabColumnPeek final {
public:
    static constexpr int delayMs = 120;
    bool setIconsOnly(bool icons)
    {
        if (icons_ == icons) return false;
        icons_ = icons; expanded_ = false;
        return true;
    }
    [[nodiscard]] bool iconsOnly() const { return icons_; }
    [[nodiscard]] bool expanded() const { return expanded_; }
    void setPopupOpen(bool open) { popup_ = open; }

    [[nodiscard]] TabColumnPeekAction hover(bool inside, bool timerActive, bool scrollbarHeld)
    {
        using Timer = TabColumnPeekAction::Timer;
        if (inside) return {icons_ && !expanded_ && !timerActive ? Timer::Start : Timer::Keep};
        if (popup_ || scrollbarHeld || !expanded_) return {Timer::Stop};
        expanded_ = false;
        return {Timer::Stop, true};
    }
    [[nodiscard]] TabColumnPeekAction timeout()
    {
        if (!icons_ || expanded_) return {};
        expanded_ = true;
        return {TabColumnPeekAction::Timer::Keep, true};
    }
    [[nodiscard]] TabColumnPeekAction restore(bool expanded)
    {
        if (!icons_ || !expanded || expanded_) return {};
        expanded_ = true;
        return {TabColumnPeekAction::Timer::Stop, true, false};
    }

private:
    bool icons_ = false;
    bool expanded_ = false;
    bool popup_ = false;
};

} // namespace window_system
