#include <window_system/interface_appearance.hpp>
// SPDX-License-Identifier: MIT
#include "edge_strip.hpp"
#include "style/themed_controls.hpp"
#include "edge_tab_button.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QBoxLayout>
#include <QEvent>
#include <QHideEvent>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>

namespace window_system {
namespace {
constexpr int spacing = 2;
constexpr int scrollbarThickness = 4;
constexpr int hoverMargin = 24;
}

void EdgeStrip::setTheme(const Theme& theme)
{
    theme_ = theme;
    const int thickness = std::max(1, theme_.metrics.edgeThickness);
    if (vertical_) setFixedWidth(thickness); else setFixedHeight(thickness);
    layout()->setSpacing(uiPixels(spacing)); trackLayout_->setSpacing(uiPixels(spacing));
    for (const auto& button : buttons_) if (auto* tab = dynamic_cast<EdgeTabButton*>(button.data())) tab->setThickness(thickness);
    scrollbar_->setStyleSheet(overlayScrollbarStyle(theme_));
    layoutTrack(); updateScrollBarGeometry(); update();
}

EdgeStrip::EdgeStrip(Edge edge, const Theme& theme, QWidget* parent)
    : QFrame(parent), vertical_(edge == Edge::Left || edge == Edge::Right), theme_(theme),
      scroll_(new QScrollArea(this)), track_(new QWidget),
      trackLayout_(new QBoxLayout(vertical_ ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight, track_)),
      scrollbar_(new QScrollBar(vertical_ ? Qt::Vertical : Qt::Horizontal, scroll_->viewport())),
      gap_(new QWidget(track_)), autoScroll_(new QTimer(this))
{
    setObjectName("EdgeBar"); setProperty("edge", int(edge)); setFrameShape(QFrame::NoFrame);
    const int thickness = std::max(1, theme_.metrics.edgeThickness);
    if (vertical_) setFixedWidth(thickness); else setFixedHeight(thickness);
    auto* column = new QBoxLayout(vertical_ ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight, this);
    column->setContentsMargins(0, 0, 0, 0); column->setSpacing(uiPixels(spacing));
    scroll_->setObjectName("EdgeScrollViewport"); scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setProperty("windowSystemOwnWheelScrolling", true);
    scroll_->setWidgetResizable(false);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    track_->setObjectName("EdgeTabTrack");
    trackLayout_->setContentsMargins(0, 0, 0, 0); trackLayout_->setSpacing(uiPixels(spacing));
    trackLayout_->setSizeConstraint(QLayout::SetNoConstraint); trackLayout_->addStretch();
    gap_->setObjectName("EdgeInsertionPreview");
    gap_->setAttribute(Qt::WA_TransparentForMouseEvents); gap_->setFocusPolicy(Qt::NoFocus);
    // An empty gap, as in tab strips: no placeholder block (tab strip design 6.1).
    gap_->setStyleSheet(QStringLiteral("QWidget#EdgeInsertionPreview { background: transparent; border: none; }"));
    gap_->hide();
    scroll_->setWidget(track_); column->addWidget(scroll_, 1);
    // QScrollArea::setWidget enables background filling on its content.
    // Keep every intermediate layer transparent to the shared backplate,
    // including the card shadows which extend into an empty sidebar.
    track_->setAutoFillBackground(false);
    scroll_->setAutoFillBackground(false);
    scroll_->viewport()->setAutoFillBackground(false);

    scrollbar_->setObjectName("EdgeScrollBar"); scrollbar_->setSingleStep(28);
    // The overlay is not managed by QScrollArea. A new QScrollBar otherwise
    // keeps its default 0..99 range when the real range never changes from 0.
    scrollbar_->setRange(0, 0);
    scrollbar_->setStyleSheet(overlayScrollbarStyle(theme_));
    scrollbar_->hide();
    observe(scroll_->viewport()); observe(track_); observe(scrollbar_);
    autoScroll_->setObjectName("EdgeAutoScrollTimer"); autoScroll_->setInterval(200);
    connect(autoScroll_, &QTimer::timeout, this, [this] {
        pendingScrollOffset_.reset();
        auto* bar = axisScrollBar(); const int old = bar->value();
        bar->setValue(old + scrollDirection_ * 12);
        if (bar->value() == old) autoScroll_->stop();
        else autoScroll_->setInterval(35);
    });
    scrollMotion_.applyValue = [this](qreal value) {
        if (!smoothScrollAnimationsAllowed(this)) { scrollMotion_.stopAt(axisScrollBar()->value()); return; }
        const QPointer<EdgeStrip> self(this);
        applyingScrollMotion_ = true;
        axisScrollBar()->setValue(qRound(value));
        if (self) applyingScrollMotion_ = false;
    };
    connect(axisScrollBar(), &QScrollBar::rangeChanged, this, [this] {
        scrollMotion_.stopAt(axisScrollBar()->value()); applyPendingScrollOffset(); synchronizeScrollBar();
    });
    connect(axisScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (!applyingScrollMotion_) scrollMotion_.stopAt(value);
        scrollbar_->setValue(value); queueHoverGeometryChanged();
    });
    connect(scrollbar_, &QScrollBar::valueChanged, axisScrollBar(), &QScrollBar::setValue);
    connect(scrollbar_, &QScrollBar::sliderPressed, this, [this] { scrollMotion_.stopAt(axisScrollBar()->value()); });
    connect(scrollbar_, &QScrollBar::sliderMoved, this, [this] { scrollMotion_.stopAt(axisScrollBar()->value()); pendingScrollOffset_.reset(); });
    connect(scrollbar_, &QScrollBar::actionTriggered, this, [this] { scrollMotion_.stopAt(axisScrollBar()->value()); pendingScrollOffset_.reset(); });
    layoutTrack();
    connect(qApp, &QGuiApplication::fontDatabaseChanged, this, [this] { queueTrackLayout(); });
}

