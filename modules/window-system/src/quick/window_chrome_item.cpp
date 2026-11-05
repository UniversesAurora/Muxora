// SPDX-License-Identifier: MIT
#include "window_chrome_item.hpp"
#include "button_item.hpp"
#include "tab_label_text_item.hpp"
#include <window_system/ui_scale.hpp>

#include <QCoreApplication>
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QStyleHints>
#include <QtMath>
#include <algorithm>
#include <array>

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

void WindowChromeItem::setMenuBar(QQuickItem* menu)
{
    Q_ASSERT(!menu_);
    menu_ = menu;
    menu->setClip(true);
    // A font or catalog update must finish before header geometry reaches
    // host content callbacks, which can destroy the workspace synchronously.
    connect(menu, &QQuickItem::implicitWidthChanged, this, &WindowChromeItem::requestLayout);
    connect(menu, &QQuickItem::implicitHeightChanged, this, &WindowChromeItem::requestLayout);
    relayout();
}

QSize WindowChromeItem::minimumSize() const
{
    return {3 * uiPixels(captionButtonWidth) + (menu_ ? qCeil(menu_->property("minimumWidthHint").toReal()) : 0), qCeil(implicitHeight())};
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
    titleWidth_ = titleText_.isEmpty() ? 0 : QFontMetrics(titleFont_).horizontalAdvance(titleText_);
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

void WindowChromeItem::requestLayout()
{
    if (layoutPending_) return;
    layoutPending_ = true;
    QMetaObject::invokeMethod(this, [this] { layoutPending_ = false; relayout(); }, Qt::QueuedConnection);
}

void WindowChromeItem::relayout()
{
    setImplicitHeight(std::max(rowHeight(), menu_ ? qCeil(menu_->implicitHeight()) : 0));
    // The outermost button keeps the window's edge, as in the Widgets row.
    const int button = uiPixels(captionButtonWidth), row = int(height());
    const bool leading = order_ != CaptionButtonOrder::Windows;
    const std::array buttons = order_ == CaptionButtonOrder::Reversed ? std::array{close_, maximize_, minimize_}
        : order_ == CaptionButtonOrder::MacOS ? std::array{close_, minimize_, maximize_}
        : std::array{minimize_, maximize_, close_};
    const int controls = int(buttons.size()) * button;
    int x = leading ? 0 : int(width()) - controls;
    for (auto* item : buttons) {
        item->setPosition(QPointF(x, 0));
        item->setSize(QSizeF(button, row));
        x += button;
    }
    const int available = std::max(0, int(width()) - controls);
    const int menus = menu_ ? std::min(available, qCeil(menu_->implicitWidth())) : 0;
    if (menu_) {
        menu_->setPosition({qreal(leading ? controls : 0), (row - menu_->implicitHeight()) / 2});
        menu_->setSize({qreal(menus), menu_->implicitHeight()});
    }
    // The title is centered between the menus and caption controls.
    const QRect area((leading ? controls : 0) + menus, 0, available - menus, row);
    const int textWidth = std::min(titleWidth_, std::max(0, area.width() - uiPixels(16)));
    if (titleText_.isEmpty() || textWidth <= 0) { title_->clearLabel(); return; }
    const QRect text(0, 0, textWidth, row);
    TabLabelPresentation label;
    label.parts.tab = text; label.parts.title = text;
    label.state.shape = TabShape::Row; label.state.selected = true; label.state.title = titleText_;
    label.font = titleFont_; label.naturalWidth = titleWidth_;
    TabStripLabelStyle style;
    style.palette.text = theme_.headerText;
    title_->setLabel(label, style, TabTextRole::Literal);
    title_->setPosition(QPointF(area.left() + (area.width() - textWidth) / 2, 0));
}

void WindowChromeItem::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !window()) return QQuickItem::mousePressEvent(event);
    event->accept();
    if (carriesTabs && carriesTabs()) press_ = event->globalPosition().toPoint();
    else window()->startSystemMove();
}

bool WindowChromeItem::event(QEvent* event)
{
    if (event->type() == QEvent::ApplicationFontChange || event->type() == QEvent::DevicePixelRatioChange) {
        measureTitle(); title_->clearLabel(); requestLayout();
    }
    if (event->type() == QEvent::ContextMenu) if (const auto callback = menuRequested) {
        const auto* menu = static_cast<QContextMenuEvent*>(event);
        const auto global = menu->reason() == QContextMenuEvent::Keyboard
            ? mapToGlobal(boundingRect().center()).toPoint() : menu->globalPos();
        event->accept(); callback(global); return true;
    }
    return QQuickItem::event(event);
}

void WindowChromeItem::mouseMoveEvent(QMouseEvent* event)
{
    if (!press_) return QQuickItem::mouseMoveEvent(event);
    event->accept();
    if ((event->globalPosition().toPoint() - *press_).manhattanLength() < QGuiApplication::styleHints()->startDragDistance()) return;
    const auto press = *std::exchange(press_, std::nullopt);
    if (carryRequested) carryRequested(press);
}

void WindowChromeItem::mouseReleaseEvent(QMouseEvent* event)
{
    press_.reset();
    QQuickItem::mouseReleaseEvent(event);
}

void WindowChromeItem::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return QQuickItem::mouseDoubleClickEvent(event);
    event->accept();
    if (maximizeRequested) maximizeRequested();
}

} // namespace window_system::quick
