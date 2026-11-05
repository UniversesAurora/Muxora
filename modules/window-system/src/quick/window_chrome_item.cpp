// SPDX-License-Identifier: MIT
#include "window_chrome_item.hpp"
#include "button_item.hpp"
#include "tab_label_text_item.hpp"
#include <window_system/ui_scale.hpp>

#include <QCoreApplication>
#include <QFontMetrics>
#include <QGuiApplication>
#include <algorithm>
#include <array>

namespace window_system::quick {
namespace {
// As the Widgets caption buttons.
constexpr int captionButtonWidth = 42;
constexpr int titleMargin = 7;
} // namespace

WindowChromeItem::WindowChromeItem(const Theme& theme, QQuickItem* parent)
    : WindowDragAreaItem(parent), theme_(theme), caption_(new QQuickItem(this)), minimize_(new ButtonItem(theme, caption_)),
      maximize_(new ButtonItem(theme, caption_)), close_(new ButtonItem(theme, caption_)), title_(new TabLabelTextItem(this))
{
    caption_->setObjectName(QStringLiteral("CaptionControls"));
    minimize_->setProperty("role", QStringLiteral("minimize"));
    maximize_->setProperty("role", QStringLiteral("maximize"));
    close_->setProperty("role", QStringLiteral("close"));
    connect(caption_, &QQuickItem::widthChanged, this, &WindowChromeItem::layoutCaption);
    connect(caption_, &QQuickItem::heightChanged, this, &WindowChromeItem::layoutCaption);
    for (auto* button : {minimize_, maximize_, close_}) button->setShape(ButtonItem::Shape::Caption);
    minimize_->setToolTip(QCoreApplication::translate("WindowSystem", "Minimize"));
    close_->setDestructive(true);
    close_->setToolTip(QCoreApplication::translate("WindowSystem", "Close window"));
    connect(minimize_, &ButtonItem::clicked, this, [this] { if (minimizeRequested) minimizeRequested(); });
    connect(maximize_, &ButtonItem::clicked, this, [this] { if (maximizeRequested) maximizeRequested(); });
    connect(close_, &ButtonItem::clicked, this, [this] { if (closeRequested) closeRequested(); });
    connect(qGuiApp, &QGuiApplication::fontDatabaseChanged, this, [this] {
        measureTitle(); title_->clearLabel(); requestLayout();
    });
    setTheme(theme);
}

int WindowChromeItem::rowHeight() { return uiPixels(32); }

void WindowChromeItem::setTheme(const Theme& theme)
{
    if (titleFont_ != QGuiApplication::font()) measureTitle();
    theme_ = theme;
    for (auto* button : {minimize_, maximize_, close_}) button->setTheme(theme);
    minimize_->setIcon(iconFor(QStringLiteral("minimize"), theme));
    close_->setIcon(iconFor(QStringLiteral("close"), theme));
    auto onDanger = theme;
    onDanger.text = onDanger.headerText = Qt::white;
    close_->setActiveIcon(iconFor(QStringLiteral("close"), onDanger));
    setExpanded(expanded_);
    relayout();
}

void WindowChromeItem::setTitle(const QString& title)
{
    if (titleText_ == title) return;
    titleText_ = title;
    measureTitle();
    relayout();
}

void WindowChromeItem::measureTitle()
{
    titleFont_ = QGuiApplication::font();
    const QFontMetrics metrics(titleFont_);
    titleWidth_ = titleText_.isEmpty() ? 0 : metrics.horizontalAdvance(titleText_);
    titleHeight_ = metrics.height();
}

void WindowChromeItem::setButtonOrder(CaptionButtonOrder order)
{
    if (order_ == order) return;
    order_ = order;
    layoutCaption();
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
    if (now.size() != previous.size()) layoutTitle();
}

void WindowChromeItem::requestLayout()
{
    if (layoutPending_) return;
    layoutPending_ = true;
    QMetaObject::invokeMethod(this, [this] { layoutPending_ = false; relayout(); }, Qt::QueuedConnection);
}

void WindowChromeItem::relayout()
{
    setImplicitWidth(titleWidth_ + 2 * uiPixels(titleMargin));
    setImplicitHeight(std::max(rowHeight(), titleHeight_));
    caption_->setImplicitWidth(3 * uiPixels(captionButtonWidth));
    caption_->setImplicitHeight(rowHeight());
    layoutCaption();
    layoutTitle();
}

void WindowChromeItem::layoutTitle()
{
    const int textWidth = std::min(titleWidth_, std::max(0, int(width()) - 2 * uiPixels(titleMargin)));
    if (titleText_.isEmpty() || textWidth <= 0) { title_->clearLabel(); return; }
    const QRect text(0, 0, textWidth, int(height()));
    TabLabelPresentation label;
    label.parts.tab = text; label.parts.title = text;
    label.state.shape = TabShape::Row; label.state.selected = true; label.state.title = titleText_;
    label.font = titleFont_; label.naturalWidth = titleWidth_;
    TabStripLabelStyle style;
    style.palette.text = theme_.headerText;
    title_->setLabel(label, style, TabTextRole::Literal);
    title_->setPosition(QPointF((width() - textWidth) / 2, 0));
}

void WindowChromeItem::layoutCaption()
{
    const int button = uiPixels(captionButtonWidth);
    const bool leading = order_ != CaptionButtonOrder::Windows;
    const std::array buttons = order_ == CaptionButtonOrder::Reversed ? std::array{close_, maximize_, minimize_}
        : order_ == CaptionButtonOrder::MacOS ? std::array{close_, minimize_, maximize_}
        : std::array{minimize_, maximize_, close_};
    const int controls = int(buttons.size()) * button;
    int x = leading ? 0 : int(caption_->width()) - controls;
    const qreal y = (caption_->height() - rowHeight()) / 2;
    for (auto* item : buttons) {
        item->setPosition(QPointF(x, y));
        item->setSize(QSizeF(button, rowHeight()));
        x += button;
    }
}

bool WindowChromeItem::event(QEvent* event)
{
    if (event->type() == QEvent::ApplicationFontChange || event->type() == QEvent::DevicePixelRatioChange) {
        measureTitle(); title_->clearLabel(); requestLayout();
    }
    return WindowDragAreaItem::event(event);
}

} // namespace window_system::quick
