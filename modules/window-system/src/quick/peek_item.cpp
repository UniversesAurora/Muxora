// SPDX-License-Identifier: MIT
#include "peek_item.hpp"
#include <QContextMenuEvent>
#include "button_item.hpp"
#include "path_item.hpp"
#include "tab_label_text_item.hpp"
#include "docking/peek_metrics.hpp"
#include <window_system/ui_scale.hpp>

#include <QCoreApplication>
#include <QCursor>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainterPath>
#include <QStyleHints>
#include <algorithm>

namespace window_system::quick {
namespace {

// As the Widgets PeekResizeHandle: the panel's size follows the pointer
// from the press, no smaller than 140 by 100.
class ResizeHandle final : public QQuickItem {
public:
    ResizeHandle(PeekItem& panel, Qt::Edges edges) : QQuickItem(&panel), panel_(panel), edges_(edges)
    {
        setAcceptedMouseButtons(Qt::LeftButton);
        setZ(3);
        const bool across = edges.testFlag(Qt::LeftEdge) || edges.testFlag(Qt::RightEdge);
        const bool along = edges.testFlag(Qt::TopEdge) || edges.testFlag(Qt::BottomEdge);
        setCursor(across && along ? (edges.testFlag(Qt::LeftEdge) == edges.testFlag(Qt::TopEdge)
            ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor) : across ? Qt::SizeHorCursor : Qt::SizeVerCursor);
    }
    [[nodiscard]] Qt::Edges edges() const { return edges_; }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        start_ = event->globalPosition().toPoint();
        initial_ = panel_.size().toSize();
        initialPolicy_ = panel_.requestedSize ? panel_.requestedSize() : PeekSizePolicy{};
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        const auto delta = event->globalPosition().toPoint() - start_;
        auto size = initialPolicy_;
        if (edges_.testFlag(Qt::LeftEdge) || edges_.testFlag(Qt::RightEdge))
            size.width = std::max(140, initial_.width() + (edges_.testFlag(Qt::RightEdge) ? delta.x() : -delta.x()));
        if (edges_.testFlag(Qt::TopEdge) || edges_.testFlag(Qt::BottomEdge))
            size.height = std::max(100, initial_.height() + (edges_.testFlag(Qt::BottomEdge) ? delta.y() : -delta.y()));
        event->accept();
        if (const auto resize = panel_.resizeRequested) resize(size);
    }

private:
    PeekItem& panel_;
    Qt::Edges edges_;
    QPoint start_;
    QSize initial_;
    PeekSizePolicy initialPolicy_;
};

} // namespace

PeekItem::PeekItem(const Theme& theme, QQuickItem* parent)
    : QQuickItem(parent), theme_(theme), surface_(new PathItem(this)), frame_(new PathItem(this)), title_(new TabLabelTextItem(this)),
      pin_(new ButtonItem(theme, this)), close_(new ButtonItem(theme, this))
{
    // Clicks on the panel's empty parts stay on the panel.
    setAcceptedMouseButtons(Qt::AllButtons);
    pin_->setIcon(iconFor(QStringLiteral("pin"), theme));
    pin_->setToolTip(QCoreApplication::translate("WindowSystem", "Pin and reserve layout space"));
    close_->setIcon(iconFor(QStringLiteral("close"), theme));
    close_->setToolTip(QCoreApplication::translate("WindowSystem", "Close tab"));
    connect(pin_, &ButtonItem::clicked, this, [this] { if (pinRequested) pinRequested(); });
    connect(close_, &ButtonItem::clicked, this, [this] { if (closeRequested) closeRequested(); });
}

void PeekItem::setTitle(const QString& title, const QFont& font)
{
    if (titleText_ == title && font_ == font) return;
    titleText_ = title; font_ = font;
    relayout();
}

void PeekItem::setPage(QQuickItem* page)
{
    if (page_ == page) return;
    page_ = page;
    if (page) { page->setParentItem(this); page->setVisible(true); }
    relayout();
}

void PeekItem::setEdge(Edge edge)
{
    if (edge_ == edge) return;
    edge_ = edge;
    qDeleteAll(handles_); handles_.clear();
    const bool vertical = edge == Edge::Left || edge == Edge::Right;
    const Qt::Edges inner = edge == Edge::Left ? Qt::RightEdge : edge == Edge::Right ? Qt::LeftEdge
        : edge == Edge::Top ? Qt::BottomEdge : Qt::TopEdge;
    const Qt::Edges along = vertical ? Qt::BottomEdge : Qt::RightEdge;
    for (const auto edges : {inner, along, inner | along}) {
        auto* handle = new ResizeHandle(*this, edges);
        handle->setObjectName(edges == inner ? QStringLiteral("PeekResizeHandle")
            : edges == along ? QStringLiteral("PeekAlongResizeHandle") : QStringLiteral("PeekCornerResizeHandle"));
        handles_.append(handle);
    }
    relayout();
}

int PeekItem::headerHeight() const
{
    return std::max(uiPixels(peekHeaderHeight), QFontMetrics(font_).height() + uiPixels(2));
}

