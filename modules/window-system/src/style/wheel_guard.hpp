// SPDX-License-Identifier: MIT
#pragma once

class QEvent;
class QObject;

namespace window_system {

// Value controls (combo boxes, spin, date and time boxes, sliders) never
// change their value from a mouse wheel: scrolling a page over one must not
// edit a setting. The wheel goes to the control's parent instead, so the
// surrounding scroll area keeps scrolling. Scroll bars and open popups are
// unaffected. Returns true when the event was redirected.
bool redirectValueControlWheel(QObject* watched, QEvent* event);

} // namespace window_system