EdgeStrip::~EdgeStrip()
{
    disconnect(qApp, nullptr, this, nullptr);
    autoScroll_->stop();
    // Child destruction can emit signals after member storage is gone.
    for (auto* object : findChildren<QObject*>()) {
        object->removeEventFilter(this); disconnect(object, nullptr, this, nullptr);
    }
}

void EdgeStrip::observe(QWidget* widget)
{
    widget->installEventFilter(this); widget->setMouseTracking(true);
    connect(widget, &QObject::destroyed, this, [this, widget] {
        hoveredSurfaces_.remove(widget); queueTrackLayout();
    });
}

QList<QAbstractButton*> EdgeStrip::buttons() const
{
    QList<QAbstractButton*> result;
    for (const auto& button : buttons_) if (button) result.append(button);
    return result;
}
void EdgeStrip::addButton(QAbstractButton* button)
{
    if (!button || std::any_of(buttons_.cbegin(), buttons_.cend(), [button](const auto& existing) { return existing == button; })) return;
    button->setParent(track_); buttons_.append(button); observe(button);
    trackLayout_->insertWidget(trackLayout_->count() - 1, button); button->show(); layoutTrack();
}

void EdgeStrip::setTailWidget(QWidget* widget)
{
    if (tail_ == widget) return;
    if (tail_) { layout()->removeWidget(tail_); tail_->hide(); }
    tail_ = widget;
    if (widget) {
        // The all-pin action belongs to the rail, not the scrolling label
        // track. It remains reachable at the trailing edge at every offset.
        widget->setParent(this); observe(widget);
        const int thickness = std::max(1, theme_.metrics.edgeThickness);
        widget->setFixedSize(thickness, thickness);
        static_cast<QBoxLayout*>(layout())->addWidget(widget); widget->show();
    }
    layout()->activate();
    layoutTrack();
}

