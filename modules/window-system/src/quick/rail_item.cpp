// SPDX-License-Identifier: MIT
#include "rail_item.hpp"
#include "button_item.hpp"
#include "path_item.hpp"
#include "rail_button_item.hpp"
#include "style/frame_motion.hpp"
#include <window_system/ui_scale.hpp>

#include <QPainterPath>
#include <QQuickWindow>
#include <QWheelEvent>
#include <algorithm>

namespace window_system::quick {
namespace {
constexpr int spacing = 2;
constexpr int scrollbarThickness = 4;
constexpr int hoverMargin = 24;
// The same bound as maximumExtent, without depending on Widgets.
constexpr int maximumExtent = (1 << 24) - 1;
} // namespace

RailItem::RailItem(Edge edge, const Theme& theme, QQuickItem* parent)
    : QQuickItem(parent), edge_(edge), vertical_(edge == Edge::Left || edge == Edge::Right), theme_(theme),
      viewport_(new QQuickItem(this)), track_(new QQuickItem(viewport_)), thumb_(new PathItem(this))
{
    setAcceptHoverEvents(true);
    viewport_->setClip(true);
    thumb_->setZ(2); thumb_->setVisible(false);
    clock_.start();
    autoScroll_.setInterval(200);
    connect(&autoScroll_, &QTimer::timeout, this, [this] {
        const int old = offset_;
        setOffset(offset_ + scrollDirection_ * 12);
        if (offset_ == old) autoScroll_.stop(); else autoScroll_.setInterval(35);
    });
    layoutTrack();
}

void RailItem::setTheme(const Theme& theme)
{
    theme_ = theme;
    for (auto* button : std::as_const(buttons_)) button->setTheme(theme);
    if (tail_) tail_->setTheme(theme);
    layoutTrack();
}

void RailItem::addButton(RailButtonItem* button)
{
    if (!button || buttons_.contains(button)) return;
    button->setParentItem(track_);
    buttons_.append(button);
    connect(button, &QObject::destroyed, this, [this, button] { buttons_.removeAll(button); layoutTrack(); });
    layoutTrack();
}

void RailItem::setTail(ButtonItem* tail)
{
    if (tail_ == tail) return;
    if (tail_) tail_->setVisible(false);
    tail_ = tail;
    if (tail) tail->setParentItem(this);
    layoutTrack();
}

int RailItem::viewportExtent() const { return int(vertical_ ? viewport_->height() : viewport_->width()); }
int RailItem::maximumOffset() const { return std::max(0, trackExtent_ - viewportExtent()); }

void RailItem::layoutTrack()
{
    int thickness = std::max(1, theme_.metrics.edgeThickness);
    for (auto* button : std::as_const(buttons_)) {
        const auto preferred = button->preferredSize();
        thickness = std::max(thickness, vertical_ ? preferred.width() : preferred.height());
    }
    const bool thicker = thickness != thickness_;
    thickness_ = thickness;
    const int length = int(vertical_ ? height() : width());
    const int gapSize = uiPixels(spacing);
    // The all-pin action owns the trailing square, outside the scrolling track.
    int viewportLength = length;
    if (tail_) {
        tail_->setVisible(true);
        tail_->setSize(QSizeF(thickness, thickness));
        tail_->setPosition(vertical_ ? QPointF(0, length - thickness) : QPointF(length - thickness, 0));
        viewportLength = std::max(0, length - thickness - gapSize);
    }
    viewport_->setPosition({});
    viewport_->setSize(vertical_ ? QSizeF(thickness, viewportLength) : QSizeF(viewportLength, thickness));
    int position = 0, placed = 0, visible = 0;
    gap_ = {};
    const auto advance = [&](int extent) { position += extent + gapSize; ++placed; };
    for (auto* button : std::as_const(buttons_)) {
        if (button == dragged_) { button->setVisible(false); continue; }
        if (visible == previewIndex_) {
            gap_ = vertical_ ? QRect(0, position, thickness, previewExtent_) : QRect(position, 0, previewExtent_, thickness);
            advance(previewExtent_);
        }
        const auto preferred = button->preferredSize();
        const int extent = std::clamp(vertical_ ? preferred.height() : preferred.width(), 1, maximumExtent);
        button->setThickness(thickness);
        button->setPosition(vertical_ ? QPointF(0, position) : QPointF(position, 0));
        button->setSize(vertical_ ? QSizeF(thickness, extent) : QSizeF(extent, thickness));
        button->setVisible(true);
        advance(extent); ++visible;
    }
    if (previewIndex_ >= 0 && previewIndex_ == visible) {
        gap_ = vertical_ ? QRect(0, position, thickness, previewExtent_) : QRect(position, 0, previewExtent_, thickness);
        advance(previewExtent_);
    }
    trackExtent_ = std::max(0, position - (placed ? gapSize : 0));
    track_->setSize(vertical_ ? QSizeF(thickness, std::max(trackExtent_, viewportLength))
                              : QSizeF(std::max(trackExtent_, viewportLength), thickness));
    reorderLayoutValid_ = false;
    setOffset(offset_);
    setImplicitSize(vertical_ ? thickness : implicitWidth(), vertical_ ? implicitHeight() : thickness);
    if (thicker) if (const auto callback = thicknessChanged) callback();
}

void RailItem::setOffset(int offset)
{
    offset = std::clamp(offset, 0, maximumOffset());
    offset_ = offset;
    track_->setPosition(vertical_ ? QPointF(0, -offset) : QPointF(-offset, 0));
    if (maximumOffset() == 0) { autoScroll_.stop(); scrollDirection_ = 0; }
    syncScrollbar();
    if (const auto callback = hoverGeometryChanged) callback();
}

void RailItem::syncScrollbar()
{
    const int extent = viewportExtent(), maximum = maximumOffset();
    const bool shown = isVisible() && maximum > 0 && (hovered_ || scrollDirection_);
    thumb_->setVisible(shown);
    if (!shown || trackExtent_ <= 0) return;
    const int bar = uiPixels(scrollbarThickness);
    // As the Widgets overlay scrollbar: a square gray thumb, at least 18 long.
    const qreal length = std::min<qreal>(extent, std::max<qreal>(uiPixels(18), qreal(extent) * extent / trackExtent_));
    const qreal start = (extent - length) * offset_ / maximum;
    QPainterPath path;
    path.addRect(vertical_ ? QRectF(viewport_->width() - bar, start, bar, length) : QRectF(start, viewport_->height() - bar, length, bar));
    const int gray = qGray(theme_.border.rgb());
    thumb_->setSize(size()); thumb_->setFill(QColor(gray, gray, gray, 144)); thumb_->setPath(path);
}

QRect RailItem::viewportRectGlobal() const
{
    if (!isVisible()) return {};
    return QRect(viewport_->mapToGlobal(QPointF()).toPoint(), viewport_->size().toSize());
}

void RailItem::rebuildReorderLayout() const
{
    reorderInput_ = {}; reorderLayout_ = {};
    reorderLayoutValid_ = true;
    if (previewIndex_ < 0) return;
    reorderInput_.orientation = vertical_ ? TabLayoutInput::Orientation::Vertical : TabLayoutInput::Orientation::Horizontal;
    reorderInput_.width = int(track_->width()); reorderInput_.height = int(track_->height());
    reorderInput_.rowHeight = thickness_; reorderInput_.rowGap = uiPixels(spacing); reorderInput_.insertion = -1;
    for (auto* button : buttons_) if (button != dragged_) {
        const QRect rect(button->position().toPoint(), button->size().toSize());
        reorderInput_.extents.append(vertical_ ? rect.height() : rect.width());
        reorderLayout_.order.append(int(reorderLayout_.tabs.size()));
        reorderLayout_.tabs.append(rect); reorderLayout_.lanes.append(0);
    }
    reorderLayout_.size = track_->size().toSize();
    TabLayoutLane lane;
    lane.viewport = QRect(QPoint(), track_->size().toSize()); lane.extent = trackExtent_; lane.vertical = vertical_;
    reorderLayout_.laneList.append(lane);
    if (!gap_.isEmpty()) {
        reorderInput_.insertion = previewIndex_; reorderInput_.block = {previewExtent_};
        reorderLayout_.gap.append(gap_); reorderLayout_.gapLane = 0;
    }
}

int RailItem::insertionIndex(QPoint global, QRect carriedGlobal) const
{
    const auto viewport = viewportRectGlobal();
    if (!viewport.contains(global)) return -1;
    if (previewIndex_ >= 0 && !carriedGlobal.isEmpty()) {
        if (!reorderLayoutValid_) rebuildReorderLayout();
        if (!reorderLayout_.gap.isEmpty()) {
            const auto visible = carriedGlobal.intersected(viewport);
            if (visible.isEmpty()) return reorderInput_.insertion;
            const auto origin = track_->mapToGlobal(QPointF()).toPoint();
            return tabReorderAt(reorderInput_, reorderLayout_, visible.translated(-origin), global - origin).index;
        }
    }
    // A gap captures its own index, so the shifted neighbor cannot make a
    // stationary pointer alternate between two slots.
    if (previewIndex_ >= 0 && insertionPreviewRect().contains(global)) return previewIndex_;
    const int position = axis(track_->mapFromGlobal(QPointF(global)));
    int index = 0;
    for (auto* button : buttons_) if (button != dragged_) {
        if (position < axis(button->position() + QPointF(button->width() / 2, button->height() / 2))) return index;
        ++index;
    }
    return index;
}

void RailItem::setInsertionPreview(int index, int extent)
{
    int shown = 0;
    for (auto* button : std::as_const(buttons_)) if (button != dragged_) ++shown;
    const bool valid = index >= 0 && index <= shown && extent > 0;
    const int nextIndex = valid ? index : -1, nextExtent = valid ? std::min(extent, maximumExtent) : 0;
    if (nextIndex == previewIndex_ && nextExtent == previewExtent_) return;
    previewIndex_ = nextIndex; previewExtent_ = nextExtent;
    layoutTrack();
}

QRect RailItem::insertionPreviewRect() const
{
    if (previewIndex_ < 0 || gap_.isEmpty()) return {};
    return QRect(track_->mapToGlobal(QPointF(gap_.topLeft())).toPoint(), gap_.size()).intersected(viewportRectGlobal());
}

void RailItem::setDraggedTab(TabId tab)
{
    dragged_ = nullptr;
    for (auto* button : std::as_const(buttons_)) if (!tab.isNull() && button->tab() == tab) dragged_ = button;
    layoutTrack();
}

void RailItem::setDragHover(QPoint global)
{
    const auto viewport = viewportRectGlobal();
    int direction = 0;
    if (maximumOffset() > 0 && viewport.contains(global)) {
        const int position = vertical_ ? global.y() : global.x();
        const int first = vertical_ ? viewport.top() : viewport.left();
        const int last = vertical_ ? viewport.bottom() : viewport.right();
        direction = position < first + uiPixels(hoverMargin) ? -1 : position > last - uiPixels(hoverMargin) ? 1 : 0;
        if ((direction < 0 && offset_ == 0) || (direction > 0 && offset_ == maximumOffset())) direction = 0;
    }
    if (direction != scrollDirection_) autoScroll_.stop();
    scrollDirection_ = direction;
    if (!direction) autoScroll_.stop();
    else if (!autoScroll_.isActive()) { autoScroll_.setInterval(200); autoScroll_.start(); }
    syncScrollbar();
}

void RailItem::restoreScrollOffset(int offset)
{
    motion_.stopAt(offset_);
    setOffset(offset);
}

RailItem::ScrollAnchor RailItem::scrollAnchor() const
{
    ScrollAnchor anchor{offset_, {}, 0, false};
    const int extent = viewportExtent();
    RailButtonItem* leading = nullptr;
    for (auto* button : buttons_) {
        if (button == dragged_) continue;
        const int start = axis(button->position());
        const int end = start + int(vertical_ ? button->height() : button->width());
        if (button->isChecked() && start >= anchor.offset && end <= anchor.offset + extent) {
            anchor.tab = button->tab(); anchor.shift = start - anchor.offset; anchor.active = true;
            return anchor;
        }
        if (!leading && end > anchor.offset) leading = button;
    }
    if (leading) { anchor.tab = leading->tab(); anchor.shift = axis(leading->position()) - anchor.offset; }
    return anchor;
}

void RailItem::restoreScrollAnchor(const ScrollAnchor& anchor)
{
    int offset = anchor.offset;
    layoutTrack();
    for (auto* button : std::as_const(buttons_)) {
        if (anchor.tab.isNull() || button->tab() != anchor.tab) continue;
        const int start = axis(button->position());
        const int length = int(vertical_ ? button->height() : button->width());
        offset = start - std::max(anchor.shift, -std::max(0, length - 1));
        const int extent = viewportExtent();
        if (anchor.active && extent > 0)
            offset = extent >= length ? qBound(start + length - extent, offset, start) : start;
        break;
    }
    restoreScrollOffset(offset);
}

void RailItem::scrollByWheel(QPoint delta, bool smooth)
{
    const int distance = vertical_ ? (delta.y() ? delta.y() : delta.x()) : (delta.x() ? delta.x() : delta.y());
    if (!smooth || !motionAnimationsEnabled() || !window()) { motion_.stopAt(offset_); setOffset(offset_ - distance); return; }
    motion_.scrollBy(-distance, offset_, 0, maximumOffset(), clock_.nsecsElapsed());
    scheduleMotion();
}

void RailItem::scheduleMotion()
{
    if (motion_.needsFrame() && window() && !frameConnection_) {
        frameConnection_ = connect(window(), &QQuickWindow::afterAnimating, this, [this] {
            if (motion_.advance(clock_.nsecsElapsed())) setOffset(qRound(motion_.value()));
            if (!motion_.needsFrame()) { disconnect(frameConnection_); frameConnection_ = {}; }
            else window()->update();
        });
        window()->update();
    }
}

void RailItem::geometryChange(const QRectF& now, const QRectF& previous)
{
    QQuickItem::geometryChange(now, previous);
    if (now.size() != previous.size()) layoutTrack();
}

void RailItem::wheelEvent(QWheelEvent* event)
{
    const auto delta = event->pixelDelta().isNull() ? event->angleDelta() / 3 : event->pixelDelta();
    scrollByWheel(delta, event->pixelDelta().isNull());
    event->accept();
}

void RailItem::hoverEnterEvent(QHoverEvent*) { hovered_ = true; syncScrollbar(); }
void RailItem::hoverLeaveEvent(QHoverEvent*) { hovered_ = false; syncScrollbar(); }

} // namespace window_system::quick
