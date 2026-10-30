// SPDX-License-Identifier: MIT
#include "button_item.hpp"
#include "path_item.hpp"
#include "tab_icon_item.hpp"
#include "tab_label_text_item.hpp"
#include <window_system/ui_scale.hpp>

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainterPath>
#include <algorithm>

namespace window_system::quick {
namespace {
constexpr qreal splitRadius = 6;

QPainterPath outline(QRectF area, ButtonItem::Shape shape, qreal radius)
{
    QPainterPath path;
    if (shape == ButtonItem::Shape::Tool || radius <= 0) { path.addRect(area); return path; }
    // Rounded only on the outer side; the side facing the other part is square.
    const bool start = shape == ButtonItem::Shape::SplitStart;
    radius = std::min({radius, area.width() / 2, area.height() / 2});
    const qreal d = 2 * radius;
    path.moveTo(start ? area.right() : area.left(), area.top());
    if (start) {
        path.lineTo(area.left() + radius, area.top());
        path.arcTo(QRectF(area.left(), area.top(), d, d), 90, 90);
        path.lineTo(area.left(), area.bottom() - radius);
        path.arcTo(QRectF(area.left(), area.bottom() - d, d, d), 180, 90);
        path.lineTo(area.right(), area.bottom());
    } else {
        path.lineTo(area.right() - radius, area.top());
        path.arcTo(QRectF(area.right() - d, area.top(), d, d), 90, -90);
        path.lineTo(area.right(), area.bottom() - radius);
        path.arcTo(QRectF(area.right() - d, area.bottom() - d, d, d), 0, -90);
        path.lineTo(area.left(), area.bottom());
    }
    path.closeSubpath();
    return path;
}
} // namespace

ButtonItem::ButtonItem(const Theme& theme, QQuickItem* parent)
    : QQuickItem(parent), theme_(theme), fill_(new PathItem(this)), border_(new PathItem(this)),
      icon_(new TabIconItem(this)), text_(new TabLabelTextItem(this))
{
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setAcceptHoverEvents(true);
    border_->setZ(1); icon_->setZ(2); text_->setZ(2);
    refresh();
}

void ButtonItem::setTheme(const Theme& theme) { theme_ = theme; refresh(); }
void ButtonItem::setShape(Shape shape) { if (shape_ != shape) { shape_ = shape; refresh(); } }
void ButtonItem::setIcon(const QIcon& icon) { iconSource_ = icon; refresh(); }
void ButtonItem::setText(const QString& text, const QFont& font)
{
    if (label_ == text && font_ == font) return;
    label_ = text; font_ = font; refresh();
}
void ButtonItem::setIconRail(std::optional<int> inset, Qt::Edge edge)
{
    if (iconRail_ == inset && iconEdge_ == edge) return;
    iconRail_ = inset; iconEdge_ = edge; refresh();
}
void ButtonItem::setChecked(bool checked) { if (checked_ != checked) { checked_ = checked; refresh(); } }
void ButtonItem::setDown(bool down) { if (down_ != down) { down_ = down; refresh(); } }
void ButtonItem::setEmphasized(bool emphasized) { if (emphasized_ != emphasized) { emphasized_ = emphasized; refresh(); } }

void ButtonItem::geometryChange(const QRectF& now, const QRectF& previous)
{
    QQuickItem::geometryChange(now, previous);
    if (now.size() != previous.size()) refresh();
}

void ButtonItem::refresh()
{
    const QRectF area(QPointF(), size());
    const bool pressed = (pressed_ && hovered_) || down_ || checked_;
    const bool split = shape_ != Shape::Tool;
    QColor fill;
    if (split) fill = pressed ? theme_.selection : emphasized_ || hovered_ ? theme_.buttonHover : theme_.raisedSurface;
    else if (pressed) fill = theme_.selection;
    else if (hovered_) fill = theme_.buttonHover;
    fill_->setVisible(fill.isValid() && !area.isEmpty());
    if (fill_->isVisible()) {
        fill_->setSize(size()); fill_->setFill(fill);
        fill_->setPath(outline(area, shape_, uiPixelsF(splitRadius)));
    }
    // Tool buttons show a one-pixel accent frame while hovered or pressed.
    const bool framed = !split && (pressed || hovered_) && area.width() > 2 && area.height() > 2;
    border_->setVisible(framed);
    if (framed) {
        QPainterPath frame;
        frame.addRect(QRectF(0, 0, area.width(), 1));
        frame.addRect(QRectF(0, area.height() - 1, area.width(), 1));
        frame.addRect(QRectF(0, 1, 1, area.height() - 2));
        frame.addRect(QRectF(area.width() - 1, 1, 1, area.height() - 2));
        border_->setSize(size()); border_->setFill(theme_.accent); border_->setPath(frame);
    }
    const int extent = std::max(1, std::min({uiPixels(14), int(area.width()) - uiPixels(2), int(area.height()) - uiPixels(2)}));
    const QSize iconSize(extent, extent);
    QRect glyph;
    if (iconRail_) {
        const int center = iconEdge_ == Qt::RightEdge ? int(area.width()) - *iconRail_ : *iconRail_;
        glyph = QRect(center - extent / 2, (int(area.height()) - extent) / 2, extent, extent);
    } else if (label_.isEmpty()) {
        glyph = QRect((int(area.width()) - extent) / 2, (int(area.height()) - extent) / 2, extent, extent);
    } else {
        glyph = QRect(uiPixels(4), (int(area.height()) - extent) / 2, extent, extent);
    }
    icon_->setVisible(!iconSource_.isNull());
    if (!iconSource_.isNull()) {
        icon_->setPosition(glyph.topLeft());
        icon_->setIcon(iconSource_, iconSize, {}, isEnabled(), checked_);
    }
    if (label_.isEmpty() || area.isEmpty()) { text_->clearLabel(); return; }
    const bool right = iconRail_ && iconEdge_ == Qt::RightEdge;
    const QRect textArea = right ? QRect(2, 0, std::max(0, glyph.left() - 8), int(area.height()))
        : QRect(glyph.right() + 7, 0, std::max(0, int(area.width()) - glyph.right() - 9), int(area.height()));
    TabLabelPresentation label;
    label.parts.tab = textArea; label.parts.title = textArea;
    label.state.shape = TabShape::Row; label.state.selected = true; label.state.title = label_;
    label.font = font_; label.direction = right ? Qt::RightToLeft : Qt::LeftToRight;
    label.naturalWidth = QFontMetrics(font_).horizontalAdvance(label_);
    TabStripLabelStyle style;
    style.palette.text = isEnabled() ? theme_.text : theme_.mutedText;
    text_->setLabel(label, style, TabTextRole::Literal);
    text_->setPosition(textArea.topLeft());
}

void ButtonItem::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton) {
        event->accept();
        emit contextMenuRequested(event->globalPosition().toPoint());
        return;
    }
    pressed_ = true; event->accept(); refresh();
}

void ButtonItem::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !pressed_) return;
    pressed_ = false; event->accept();
    const bool inside = contains(event->position());
    if (inside && checkable_) checked_ = !checked_;
    refresh();
    if (inside) emit clicked();
}

void ButtonItem::mouseUngrabEvent() { pressed_ = false; refresh(); }

void ButtonItem::hoverEnterEvent(QHoverEvent*)
{
    hovered_ = true; refresh(); emit hoveredChanged(true);
}

void ButtonItem::hoverLeaveEvent(QHoverEvent*)
{
    hovered_ = false; refresh(); emit hoveredChanged(false);
}

} // namespace window_system::quick
