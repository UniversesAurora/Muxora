// SPDX-License-Identifier: MIT
#include "rail_button_item.hpp"
#include "path_item.hpp"
#include "tab_icon_item.hpp"
#include "tab_label_text_item.hpp"
#include "style/edge_tab_label.hpp"

#include <QFontMetrics>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainterPath>
#include <QStyleHints>
#include <algorithm>

namespace window_system::quick {

RailButtonItem::RailButtonItem(const Theme& theme, TabId tab, QString title, bool vertical, int thickness, QQuickItem* parent)
    : QQuickItem(parent), theme_(theme), tab_(tab), title_(std::move(title)), vertical_(vertical), thickness_(thickness),
      fill_(new PathItem(this)), line_(new PathItem(this)), content_(new QQuickItem(this)),
      numberText_(new TabLabelTextItem(content_)), iconItem_(new TabIconItem(content_)), titleText_(new TabLabelTextItem(content_))
{
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton | Qt::RightButton);
    setAcceptHoverEvents(true);
    line_->setZ(1); content_->setZ(2);
    // Vertical rails read top to bottom: the label turns a quarter clockwise.
    content_->setTransformOrigin(QQuickItem::TopLeft);
    if (vertical_) content_->setRotation(90);
    refresh();
}

void RailButtonItem::setTheme(const Theme& theme) { theme_ = theme; refresh(); }
void RailButtonItem::setThickness(int thickness) { if (thickness_ != thickness) { thickness_ = thickness; refresh(); } }
void RailButtonItem::setPreset(std::optional<TabColorPreset> preset)
{
    if (preset_ == preset) return;
    preset_ = std::move(preset); refresh();
}
void RailButtonItem::setPresentation(QIcon icon, int number)
{
    icon_ = std::move(icon); number_ = std::max(0, number); refresh();
}
void RailButtonItem::setTitleClipping(bool enabled, int maximumWidth)
{
    if (clipTitles_ == enabled && maximumTitleWidth_ == maximumWidth) return;
    clipTitles_ = enabled; maximumTitleWidth_ = maximumWidth; refresh();
}
void RailButtonItem::setChecked(bool checked) { if (checked_ != checked) { checked_ = checked; refresh(); } }

QSize RailButtonItem::preferredSize() const
{
    const auto font = QGuiApplication::font();
    titleLayout_.prepare(title_, font, QGuiApplication::layoutDirection(), false);
    const auto fitted = titleLayout_.fittedMetrics().size;
    const int thickness = std::max(thickness_, fitted.height() + uiPixels(4));
    const auto extent = edgeTabExtent(QFontMetrics(font), fitted.width(), !icon_.isNull(), number_, thickness,
                                      clipTitles_, maximumTitleWidth_);
    return vertical_ ? QSize(thickness, extent) : QSize(extent, thickness);
}

void RailButtonItem::geometryChange(const QRectF& now, const QRectF& previous)
{
    QQuickItem::geometryChange(now, previous);
    if (now.size() != previous.size()) refresh();
}

