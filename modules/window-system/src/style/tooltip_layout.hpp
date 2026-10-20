// SPDX-License-Identifier: MIT
#pragma once

class QEvent;
class QObject;

namespace window_system {

// Qt only wraps a tooltip when it would not fit on the screen, so a long text
// becomes one thin, stretched line. Tips up to this width (logical pixels)
// keep a single line.
inline constexpr int singleLineToolTipWidth = 400;
// Longer tips wrap into as few lines as keep the bubble no wider than this
// many times its height: the wrap width grows with the text, so a very long
// text stays a wide card instead of becoming a tall, narrow strip.
inline constexpr int maximumToolTipAspectRatio = 8;

// Applied to every tooltip label while Qt sizes it (new or reused), before
// Qt places it, so placement still keeps the tip on screen.
void balanceToolTipShape(QObject* watched, QEvent* event);

} // namespace window_system
