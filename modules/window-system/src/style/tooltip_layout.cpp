// SPDX-License-Identifier: MIT
#include "tooltip_layout.hpp"

#include <QEvent>
#include <QLabel>

namespace window_system {

void limitToolTipWidth(QObject* watched, QEvent* event)
{
    // QTipLabel is Qt's private tooltip widget, a QLabel.
    if (event->type() != QEvent::Resize || !watched->inherits("QTipLabel")) return;
    auto* tip = static_cast<QLabel*>(watched);
    if (tip->width() <= maximumToolTipWidth) return;
    // Qt resets word wrap whenever it sizes a reused tip; wrap again here.
    tip->setWordWrap(true);
    const int height = tip->heightForWidth(maximumToolTipWidth);
    if (height > 0) tip->resize(maximumToolTipWidth, height);
}

} // namespace window_system
