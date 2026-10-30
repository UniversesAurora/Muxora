// SPDX-License-Identifier: MIT
#include "peek_item.hpp"
#include "button_item.hpp"
#include "path_item.hpp"
#include "tab_label_text_item.hpp"
#include "docking/peek_metrics.hpp"
#include <window_system/ui_scale.hpp>

#include <QCoreApplication>
#include <QFontMetrics>
#include <QPainterPath>
#include <algorithm>

namespace window_system::quick {

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

    const int header = std::max(uiPixels(peekHeaderHeight), QFontMetrics(font_).height() + uiPixels(2));
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
}

} // namespace window_system::quick