int EdgeStrip::axis(QPoint point) const { return vertical_ ? point.y() : point.x(); }
QScrollBar* EdgeStrip::axisScrollBar() const
{
    return vertical_ ? scroll_->verticalScrollBar() : scroll_->horizontalScrollBar();
}
QRect EdgeStrip::viewportRectGlobal() const
{
    return isVisible() ? QRect(scroll_->viewport()->mapToGlobal(QPoint()), scroll_->viewport()->size()) : QRect();
}
int EdgeStrip::scrollOffset() const { return pendingScrollOffset_.value_or(axisScrollBar()->value()); }
void EdgeStrip::applyPendingScrollOffset()
{
    if (pendingScrollOffset_) axisScrollBar()->setValue(*pendingScrollOffset_);
}
void EdgeStrip::restoreScrollOffset(int offset)
{
    scrollMotion_.stopAt(axisScrollBar()->value());
    pendingScrollOffset_ = qMax(0, offset);
    applyPendingScrollOffset();
    QTimer::singleShot(0, this, [this] { applyPendingScrollOffset(); pendingScrollOffset_.reset(); });
}
EdgeStrip::ScrollAnchor EdgeStrip::scrollAnchor() const
{
    ScrollAnchor anchor{scrollOffset(), {}, 0, false};
    const int extent = vertical_ ? scroll_->viewport()->height() : scroll_->viewport()->width();
    QAbstractButton* leading = nullptr;
    for (const auto& button : buttons_) {
        if (!button || button == draggedButton_) continue;
        const int start = axis(button->pos());
        const int end = start + (vertical_ ? button->height() : button->width());
        if (button->isChecked() && start >= anchor.offset && end <= anchor.offset + extent) {
            anchor.tab = button->property("tabId").toUuid(); anchor.shift = start - anchor.offset; anchor.active = true;
            return anchor;
        }
        if (!leading && end > anchor.offset) leading = button;
    }
    if (leading) { anchor.tab = leading->property("tabId").toUuid(); anchor.shift = axis(leading->pos()) - anchor.offset; }
    return anchor;
}
void EdgeStrip::restoreScrollAnchor(const ScrollAnchor& anchor)
{
    int offset = anchor.offset;
    if (!anchor.tab.isNull()) {
        // Lay out the new buttons now; the anchor needs their real positions.
        layoutTrack();
        for (const auto& button : buttons_) {
            if (!button || button->property("tabId").toUuid() != anchor.tab) continue;
            const int start = axis(button->pos());
            const int length = vertical_ ? button->height() : button->width();
            offset = start - std::max(anchor.shift, -std::max(0, length - 1));
            const int extent = vertical_ ? scroll_->viewport()->height() : scroll_->viewport()->width();
            if (anchor.active && extent > 0)
                offset = extent >= length ? qBound(start + length - extent, offset, start) : start;
            break;
        }
    }
    restoreScrollOffset(offset);
}
void EdgeStrip::scrollByWheel(QPoint delta, bool smooth)
{
    pendingScrollOffset_.reset();
    const int distance = vertical_ ? (delta.y() ? delta.y() : delta.x()) : (delta.x() ? delta.x() : delta.y());
    auto* bar = axisScrollBar();
    if (!smooth || !smoothScrollAnimationsAllowed(this)) {
        scrollMotion_.stopAt(bar->value()); bar->setValue(bar->value() - distance); return;
    }
    scrollMotion_.scrollBy(-distance, bar->value(), bar->minimum(), bar->maximum());
}

int EdgeStrip::insertionIndex(QPoint global) const
{
    if (!viewportRectGlobal().contains(global)) return -1;
    // A gap captures its own index before midpoint testing, so the shifted
    // neighbour cannot make a stationary cursor alternate between two slots.
    if (previewIndex_ >= 0 && insertionPreviewRect().contains(global)) return previewIndex_;
    const int position = axis(track_->mapFromGlobal(global));
    int index = 0;
    for (const auto& button : buttons_) if (button && button != draggedButton_) {
        if (position < axis(button->geometry().center())) return index;
        ++index;
    }
    return index;
}

QRect EdgeStrip::insertionMarkerRect(int index) const
{
    if (index < 0 || index > buttons_.size()) return {};
    int position = 0;
    if (index == previewIndex_) position = axis(gap_->pos());
    else if (index < buttons_.size() && buttons_[index]) position = axis(buttons_[index]->pos());
    else if (!buttons_.isEmpty() && buttons_.last()) {
        const auto bounds = buttons_.last()->geometry();
        position = vertical_ ? bounds.bottom() + 1 : bounds.right() + 1;
    }
    const auto viewport = viewportRectGlobal();
    if (viewport.isEmpty()) return {};
    const auto origin = track_->mapToGlobal(vertical_ ? QPoint(0, position) : QPoint(position, 0));
    const int coordinate = axis(origin);
    const int first = vertical_ ? viewport.top() : viewport.left();
    const int last = vertical_ ? viewport.bottom() : viewport.right();
    if (coordinate < first || coordinate > last) return {};
    const int center = qBound(first + 2, coordinate, std::max(first + 2, last - 1));
    return vertical_ ? QRect(viewport.left(), center - 2, viewport.width(), 4)
                     : QRect(center - 2, viewport.top(), 4, viewport.height());
}

