// SPDX-License-Identifier: MIT
#include "command_strip_item.hpp"
#include "button_item.hpp"
#include "popup_menu.hpp"
#include "path_stroke_item.hpp"
#include "path_item.hpp"
#include <window_system/ui_scale.hpp>
#include <QCoreApplication>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QCursor>
#include <QMouseEvent>
#include <QStyleHints>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QtMath>
#include <algorithm>

namespace window_system::quick {

// As a QToolButton under the Widgets style sheet: the 16-pixel icon, 4-pixel
// side padding, 1-pixel borders and the style's 3-pixel button margin.
constexpr int commandButtonWidth = 29;
namespace {
// Everything iconFor() reads: a sync that changes none of it keeps the
// button's prepared pixmap instead of rasterizing a new engine.
QString iconKey(const QString& name, const Theme& theme)
{
    return name + QLatin1Char('|') + theme.text.name(QColor::HexArgb) + theme.mutedText.name(QColor::HexArgb)
        + theme.selectionText.name(QColor::HexArgb) + theme.accent.name(QColor::HexArgb)
        + QString::number(theme.metrics.iconSize);
}
} // namespace

CommandStripItem::CommandStripItem(QQuickItem* parent, bool optionsEnabled)
    : QQuickItem(parent), viewport_(new QQuickItem(this)), more_(optionsEnabled ? new ButtonItem(defaultTheme(), this) : nullptr),
      arrows_(more_ ? new PathStrokeItem(more_) : nullptr), handle_(new PathItem(this)), optionsEnabled_(optionsEnabled)
{
    setClip(true);
    viewport_->setClip(true);
    setAcceptedMouseButtons(Qt::LeftButton);
    if (more_) {
        more_->setObjectName(QStringLiteral("StripOverflowButton"));
        more_->setActiveFocusOnTab(true);
        arrows_->setZ(3);
        more_->setToolTip(QCoreApplication::translate("WindowSystem", "Toolbar options"));
        connect(more_, &ButtonItem::clicked, this, &CommandStripItem::showOverflow);
    }
}

CommandStripItem::~CommandStripItem() { delete popup_.data(); }

void CommandStripItem::sync(const QVector<MenuItemDescription>& actions, const Theme& theme, const StripLayoutPreferences& preferences, bool hideEmptyOptions)
{
    theme_ = theme;
    hideEmptyOptions_ = hideEmptyOptions;
    more_->setTheme(theme);
    QHash<QString, Action> previous;
    for (const auto& action : std::as_const(actions_)) previous.insert(action.description.id, action);
    actions_.clear(); actions_.reserve(actions.size());
    const QSet<QString> hidden(preferences.excluded.cbegin(), preferences.excluded.cend());
    for (const auto& item : actions) {
        const auto retained = previous.take(item.id);
        auto button = retained.button;
        if (!button) {
            button = new ButtonItem(theme, viewport_);
            button->setObjectName(QStringLiteral("StripCommandButton"));
            button->setProperty("commandId", item.commandId);
            button->setIconExtent(16);
            button->setActiveFocusOnTab(true);
            connect(button, &ButtonItem::clicked, this, [this, id = item.commandId] {
                // A command can destroy this toolbar or its whole window.
                QTimer::singleShot(0, this, [this, id] { if (const auto callback = triggered) callback(id, {}); });
            });
        }
        button->setTheme(theme);
        auto icon = iconKey(item.iconName, theme);
        if (!retained.button || retained.iconKey != icon) button->setIcon(iconFor(item.iconName, theme));
        button->setToolTip(item.tooltip);
        button->setEnabled(item.action.enabled);
        button->setCheckable(item.action.checkable);
        button->setChecked(item.action.checked.value_or(false));
        actions_.append({item, button, !hidden.contains(item.id), std::move(icon)});
    }
    for (const auto& action : std::as_const(previous)) delete action.button.data();
    const int count = int(std::count_if(actions_.cbegin(), actions_.cend(), [](const auto& action) {
        return action.included && action.description.action.visible;
    }));
    contentWidth_ = count * uiPixels(commandButtonWidth);
    updateMetrics();
    if (popup_) popup_->setPresentation(theme);
    relayout();
    if (popup_) popup_->requestCatalogRefresh();
}

void CommandStripItem::setContent(QQuickItem* content, StripConstraints constraints)
{
    Q_ASSERT(!content_ && content && actions_.isEmpty());
    content_ = content; constraints_ = constraints;
    content->setParentItem(viewport_);
    connect(content, &QQuickItem::implicitWidthChanged, this, [this] { updateMetrics(); relayout(); });
    connect(content, &QQuickItem::implicitHeightChanged, this, [this] { updateMetrics(); relayout(); });
}

void CommandStripItem::syncContent(const Theme& theme, const StripLayoutPreferences& preferences, bool hideEmptyOptions)
{
    theme_ = theme; hideEmptyOptions_ = hideEmptyOptions; contentVisible_ = preferences.contentVisible;
    if (more_) more_->setTheme(theme);
    content_->setVisible(contentVisible_);
    updateMetrics(); relayout();
}

void CommandStripItem::setLocked(bool locked)
{
    if (locked_ == locked) return;
    locked_ = locked; updateMetrics(); relayout();
}

int CommandStripItem::chromeMinimumWidth() const
{
    return (locked_ ? 0 : uiPixels(stripHandleWidth)) + (optionsEnabled_ ? uiPixels(stripOptionsWidth) : 0)
        + uiPixels(constraints_.minimumContentWidth);
}

QVector<MenuItemDescription> CommandStripItem::buttons() const
{
    QVector<MenuItemDescription> result;
    for (const auto& action : actions_) result.append(action.description);
    return result;
}

QStringList CommandStripItem::itemIds() const
{
    QStringList result;
    for (const auto& action : actions_) result.append(action.description.id);
    return result;
}

int CommandStripItem::minimumWidth() const { return constraints_.compressible ? chromeMinimumWidth() : qCeil(implicitWidth()); }

void CommandStripItem::updateMetrics()
{
    const int content = content_ ? contentVisible_ ? qCeil(content_->implicitWidth()) : 0 : contentWidth_;
    setImplicitWidth(std::max(chromeMinimumWidth(), content + (locked_ ? 0 : uiPixels(stripHandleWidth))
        + (optionsEnabled_ && !hideEmptyOptions_ ? uiPixels(stripOptionsWidth) : 0)));
    setImplicitHeight(std::max({uiPixels(compactRowHeight), QFontMetrics(QGuiApplication::font()).height() + uiPixels(8),
        content_ && contentVisible_ ? qCeil(content_->implicitHeight()) : 0}));
}

void CommandStripItem::geometryChange(const QRectF& now, const QRectF& previous)
{
    QQuickItem::geometryChange(now, previous);
    if (now.size() != previous.size()) relayout();
}

void CommandStripItem::relayout()
{
    const int buttonWidth = uiPixels(commandButtonWidth);
    const int handle = locked_ ? 0 : uiPixels(stripHandleWidth);
    const int content = content_ ? contentVisible_ ? qCeil(content_->implicitWidth()) : 0 : contentWidth_;
    // Reserve the options tail only after checking the complete content.
    const int tail = optionsEnabled_ && (!hideEmptyOptions_ || content > width() - handle) ? uiPixels(stripOptionsWidth) : 0;
    if (more_) more_->setVisible(tail > 0);
    if (!tail && popup_) popup_->dismiss();
    const int available = std::max(0, int(width()) - tail - handle);
    viewport_->setPosition({qreal(handle), 0}); viewport_->setSize({qreal(available), height()});
    if (more_) { more_->setPosition({std::max(0.0, width() - tail), 0}); more_->setSize({qreal(tail), height()}); }
    handle_->setSize({qreal(handle), height()}); handle_->setVisible(!locked_); updateHandle();
    if (locked_) handle_->unsetCursor(); else handle_->setCursor(QCursor(Qt::SizeAllCursor));
    overflow_.clear();
    if (content_) {
        content_->setPosition({0, 0}); content_->setSize(viewport_->size());
        updateArrows(); return;
    }
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
    // The tail also holds the strip's options, as the Widgets tail does.
    more_->setEnabled(!overflow_.isEmpty() || bool(options));
    updateArrows();
}

void CommandStripItem::mousePressEvent(QMouseEvent* event)
{
    // Elsewhere the press belongs to the toolbar area, which moves the window.
    if (locked_ || event->button() != Qt::LeftButton || event->position().x() >= uiPixels(stripHandleWidth)) {
        event->ignore();
        return;
    }
    event->accept();
    pressGlobal_ = event->globalPosition().toPoint();
    pressOffset_ = event->position().toPoint();
}

void CommandStripItem::mouseMoveEvent(QMouseEvent* event)
{
    if (!pressGlobal_) return QQuickItem::mouseMoveEvent(event);
    event->accept();
    if ((event->globalPosition().toPoint() - *pressGlobal_).manhattanLength() < QGuiApplication::styleHints()->startDragDistance()) return;
    // A drag can move this strip to the other area, which would cost its
    // pointer grab; the receiver takes the pointer before that.
    const auto press = *std::exchange(pressGlobal_, std::nullopt);
    if (const auto callback = dragStarted) callback(press, pressOffset_);
}

void CommandStripItem::mouseReleaseEvent(QMouseEvent* event)
{
    if (!pressGlobal_) return QQuickItem::mouseReleaseEvent(event);
    event->accept();
    pressGlobal_.reset();
}

void CommandStripItem::updateHandle()
{
    const auto size = handle_->size();
    auto color = theme_.text; color.setAlpha(120);
    if (handleSize_ == size && handleScale_ == interfaceScale() && handleColor_ == color) return;
    handleSize_ = size; handleScale_ = interfaceScale(); handleColor_ = color;
    QPainterPath dots;
    for (qreal y = uiPixelsF(9); y < size.height() - uiPixelsF(7); y += uiPixelsF(4))
        for (const int x : {4, 7}) dots.addRect({uiPixelsF(x), y, uiPixelsF(1), uiPixelsF(1)});
    handle_->setFill(color); handle_->setPath(dots);
}

void CommandStripItem::updateArrows()
{
    if (!arrows_) return;
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

void CommandStripItem::showOverflow()
{
    if (popup_ && popup_->isOpen()) { popup_->dismiss(); return; }
    delete popup_.data();
    auto* popup = new PopupMenu(theme_, this);
    popup_ = popup;
    const auto describe = [this] {
        auto items = overflow_;
        if (const auto describeOptions = options) {
            const auto more = describeOptions();
            if (!items.isEmpty() && !more.isEmpty()) {
                MenuItemDescription separator;
                separator.kind = MenuItemDescription::Kind::Separator;
                items.append(std::move(separator));
            }
            items += more;
        }
        return items;
    };
    popup->setRefreshHandler(describe);
    connect(popup, &PopupMenu::triggered, this, [this](const MenuItemDescription& item) {
        if (const auto callback = triggered) callback(item.commandId, item.arguments);
    });
    connect(popup, &PopupMenu::closed, popup, &QObject::deleteLater);
    popup->open(more_, more_->mapToGlobal(QPointF(0, more_->height())).toPoint(), describe());
}

} // namespace window_system::quick
