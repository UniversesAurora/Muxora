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

// Qt gives focus to a Qt::WheelFocus widget under the wheel before any event
// filter runs, which would activate a spin box and show its text cursor.
// Value controls are switched to Qt::StrongFocus when first polished: click
// and Tab still focus them, the wheel no longer does.
void removeValueControlWheelFocus(QObject* watched, QEvent* event);

} // namespace window_system
