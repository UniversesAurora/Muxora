// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/theme.hpp>
#include <QRectF>
#include <QString>

class QQuickItem;

namespace window_system::quick {

// One hover tip at a time, as QToolTip shows them: after the wake-up delay,
// or at once while the previous tip was shown moments ago; below and right
// of the pointer, in its own popup window. An area, in the anchor's
// coordinates, limits the tip to part of the anchor. Empty text hides it.
void showToolTip(QQuickItem* anchor, const Theme& theme, const QString& text, QRectF area = {});
// Hides the anchor's tip; pressing or leaving the anchor does this.
void hideToolTip(QQuickItem* anchor);

} // namespace window_system::quick