void RailButtonItem::refresh()
{
    const QRectF area(QPointF(), size());
    const auto colors = preset_ ? std::optional(checked_ ? preset_->autoHideActive : preset_->autoHideInactive) : std::nullopt;
    QPainterPath body; body.addRect(area);
    fill_->setSize(size());
    fill_->setFill(colors ? colors->background : hovered_ ? theme_.edgeTabHover : theme_.edgeTabBackground);
    fill_->setPath(body);
    line_->setVisible(bool(colors) && !area.isEmpty());
    if (colors && !area.isEmpty()) {
        QPainterPath line;
        line.addRect(vertical_ ? QRectF(area.width() - 2, 0, 2, area.height()) : QRectF(0, area.height() - 2, area.width(), 2));
        line_->setSize(size()); line_->setFill(colors->line); line_->setPath(line);
    }
    const QColor ink = colors ? colors->text : theme_.edgeTabText;
    // In the label's own frame: along the rail is x.
    const QSize frame = vertical_ ? QSize(int(area.height()), int(area.width())) : area.size().toSize();
    content_->setPosition(vertical_ ? QPointF(area.width(), 0) : QPointF());
    content_->setSize(frame);
    const auto font = QGuiApplication::font();
    const auto direction = QGuiApplication::layoutDirection();
    titleLayout_.prepare(title_, font, direction, false);
    const QFontMetrics metrics(font);
    const auto parts = edgeTabParts(metrics, titleLayout_.fittedMetrics().size.width(), !icon_.isNull(), number_,
                                    clipTitles_, maximumTitleWidth_);
    // The extent matches the label exactly; a longer button centers it and
    // a shorter one cuts the title.
    const int span = int(std::min<qint64>(std::max(0, frame.width() - 2 * uiPixels(edgeTabPadding)), tabLabelWidth(parts)));
    const QRect label(std::max(uiPixels(edgeTabPadding), (frame.width() - span) / 2), 0, span, frame.height());
    const auto rects = layoutTabLabel(label, parts, direction);
    const auto text = [&](TabLabelTextItem* item, const QRect& rect, const QString& value) {
        if (rect.isEmpty() || value.isEmpty()) { item->clearLabel(); item->setVisible(false); return; }
        TabLabelPresentation presentation;
        presentation.parts.tab = rect; presentation.parts.title = rect;
        presentation.state.shape = TabShape::Row; presentation.state.selected = true; presentation.state.title = value;
        presentation.font = font; presentation.direction = direction;
        presentation.naturalWidth = metrics.horizontalAdvance(value);
        TabStripLabelStyle style; style.palette.text = ink;
        item->setLabel(presentation, style, TabTextRole::Literal);
        item->setPosition(rect.topLeft()); item->setVisible(true);
    };
    text(numberText_, rects.number, number_ > 0 ? QString::number(number_) : QString());
    text(titleText_, rects.title, title_);
    iconItem_->setVisible(!rects.icon.isEmpty() && !icon_.isNull());
    if (iconItem_->isVisible()) {
        iconItem_->setPosition(rects.icon.topLeft());
        iconItem_->setIcon(icon_, rects.icon.size(), colors ? colors->icon : theme_.edgeTabText, true, checked_);
    }
}

void RailButtonItem::mousePressEvent(QMouseEvent* event)
{
    event->accept();
    if (event->button() == Qt::MiddleButton) { middlePressed_ = true; return; }
    if (event->button() == Qt::RightButton) { if (const auto callback = menu) callback(event->globalPosition().toPoint()); return; }
    pressed_ = true;
    start_ = DragStart::capture(event->globalPosition().toPoint(), event->position().toPoint(), QRect(QPoint(), size().toSize()));
}

void RailButtonItem::mouseMoveEvent(QMouseEvent* event)
{
    if (!pressed_ || !(event->buttons() & Qt::LeftButton)) return;
    const auto global = event->globalPosition().toPoint();
    if (!start_.crossedThreshold(global, QGuiApplication::styleHints()->startDragDistance())) return;
    pressed_ = false;
    if (const auto callback = drag) callback(start_.movedTo(global));
}

void RailButtonItem::mouseReleaseEvent(QMouseEvent* event)
{
    const bool inside = contains(event->position());
    if (event->button() == Qt::MiddleButton) {
        const bool pressed = std::exchange(middlePressed_, false);
        if (pressed && inside) if (const auto callback = close) callback();
        return;
    }
    if (event->button() != Qt::LeftButton || !std::exchange(pressed_, false)) return;
    if (inside) if (const auto callback = clicked) callback();
}

void RailButtonItem::hoverEnterEvent(QHoverEvent*) { hovered_ = true; refresh(); }
void RailButtonItem::hoverLeaveEvent(QHoverEvent*) { hovered_ = false; refresh(); }

} // namespace window_system::quick