bool PeekItem::event(QEvent* event)
{
    if (event->type() == QEvent::ContextMenu) {
        const auto* menu = static_cast<QContextMenuEvent*>(event);
        const bool keyboard = menu->reason() == QContextMenuEvent::Keyboard;
        const auto local = keyboard ? QPoint(int(width()) / 2, headerHeight() / 2) : mapFromGlobal(menu->globalPos()).toPoint();
        if (local.y() <= headerHeight()) if (const auto callback = contextMenuRequested) {
            const auto global = keyboard ? mapToGlobal(local).toPoint() : menu->globalPos();
            event->accept(); callback(global); return true;
        }
    }
    return QQuickItem::event(event);
}

void PeekItem::mousePressEvent(QMouseEvent* event)
{
    event->accept();
    const auto local = event->position().toPoint();
    if (event->button() != Qt::LeftButton || local.y() > headerHeight()) return;
    pressed_ = true;
    start_ = DragStart::capture(event->globalPosition().toPoint(), local, QRect(0, 0, int(width()), headerHeight() + 1));
}

void PeekItem::mouseMoveEvent(QMouseEvent* event)
{
    if (!pressed_ || !(event->buttons() & Qt::LeftButton)) return;
    const auto global = event->globalPosition().toPoint();
    if (!start_.crossedThreshold(global, QGuiApplication::styleHints()->startDragDistance())) return;
    pressed_ = false;
    // Tearing the tab out closes this panel; Qt then must not give it the grab.
    const QPointer<PeekItem> alive(this);
    if (const auto callback = dragRequested) callback(start_.movedTo(global));
    if (!alive) event->ignore();
}

void PeekItem::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) pressed_ = false;
}

void PeekItem::geometryChange(const QRectF& now, const QRectF& previous)
{
    QQuickItem::geometryChange(now, previous);
    if (now.size() != previous.size()) relayout();
}

void PeekItem::relayout()
{
    const QRectF area(QPointF(), size());
    // The surface inside a one-pixel accent frame.
    QPainterPath border, fill;
    border.addRect(QRectF(0, 0, area.width(), 1));
    border.addRect(QRectF(0, area.height() - 1, area.width(), 1));
    border.addRect(QRectF(0, 1, 1, area.height() - 2));
    border.addRect(QRectF(area.width() - 1, 1, 1, area.height() - 2));
    frame_->setSize(size()); frame_->setFill(theme_.accent); frame_->setPath(border);
    fill.addRect(area.adjusted(1, 1, -1, -1));
    surface_->setSize(size()); surface_->setFill(theme_.surface); surface_->setPath(fill);

    const int header = headerHeight();
    const int button = uiPixels(peekButtonExtent);
    const int top = 1 + (header - button) / 2;
    close_->setSize(QSizeF(button, button));
    close_->setPosition(QPointF(width() - 1 - uiPixels(2) - button, top));
    pin_->setSize(QSizeF(button, button));
    pin_->setPosition(QPointF(close_->x() - uiPixels(2) - button, top));
    const QRect text(0, 0, std::max(0, int(pin_->x()) - uiPixels(8)), header);
    if (titleText_.isEmpty() || text.isEmpty()) {
        title_->clearLabel();
    } else {
        TabLabelPresentation label;
        label.parts.tab = text; label.parts.title = text;
        label.state.shape = TabShape::Row; label.state.selected = true; label.state.title = titleText_;
        label.font = font_; label.naturalWidth = QFontMetrics(font_).horizontalAdvance(titleText_);
        TabStripLabelStyle style;
        style.palette.text = theme_.text;
        title_->setLabel(label, style, TabTextRole::Literal);
        title_->setPosition(QPointF(1 + uiPixels(6), 1));
    }
    if (page_) {
        page_->setPosition(QPointF(1, 1 + header));
        page_->setSize(QSizeF(std::max(0.0, width() - 2), std::max(0.0, height() - 2 - header)));
    }
    // The Widgets handles: four pixels inside the frame.
    const qreal w = width(), h = height();
    for (auto* item : std::as_const(handles_)) {
        const auto edges = static_cast<ResizeHandle*>(item)->edges();
        QRectF rect;
        if ((edges & (Qt::LeftEdge | Qt::RightEdge)) && (edges & (Qt::TopEdge | Qt::BottomEdge)))
            rect = QRectF(edges.testFlag(Qt::LeftEdge) ? 1 : w - 5, edges.testFlag(Qt::TopEdge) ? 1 : h - 5, 4, 4);
        else if (edges & (Qt::LeftEdge | Qt::RightEdge))
            rect = QRectF(edges.testFlag(Qt::LeftEdge) ? 1 : w - 5, 1, 4, h - 2);
        else rect = QRectF(1, edges.testFlag(Qt::TopEdge) ? 1 : h - 5, w - 2, 4);
        item->setPosition(rect.topLeft()); item->setSize(rect.size());
    }
}

} // namespace window_system::quick
