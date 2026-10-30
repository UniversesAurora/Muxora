// SPDX-License-Identifier: MIT
#include "window_chrome_item.hpp"
#include "button_item.hpp"
#include "tab_label_text_item.hpp"
#include <window_system/ui_scale.hpp>

#include <QCoreApplication>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QQuickWindow>
#include <algorithm>

namespace window_system::quick {
namespace {
// As the Widgets caption buttons.
constexpr int captionButtonWidth = 42;
} // namespace

WindowChromeItem::WindowChromeItem(const Theme& theme, QQuickItem* parent)
    : QQuickItem(parent), theme_(theme), minimize_(new ButtonItem(theme, this)), maximize_(new ButtonItem(theme, this)),
      close_(new ButtonItem(theme, this)), title_(new TabLabelTextItem(this))
{
    setAcceptedMouseButtons(Qt::LeftButton);
    for (auto* button : {minimize_, maximize_, close_}) button->setShape(ButtonItem::Shape::Caption);
    minimize_->setIcon(iconFor(QStringLiteral("minimize"), theme));
    minimize_->setToolTip(QCoreApplication::translate("WindowSystem", "Minimize"));
    close_->setIcon(iconFor(QStringLiteral("close"), theme));
    // The close icon turns white on its danger fill.
    auto onDanger = theme;
    onDanger.text = onDanger.headerText = Qt::white;
    close_->setActiveIcon(iconFor(QStringLiteral("close"), onDanger));
    close_->setDestructive(true);
    close_->setToolTip(QCoreApplication::translate("WindowSystem", "Close window"));
    connect(minimize_, &ButtonItem::clicked, this, [this] { if (minimizeRequested) minimizeRequested(); });
    connect(maximize_, &ButtonItem::clicked, this, [this] { if (maximizeRequested) maximizeRequested(); });
    connect(close_, &ButtonItem::clicked, this, [this] { if (closeRequested) closeRequested(); });
    setExpanded(false);
}

int WindowChromeItem::rowHeight() { return uiPixels(32); }

void WindowChromeItem::setTitle(const QString& title)
{
    if (titleText_ == title) return;
    titleText_ = title;
    relayout();
}

void WindowChromeItem::setButtonOrder(CaptionButtonOrder order)
{
    if (order_ == order) return;
    order_ = order;
    relayout();
}

void WindowChromeItem::setExpanded(bool expanded)
{
    expanded_ = expanded;
    const auto role = expanded ? QStringLiteral("restore") : QStringLiteral("maximize");
    maximize_->setIcon(iconFor(role, theme_));
    maximize_->setToolTip(expanded ? QCoreApplication::translate("WindowSystem", "Restore")
                                   : QCoreApplication::translate("WindowSystem", "Maximize"));
}

ButtonItem* WindowChromeItem::captionButtonAt(QPointF position) const
{
    for (auto* button : {minimize_, maximize_, close_})
        if (button->isVisible() && button->contains(button->mapFromScene(position))) return button;
    return nullptr;
}

void WindowChromeItem::geometryChange(const QRectF& now, const QRectF& previous)
{
    QQuickItem::geometryChange(now, previous);
    if (now.size() != previous.size()) relayout();
}

void WindowChromeItem::relayout()
{
    // The outermost button keeps the window's edge, as in the Widgets row.
    const int button = uiPixels(captionButtonWidth), row = int(height());
    const bool leading = order_ != CaptionButtonOrder::Windows;
    const QVector<ButtonItem*> buttons = order_ == CaptionButtonOrder::Reversed ? QVector{close_, maximize_, minimize_}
        : order_ == CaptionButtonOrder::MacOS ? QVector{close_, minimize_, maximize_}
        : QVector{minimize_, maximize_, close_};
    const int controls = int(buttons.size()) * button;
    int x = leading ? 0 : int(width()) - controls;
    for (auto* item : buttons) {
        item->setPosition(QPointF(x, 0));
        item->setSize(QSizeF(button, row));
        x += button;
    }
    // The title is centered in the rest of the row.
    const QRect area = leading ? QRect(controls, 0, std::max(0, int(width()) - controls), row)
                               : QRect(0, 0, std::max(0, int(width()) - controls), row);
    const auto font = QGuiApplication::font();
    const int natural = QFontMetrics(font).horizontalAdvance(titleText_);
    const int textWidth = std::min(natural, std::max(0, area.width() - uiPixels(16)));
    if (titleText_.isEmpty() || textWidth <= 0) { title_->clearLabel(); return; }
    const QRect text(0, 0, textWidth, row);
    TabLabelPresentation label;
    label.parts.tab = text; label.parts.title = text;
    label.state.shape = TabShape::Row; label.state.selected = true; label.state.title = titleText_;
    label.font = font; label.naturalWidth = natural;
    TabStripLabelStyle style;
    style.palette.text = theme_.headerText;
    title_->setLabel(label, style, TabTextRole::Literal);
    title_->setPosition(QPointF(area.left() + (area.width() - textWidth) / 2, 0));
}

void WindowChromeItem::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !window()) return QQuickItem::mousePressEvent(event);
    event->accept();
    window()->startSystemMove();
}

void WindowChromeItem::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return QQuickItem::mouseDoubleClickEvent(event);
    event->accept();
    if (maximizeRequested) maximizeRequested();
}

} // namespace window_system::quick
