// SPDX-License-Identifier: MIT
#include "tooltip_layout.hpp"

#include <QEvent>
#include <QLabel>
#include <QScreen>
#include <QTextDocument>

#include <algorithm>

namespace window_system {
namespace {
constexpr auto balancedSizeProperty = "_windowSystemBalancedToolTipSize";
} // namespace

void balanceToolTipShape(QObject* watched, QEvent* event)
{
    // QTipLabel is Qt's private tooltip widget, a QLabel. Qt resizes it to
    // its single-line size whenever it shows a tip, and wraps on its own only
    // when that line would leave the screen.
    if (event->type() != QEvent::Resize || !watched->inherits("QTipLabel")) return;
    auto* tip = static_cast<QLabel*>(watched);
    if (tip->property(balancedSizeProperty).toSize() == tip->size()) return; // Our own resize.
    const QString text = tip->text();
    // Rich text and explicit line breaks keep the layout their author chose.
    if (Qt::mightBeRichText(text) || text.contains(QLatin1Char('\n'))) return;
    const auto margins = tip->contentsMargins();
    const int frame = margins.left() + margins.right();
    const int textWidth = tip->fontMetrics().horizontalAdvance(text);
    if (textWidth + frame <= singleLineToolTipWidth) return;
    const int line = std::max(1, tip->fontMetrics().lineSpacing());
    // Fewest lines whose width is either readable on its own or no more than
    // the aspect ratio allows for the resulting height.
    int lines = 2;
    while ((textWidth + lines - 1) / lines > singleLineToolTipWidth
           && (textWidth + lines - 1) / lines > maximumToolTipAspectRatio * lines * line) ++lines;
    // Word boundaries make wrapped lines uneven; leave room for a few more
    // characters per line rather than an extra line, and stay on the screen.
    int width = (textWidth + lines - 1) / lines + frame + tip->fontMetrics().averageCharWidth() * 4;
    if (const auto* screen = tip->screen()) width = std::min(width, screen->availableGeometry().width() * 9 / 10);
    tip->setWordWrap(true);
    const int height = tip->heightForWidth(width);
    if (height <= 0) return;
    tip->setProperty(balancedSizeProperty, QSize(width, height));
    tip->resize(width, height);
}

} // namespace window_system
