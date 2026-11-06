// SPDX-License-Identifier: MIT
#include "text_bar_item.hpp"
#include "tab_label_text_item.hpp"
#include "tool_tip.hpp"
#include <window_system/ui_scale.hpp>
#include <QFontMetrics>
#include <QGuiApplication>

namespace window_system::quick {

TextBarItem::TextBarItem(TextBar bar, const Theme& theme, QQuickItem* parent)
    : QQuickItem(parent), bar_(std::move(bar)), theme_(theme), text_(new TabLabelTextItem(this))
{
    setAcceptHoverEvents(!bar_.toolTip.isEmpty());
    setProperty("text", bar_.text); // For accessibility and inspection.
    refresh();
}

void TextBarItem::setTheme(const Theme& theme)
{
    theme_ = theme;
    refresh();
}

void TextBarItem::geometryChange(const QRectF& now, const QRectF& previous)
{
    QQuickItem::geometryChange(now, previous);
    if (now.size() != previous.size()) refresh();
}

void TextBarItem::hoverEnterEvent(QHoverEvent*) { showToolTip(this, theme_, bar_.toolTip); }
void TextBarItem::hoverLeaveEvent(QHoverEvent*) { hideToolTip(this); }

void TextBarItem::refresh()
{
    font_ = QGuiApplication::font();
    const QFontMetrics metrics(font_);
    const int margin = uiPixels(6);
    setImplicitHeight(metrics.height());
    setImplicitWidth(metrics.horizontalAdvance(bar_.text) + 2 * margin);
    const QRect area(margin, 0, std::max(0, qRound(width()) - 2 * margin), qRound(height()));
    if (bar_.text.isEmpty() || area.isEmpty()) { text_->clearLabel(); return; }
    TabLabelPresentation label;
    label.parts.tab = area; label.parts.title = area;
    label.state.shape = TabShape::Row; label.state.selected = true;
    // Plain text: an & is a character, not an access key.
    label.state.title = QString(bar_.text).replace(QLatin1Char('&'), QStringLiteral("&&"));
    label.font = font_; label.naturalWidth = metrics.horizontalAdvance(bar_.text);
    TabStripLabelStyle style;
    style.palette.text = theme_.text;
    text_->setLabel(label, style);
    text_->setPosition(area.topLeft());
}

} // namespace window_system::quick
