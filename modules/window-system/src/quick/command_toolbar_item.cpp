// SPDX-License-Identifier: MIT
#include "command_toolbar_item.hpp"
#include "button_item.hpp"
#include "popup_menu.hpp"
#include "path_stroke_item.hpp"
#include <window_system/ui_scale.hpp>
#include <QCoreApplication>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <algorithm>

namespace window_system::quick {

CommandToolbarItem::CommandToolbarItem(QQuickItem* parent)
    : QQuickItem(parent), more_(new ButtonItem(defaultTheme(), this)), arrows_(new PathStrokeItem(more_))
{
    setClip(true);
    more_->setObjectName(QStringLiteral("StripOverflowButton"));
    more_->setActiveFocusOnTab(true);
    arrows_->setZ(3);
    more_->setToolTip(QCoreApplication::translate("WindowSystem", "Toolbar options"));
    connect(more_, &ButtonItem::clicked, this, &CommandToolbarItem::showOverflow);
}

CommandToolbarItem::~CommandToolbarItem() { delete popup_.data(); }

void CommandToolbarItem::sync(const QVector<MenuItemDescription>& actions, const Theme& theme, const QStringList& excluded, bool hideEmptyOptions)
{
    theme_ = theme;
    hideEmptyOptions_ = hideEmptyOptions;
    more_->setTheme(theme);
    QHash<QString, QPointer<ButtonItem>> previous;
    for (const auto& action : actions_) previous.insert(action.description.id, action.button);
    actions_.clear(); actions_.reserve(actions.size());
    const QSet<QString> hidden(excluded.cbegin(), excluded.cend());
    for (const auto& item : actions) {
        auto button = previous.take(item.id);
        if (!button) {
            button = new ButtonItem(theme, this);
            button->setObjectName(QStringLiteral("StripCommandButton"));
            button->setProperty("commandId", item.commandId);
            button->setIconExtent(16);
            button->setActiveFocusOnTab(true);
            connect(button, &ButtonItem::clicked, this, [this, id = item.commandId] {
                // A command can destroy this toolbar or its whole window.
                QTimer::singleShot(0, this, [this, id] { if (const auto callback = triggered) callback(id); });
            });
        }
        button->setTheme(theme);
        button->setIcon(iconFor(item.iconName, theme));
        button->setToolTip(item.tooltip);
        button->setEnabled(item.action.enabled);
        button->setCheckable(item.action.checkable);
        button->setChecked(item.action.checked.value_or(false));
        actions_.append({item, button, !hidden.contains(item.id)});
    }
    for (const auto& button : previous) delete button.data();
    const int count = int(std::count_if(actions_.cbegin(), actions_.cend(), [](const auto& action) {
        return action.included && action.description.action.visible;
    }));
    contentWidth_ = count * uiPixels(26);
    setImplicitWidth(contentWidth_ + (hideEmptyOptions_ ? 0 : uiPixels(12)));
    setImplicitHeight(std::max(uiPixels(28), QFontMetrics(QGuiApplication::font()).height() + uiPixels(6)));
    if (popup_) popup_->setPresentation(theme);
    relayout();
    if (popup_) popup_->requestCatalogRefresh();
}

void CommandToolbarItem::geometryChange(const QRectF& now, const QRectF& previous)
{
    QQuickItem::geometryChange(now, previous);
    if (now.size() != previous.size()) relayout();
}

void CommandToolbarItem::relayout()
{
    const int buttonWidth = uiPixels(26);
    // Reserve the options tail only after checking the complete content.
    const int tail = !hideEmptyOptions_ || contentWidth_ > width() ? uiPixels(12) : 0;
    more_->setVisible(tail > 0);
    if (!tail && popup_) popup_->dismiss();
    const int available = std::max(0, int(width()) - tail);
    more_->setPosition({qreal(available), 0}); more_->setSize({qreal(tail), height()});
    overflow_.clear();
    int x = 0;
    bool full = false;
    for (const auto& action : actions_) {
        const bool included = action.included && action.description.action.visible;
        const bool fits = included && !full && x + buttonWidth <= available;
        action.button->setVisible(fits);
        if (fits) {
            action.button->setPosition({qreal(x), 0}); action.button->setSize({qreal(buttonWidth), height()});
            x += buttonWidth;
        } else if (included) { full = true; overflow_.append(action.description); }
    }
    more_->setEnabled(!overflow_.isEmpty());
    updateArrows();
}

void CommandToolbarItem::updateArrows()
{
    const bool overflow = !overflow_.isEmpty();
    if (arrowSize_ == more_->size() && arrowScale_ == interfaceScale() && arrowColor_ == theme_.text && arrowsOverflow_ == overflow) return;
    arrowSize_ = more_->size(); arrowScale_ = interfaceScale(); arrowColor_ = theme_.text; arrowsOverflow_ = overflow;
    QPainterPath arrows;
    const qreal center = more_->width() / 2.0;
    if (overflow) {
        arrows.moveTo(center - uiPixelsF(2), uiPixelsF(7)); arrows.lineTo(center, uiPixelsF(9)); arrows.lineTo(center - uiPixelsF(2), uiPixelsF(11));
        arrows.moveTo(center + uiPixelsF(1), uiPixelsF(7)); arrows.lineTo(center + uiPixelsF(3), uiPixelsF(9)); arrows.lineTo(center + uiPixelsF(1), uiPixelsF(11));
    }
    arrows.moveTo(center - uiPixelsF(3), height() - uiPixelsF(11)); arrows.lineTo(center, height() - uiPixelsF(8)); arrows.lineTo(center + uiPixelsF(3), height() - uiPixelsF(11));
    arrows_->setSize(more_->size()); arrows_->setStroke(arrows, theme_.text, uiPixelsF(1));
}

void CommandToolbarItem::showOverflow()
{
    if (popup_ && popup_->isOpen()) { popup_->dismiss(); return; }
    delete popup_.data();
    auto* popup = new PopupMenu(theme_, this);
    popup_ = popup;
    popup->setRefreshHandler([this] { return overflow_; });
    connect(popup, &PopupMenu::triggered, this, [this](const MenuItemDescription& item) {
        if (const auto callback = triggered) callback(item.commandId);
    });
    connect(popup, &PopupMenu::closed, popup, &QObject::deleteLater);
    popup->open(more_, more_->mapToGlobal(QPointF(0, more_->height())).toPoint(), overflow_);
}

} // namespace window_system::quick
