// SPDX-License-Identifier: MIT
#pragma once

namespace window_system {

struct TabHoverHit {
    int tab = -1;
    bool action = false;
    int lane = -1;
    int folder = -1;
    bool folderAdd = false;
    bool operator==(const TabHoverHit&) const = default;
};

struct TabHoverTransition {
    enum class Fade { None, In, Out, Show };
    bool changed = false;
    Fade fade = Fade::None;
};

// Geometry and animation clocks stay with the host. A label session survives
// gaps between labels, so changing targets does not replay the action fade.
class TabHoverController final {
public:
    [[nodiscard]] TabHoverTransition move(TabHoverHit next)
    {
        using Fade = TabHoverTransition::Fade;
        const auto fade = next.tab >= 0 && next.tab != hit_.tab ? (session_ ? Fade::Show : Fade::In)
            : next.tab < 0 && hit_.tab >= 0 ? Fade::Out : Fade::None;
        if (next.tab >= 0 && next.tab != hit_.tab) session_ = true;
        const bool changed = next != hit_;
        hit_ = next;
        return {changed, fade};
    }
    void leave() { hit_ = {}; session_ = false; }
    void beginSession() { session_ = true; }
    void endSession() { session_ = false; }
    void removedTab(int index) { if (hit_.tab >= index) hit_.tab = -1; }
    void clearFolder() { hit_.folder = -1; }
    [[nodiscard]] const TabHoverHit& hit() const { return hit_; }
    [[nodiscard]] bool hasSession() const { return session_; }

private:
    TabHoverHit hit_;
    bool session_ = false;
};

} // namespace window_system
