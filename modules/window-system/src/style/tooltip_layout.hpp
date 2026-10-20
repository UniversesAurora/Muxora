// SPDX-License-Identifier: MIT
#pragma once

class QEvent;
class QObject;

namespace window_system {

// The widest a tooltip grows, in logical pixels. Qt only wraps a tip when it
// would not fit on the screen; a long single line looks thin and stretched,
// so wider tips wrap at this width instead. Short tips keep one line.
inline constexpr int maximumToolTipWidth = 400;

// Applied to every tooltip label while Qt sizes it (new or reused), before
// Qt places it, so placement still keeps the tip on screen.
void limitToolTipWidth(QObject* watched, QEvent* event);

} // namespace window_system