void EdgeStrip::setInsertionPreview(int index, int extent)
{
    QList<QAbstractButton*> visible;
    for (const auto& button : buttons_) if (button && button != draggedButton_) visible.append(button);
    const bool valid = index >= 0 && index <= visible.size() && extent > 0;
    const int nextIndex = valid ? index : -1;
    const int nextExtent = valid ? std::min(extent, QWIDGETSIZE_MAX) : 0;
    if (nextIndex == previewIndex_ && nextExtent == previewExtent_) return;
    trackLayout_->removeWidget(gap_); gap_->hide();
    previewIndex_ = nextIndex; previewExtent_ = nextExtent;
    if (valid) {
        const int insertion = index < visible.size() ? trackLayout_->indexOf(visible[index])
            : trackLayout_->count() - 1;
        trackLayout_->insertWidget(insertion, gap_); gap_->show();
    }
    layoutTrack();
}
void EdgeStrip::setDraggedTab(TabId tab)
{
    if (draggedButton_) draggedButton_->show();
    draggedButton_.clear();
    for (const auto& button : buttons_) if (button && !tab.isNull() && button->property("tabId").toUuid() == tab) {
        draggedButton_ = button; button->hide(); break;
    }
    layoutTrack();
}

QRect EdgeStrip::insertionPreviewRect() const
{
    if (previewIndex_ < 0 || gap_->isHidden()) return {};
    return QRect(gap_->mapToGlobal(QPoint()), gap_->size()).intersected(viewportRectGlobal());
}

void EdgeStrip::layoutTrack()
{
    if (arranging_) return;
    QScopedValueRollback guard(arranging_, true);
    int thickness = std::max(1, theme_.metrics.edgeThickness);
    for (const auto& button : buttons_) if (button) {
        const auto hint = button->sizeHint();
        thickness = std::max(thickness, vertical_ ? hint.width() : hint.height());
    }
    if (vertical_) setFixedWidth(thickness); else setFixedHeight(thickness);
    qint64 total = 0; int count = 0;
    for (const auto& button : buttons_) if (button && button != draggedButton_) {
        const auto hint = button->sizeHint();
        const int extent = std::clamp(vertical_ ? hint.height() : hint.width(), 1, QWIDGETSIZE_MAX);
        button->setFixedSize(vertical_ ? QSize(thickness, extent) : QSize(extent, thickness));
        total += extent; ++count;
    }
    if (tail_) tail_->setFixedSize(thickness, thickness);
    if (previewIndex_ >= 0) {
        gap_->setFixedSize(vertical_ ? QSize(thickness, previewExtent_) : QSize(previewExtent_, thickness));
        total += previewExtent_; ++count;
    }
    total += std::max(0, count - 1) * uiPixels(spacing);
    const int available = vertical_ ? scroll_->viewport()->height() : scroll_->viewport()->width();
    const int extent = int(std::clamp<qint64>(std::max<qint64>(total, available), 1, QWIDGETSIZE_MAX));
    track_->resize(vertical_ ? QSize(thickness, extent) : QSize(extent, thickness));
    trackLayout_->activate();
    updateScrollBarGeometry(); synchronizeScrollBar();
}

void EdgeStrip::updateScrollBarGeometry()
{
    const auto area = scroll_->viewport()->rect();
    scrollbar_->setGeometry(vertical_ ? QRect(std::max(0, area.width() - uiPixels(scrollbarThickness)), 0, uiPixels(scrollbarThickness), area.height())
        : QRect(0, std::max(0, area.height() - uiPixels(scrollbarThickness)), area.width(), uiPixels(scrollbarThickness)));
    scrollbar_->raise();
}
void EdgeStrip::synchronizeScrollBar()
{
    const auto* source = axisScrollBar();
    const QSignalBlocker blocker(scrollbar_);
    scrollbar_->setRange(source->minimum(), source->maximum());
    scrollbar_->setPageStep(vertical_ ? scroll_->viewport()->height() : scroll_->viewport()->width());
    scrollbar_->setValue(source->value());
    if (source->maximum() == source->minimum()) {
        autoScroll_->stop(); scrollDirection_ = 0;
    }
    updateScrollBarVisibility();
}
void EdgeStrip::updateScrollBarVisibility()
{
    const auto* source = axisScrollBar();
    scrollbar_->setVisible(isVisible() && source->maximum() > source->minimum() &&
        (!hoveredSurfaces_.isEmpty() || scrollDirection_));
    if (scrollbar_->isVisible()) scrollbar_->raise();
}
void EdgeStrip::queueTrackLayout()
{
    if (trackLayoutQueued_) return;
    trackLayoutQueued_ = true;
    QTimer::singleShot(0, this, [this] {
        trackLayoutQueued_ = false;
        int removedBeforePreview = 0;
        for (int index = 0; index < buttons_.size() && index < previewIndex_; ++index)
            if (!buttons_[index]) ++removedBeforePreview;
        buttons_.removeIf([](const auto& button) { return button.isNull(); });
        if (previewIndex_ >= 0) previewIndex_ -= removedBeforePreview;
        layoutTrack(); queueHoverGeometryChanged();
    });
}
void EdgeStrip::queueHoverGeometryChanged()
{
    if (!hoverGeometryChanged || hoverNotificationQueued_) return;
    hoverNotificationQueued_ = true;
    QTimer::singleShot(0, this, [this] {
        hoverNotificationQueued_ = false;
        const auto callback = hoverGeometryChanged; if (callback) callback();
    });
}

void EdgeStrip::setDragHover(QPoint global)
{
    const auto viewport = viewportRectGlobal();
    int nextDirection = 0;
    const auto* bar = axisScrollBar();
    if (bar->maximum() > bar->minimum() && viewport.contains(global)) {
        const int position = axis(global);
        const int first = vertical_ ? viewport.top() : viewport.left();
        const int last = vertical_ ? viewport.bottom() : viewport.right();
        nextDirection = position < first + uiPixels(hoverMargin) ? -1 : position > last - uiPixels(hoverMargin) ? 1 : 0;
        if ((nextDirection < 0 && bar->value() == bar->minimum()) ||
            (nextDirection > 0 && bar->value() == bar->maximum())) nextDirection = 0;
    }
    if (nextDirection != scrollDirection_) autoScroll_->stop();
    scrollDirection_ = nextDirection;
    if (!scrollDirection_) autoScroll_->stop();
    else if (!autoScroll_->isActive()) { autoScroll_->setInterval(200); autoScroll_->start(); }
    updateScrollBarVisibility();
}

bool EdgeStrip::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
        queueTrackLayout();
    } else if (watched == scroll_->viewport() && event->type() == QEvent::Resize) {
        layoutTrack(); queueHoverGeometryChanged();
    } else if (watched == track_ && event->type() == QEvent::LayoutRequest) {
        queueTrackLayout();
    } else if (event->type() == QEvent::Wheel) {
        auto* wheel = static_cast<QWheelEvent*>(event);
        const auto delta = wheel->pixelDelta().isNull() ? wheel->angleDelta() / 3 : wheel->pixelDelta();
        scrollByWheel(delta, wheel->pixelDelta().isNull());
        wheel->accept(); return true;
    } else if (event->type() == QEvent::Enter) {
        hoveredSurfaces_.insert(watched); updateScrollBarVisibility();
    } else if (event->type() == QEvent::Leave) {
        hoveredSurfaces_.remove(watched);
        QTimer::singleShot(0, this, [this] { updateScrollBarVisibility(); });
    }
    return QFrame::eventFilter(watched, event);
}
void EdgeStrip::resizeEvent(QResizeEvent* event) { QFrame::resizeEvent(event); layoutTrack(); }
void EdgeStrip::hideEvent(QHideEvent* event)
{
    scrollMotion_.stopAt(axisScrollBar()->value());
    autoScroll_->stop(); scrollDirection_ = 0; hoveredSurfaces_.clear(); scrollbar_->hide();
    QFrame::hideEvent(event);
}

} // namespace window_system
