// SPDX-License-Identifier: MIT
#include "command_strip.hpp"
#include "commands/popup_menu.hpp"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QFocusEvent>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QResizeEvent>
#include <QSet>
#include <QScopedValueRollback>
#include <QStyleOptionToolButton>
#include <QStylePainter>
#include <QToolButton>
#include <QTimer>

#include <algorithm>
#include <limits>
#include <utility>

namespace window_system {
namespace {

constexpr int rowHeight = 32;
constexpr int rowMargin = 2;
constexpr int stripSpacing = 4;
constexpr int handleWidth = 12;
constexpr int tailWidth = 12;

class StripHandle final : public QWidget {
public:
    using QWidget::QWidget;
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        QColor color = palette().color(QPalette::WindowText);
        color.setAlpha(120);
        painter.setPen(color);
        for (int y = 9; y < height() - 7; y += 4) {
            painter.drawPoint(4, y);
            painter.drawPoint(7, y);
        }
    }
};

class OverflowButton final : public QToolButton {
public:
    explicit OverflowButton(QWidget* parent) : QToolButton(parent)
    {
        setObjectName("StripOverflowButton");
        setCursor(Qt::ArrowCursor);
        setToolTip(QStringLiteral("工具条选项"));
        setAccessibleName(toolTip());
        // Mouse popups must not leave a keyboard-focus outline after closing.
        // Tab navigation still gives the button its normal visible focus.
        setFocusPolicy(Qt::StrongFocus);
        setAutoRaise(true);
        setPopupMode(QToolButton::InstantPopup);
    }
    bool hasOverflow = false;
    void setPopupMenu(QMenu* popup)
    {
        setMenu(popup);
        connect(popup, &QMenu::aboutToHide, this, [this] {
            setDown(false);
            update();
            QTimer::singleShot(0, this, [this] { refreshHoverAfterPopup(); });
        });
    }
protected:
    void focusInEvent(QFocusEvent* event) override
    {
        if (event->reason() != Qt::PopupFocusReason && event->reason() != Qt::ActiveWindowFocusReason)
            keyboardFocus_ = event->reason() == Qt::TabFocusReason
                || event->reason() == Qt::BacktabFocusReason || event->reason() == Qt::ShortcutFocusReason;
        QToolButton::focusInEvent(event);
    }
    void keyPressEvent(QKeyEvent* event) override
    {
        keyboardFocus_ = true;
        update();
        QToolButton::keyPressEvent(event);
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) { dismissRelease_ = false; keyboardFocus_ = false; update(); }
        if (event->button() == Qt::LeftButton && menu() && menu()->isVisible()) {
            dismissRelease_ = true;
            menu()->hide(); setDown(false); update(); event->accept(); return;
        }
        QToolButton::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (dismissRelease_ && event->button() == Qt::LeftButton) {
            dismissRelease_ = false; event->accept(); return;
        }
        QToolButton::mouseReleaseEvent(event);
    }
    void paintEvent(QPaintEvent*) override
    {
        QStylePainter painter(this);
        QStyleOptionToolButton option;
        initStyleOption(&option);
        // Keep native popup focus restoration, but only keyboard navigation
        // draws a focus cue. A mouse-opened menu must not leave a focus border.
        if (!keyboardFocus_) option.state &= ~QStyle::State_HasFocus;
        // Keep the themed tool-button hover, pressed and keyboard-focus states;
        // the narrow tail paints its own two compact arrows below.
        option.features = QStyleOptionToolButton::None;
        option.subControls = QStyle::SC_ToolButton;
        painter.drawComplexControl(QStyle::CC_ToolButton, option);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(palette().color(QPalette::WindowText), 1.0));
        const qreal x = width() / 2.0;
        if (hasOverflow) {
            painter.drawPolyline(QPolygonF{{x - 2, 7}, {x, 9}, {x - 2, 11}});
            painter.drawPolyline(QPolygonF{{x + 1, 7}, {x + 3, 9}, {x + 1, 11}});
        }
        painter.drawPolyline(QPolygonF{{x - 3, height() - 11.0}, {x, height() - 8.0}, {x + 3, height() - 11.0}});
    }
private:
    void refreshHoverAfterPopup()
    {
        if (!menu() || menu()->isVisible()) return;
        // Popup capture can consume the leave event. Clear the old button's
        // hover after routing ends; future real enter events restore it. Do
        // not manufacture an enter for another widget or set a positive flag
        // without Qt's mouse-receiver bookkeeping.
        QPointer<QWidget> guard = this;
        setAttribute(Qt::WA_UnderMouse, false);
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(this, &leave);
        if (guard) guard->update();
    }
    bool dismissRelease_ = false;
    bool keyboardFocus_ = false;
};

struct ActionItem {
    QString id;
    QPointer<QAction> action;
    QPointer<QToolButton> button;
    bool included = true;
};

class StripFrame final : public QFrame {
public:
    StripFrame(QString stripId, QString stripTitle, QWidget* parent)
        : QFrame(parent), id(std::move(stripId)), title(std::move(stripTitle)),
          handle(new StripHandle(this)), viewport(new QWidget(this)), tail(new OverflowButton(this))
    {
        setObjectName("CommandStrip");
        setProperty("stripId", id);
        setMinimumSize(0, 0);
        handle->setObjectName("StripHandle");
        handle->setProperty("stripId", id);
        handle->setToolTip(QStringLiteral("拖动以调整工具条位置或前一工具条宽度"));
        handle->setAccessibleName(title + QStringLiteral("工具条拖动把手"));
        viewport->setObjectName("StripViewport");
        setProperty("windowDragArea", true);
        viewport->setProperty("windowDragArea", true);
    }

    void setLocked(bool locked)
    {
        handle->setVisible(!locked);
        handle->setCursor(locked ? Qt::ArrowCursor : Qt::SizeAllCursor);
        layoutItems();
    }

    [[nodiscard]] int naturalWidth() const
    {
        int width = handle->isHidden() ? 0 : handleWidth;
        if (content && !content->isHidden()) width += content->sizeHint().width();
        else for (const auto& item : items)
            if (item.included && item.action && item.action->isVisible() && item.button) width += item.button->sizeHint().width();
        return std::max(chromeMinimumWidth(), width + (optionsEnabled ? tailWidth : 0));
    }

    [[nodiscard]] int minimumWidthForLayout() const
    {
        return constraints.compressible ? chromeMinimumWidth() : naturalWidth();
    }

    [[nodiscard]] int chromeMinimumWidth() const
    {
        return (optionsEnabled ? tailWidth : 0) + (handle->isHidden() ? 0 : handleWidth) + constraints.minimumContentWidth;
    }

    void layoutItems()
    {
        const int handleSize = handle->isHidden() ? 0 : handleWidth;
        const int optionsSize = optionsEnabled ? tailWidth : 0;
        handle->setGeometry(0, 0, handleSize, height());
        tail->setVisible(optionsEnabled);
        tail->setGeometry(std::max(0, width() - optionsSize), 0, optionsSize, height());
        viewport->setGeometry(handleSize, 0, std::max(0, width() - handleSize - optionsSize), height());
        overflow.clear();
        if (content) {
            // Menu actions retain natural geometry for our overflow menu;
            // ordinary content paints against the actual available viewport.
            const int contentWidth = qobject_cast<QMenuBar*>(content.data())
                ? std::max(viewport->width(), content->sizeHint().width()) : viewport->width();
            content->setGeometry(0, 0, contentWidth, height());
            if (auto* menu = qobject_cast<QMenuBar*>(content.data())) {
                for (const auto& item : items) {
                    if (!item.action) continue;
                    item.action->setVisible(item.included);
                    if (item.included && menu->actionGeometry(item.action).right() >= viewport->width()) overflow.append(item.id);
                }
            } else if (!content->isHidden() && content->sizeHint().width() > viewport->width()) {
                overflow.append(QStringLiteral("content"));
            }
        } else {
            int x = 0;
            bool full = false;
            for (const auto& item : items) {
                if (!item.button || !item.action) continue;
                const int itemWidth = item.button->sizeHint().width();
                const bool included = item.included && item.action->isVisible();
                const bool fits = included && !full && x + itemWidth <= viewport->width();
                item.button->setVisible(fits);
                if (fits) { item.button->setGeometry(x, 0, itemWidth, height()); x += itemWidth; }
                else if (included) { full = true; overflow.append(item.id); }
            }
        }
        tail->hasOverflow = !overflow.isEmpty();
        tail->setProperty("hasOverflow", tail->hasOverflow);
        tail->update();
    }

    QString id;
    QString title;
    StripHandle* handle;
    QWidget* viewport;
    OverflowButton* tail;
    QPointer<QWidget> content;
    QList<ActionItem> items;
    QStringList overflow;
    bool visible = true;
    bool optionsEnabled = true;
    CommandStripHost::StripConstraints constraints;
    CommandStripHost::StripSizing sizing = CommandStripHost::StripSizing::Content;
    int requestedWidth = -1;
    bool naturalWidthPriority = false;

protected:
    void resizeEvent(QResizeEvent* event) override { QFrame::resizeEvent(event); layoutItems(); }
};

} // namespace

struct CommandStripHost::Private {
    QHash<QString, StripFrame*> strips;
    QStringList rows[2];
    QWidget* rowWidgets[2] = {};
    bool locked = true;
    bool arranging = false;
    std::function<void(QMenu*)> contextMenuCustomizer;
    std::function<void()> layoutChanged;
    std::function<void(const QString&, StripSizing)> sizingChangeRequest;
    std::function<void(const QString&)> widthResetRequest;
    QString pendingStrip;
    QPoint pressPosition;
    QPoint pressOffset;
    QString dragging;
    QStringList originalRows[2];
    QHash<QString, int> originalWidths;
    QHash<QString, QRect> originalBounds;
    QHash<QString, StripSizing> originalSizing;
    QHash<QString, bool> originalWidthPriorities;
    bool reserveDragSlot = false;
    QPointer<QLabel> preview;
    QPointer<QFrame> marker;
};

CommandStripHost::CommandStripHost(QWidget* parent)
    : QFrame(parent, Qt::FramelessWindowHint), d(std::make_unique<Private>())
{
    setObjectName("CommandStripHost");
    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFocusPolicy(Qt::StrongFocus);
    for (auto*& row : d->rowWidgets) {
        row = new QWidget(this);
        row->setObjectName("CommandStripRow");
        row->setProperty("windowDragArea", true);
    }
}

CommandStripHost::~CommandStripHost()
{
    // Actions can outlive their presentations or emit while children tear down.
    // Stop callbacks before the private layout state is destroyed.
    for (auto* strip : d->strips) {
        strip->removeEventFilter(this);
        strip->handle->removeEventFilter(this);
        for (const auto& item : strip->items) if (item.action) disconnect(item.action, nullptr, this, nullptr);
        if (strip->tail->menu()) disconnect(strip->tail->menu(), nullptr, this, nullptr);
    }
}

void CommandStripHost::addStrip(QString id, QWidget* content, int row)
{
    addStrip(id, id, content, row);
}

void CommandStripHost::addStrip(QString id, QString title, QWidget* content, int row, StripSizing sizing)
{
    if (!content || id.isEmpty() || d->strips.contains(id)) return;
    auto* strip = new StripFrame(id, std::move(title), d->rowWidgets[qBound(0, row, 1)]);
    strip->content = content;
    strip->sizing = sizing;
    content->setParent(strip->viewport);
    content->show();
    if (auto* menu = qobject_cast<QMenuBar*>(content)) {
        int index = 0;
        for (auto* action : menu->actions()) {
            auto itemId = action->objectName();
            if (itemId.isEmpty()) itemId = QString::number(index);
            strip->items.append({itemId, action, {}, true});
            connect(action, &QAction::changed, this, [this] { if (!d->arranging) arrange(); });
            ++index;
        }
    }
    strip->handle->installEventFilter(this);
    strip->setLocked(d->locked);
    auto* overflowMenu = new DismissOnlyMenu(strip->tail);
    overflowMenu->setObjectName("StripOverflowMenu");
    strip->tail->setPopupMenu(overflowMenu);
    connect(overflowMenu, &QMenu::aboutToShow, this, [this, id, overflowMenu] { populateOverflowMenu(id, overflowMenu); });
    strip->installEventFilter(this);
    d->strips.insert(id, strip);
    d->rows[qBound(0, row, 1)].append(id);
    normalizeElastic(qBound(0, row, 1), sizing == StripSizing::Elastic ? id : QString{});
    arrange();
    populateOverflowMenu(id, overflowMenu);
}

void CommandStripHost::addActionStrip(QString id, QString title, const QList<QAction*>& actions, int row, StripSizing sizing)
{
    if (id.isEmpty() || d->strips.contains(id)) return;
    auto* strip = new StripFrame(id, std::move(title), d->rowWidgets[qBound(0, row, 1)]);
    strip->sizing = sizing;
    int index = 0;
    for (auto* action : actions) {
        if (!action) continue;
        auto itemId = action->objectName();
        if (itemId.isEmpty()) itemId = QString::number(index);
        auto* button = new QToolButton(strip->viewport);
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setObjectName("StripCommandButton");
        button->setProperty("commandId", itemId);
        button->setAccessibleName(action->text());
        strip->items.append({itemId, action, button, true});
        connect(action, &QAction::changed, this, [this] { if (!d->arranging) arrange(); });
        ++index;
    }
    strip->handle->installEventFilter(this);
    strip->setLocked(d->locked);
    auto* overflowMenu = new DismissOnlyMenu(strip->tail);
    overflowMenu->setObjectName("StripOverflowMenu");
    strip->tail->setPopupMenu(overflowMenu);
    connect(overflowMenu, &QMenu::aboutToShow, this, [this, id, overflowMenu] { populateOverflowMenu(id, overflowMenu); });
    strip->installEventFilter(this);
    d->strips.insert(id, strip);
    d->rows[qBound(0, row, 1)].append(id);
    normalizeElastic(qBound(0, row, 1), sizing == StripSizing::Elastic ? id : QString{});
    arrange();
    populateOverflowMenu(id, overflowMenu);
}

void CommandStripHost::setLocked(bool locked)
{
    if (d->locked == locked) return;
    if (isDragging()) finishDrag(false);
    d->locked = locked;
    for (auto* strip : d->strips) strip->setLocked(locked);
    arrange(); changed();
}
bool CommandStripHost::isLocked() const { return d->locked; }
bool CommandStripHost::isDragging() const { return !d->dragging.isEmpty(); }
QWidget* CommandStripHost::stripWidget(const QString& id) const { return d->strips.value(id); }
QStringList CommandStripHost::stripIds() const { return d->rows[0] + d->rows[1]; }
QString CommandStripHost::stripTitle(const QString& id) const
{
    const auto* strip = d->strips.value(id);
    return strip ? strip->title : QString{};
}
QStringList CommandStripHost::stripOrder(int row) const { return row >= 0 && row < 2 ? d->rows[row] : QStringList{}; }
QStringList CommandStripHost::overflowItems(const QString& id) const
{
    const auto* strip = d->strips.value(id);
    return strip ? strip->overflow : QStringList{};
}

bool CommandStripHost::setStripVisible(const QString& id, bool visible)
{
    auto* strip = d->strips.value(id);
    if (!strip) return false;
    if (isDragging()) finishDrag(false);
    if (strip->visible == visible) return true;
    strip->visible = visible; arrange(); changed(); return true;
}
bool CommandStripHost::isStripVisible(const QString& id) const
{
    const auto* strip = d->strips.value(id);
    return strip && strip->visible;
}

bool CommandStripHost::setStripItemVisible(const QString& id, const QString& item, bool visible)
{
    auto* strip = d->strips.value(id);
    if (!strip) return false;
    for (auto& entry : strip->items) if (entry.id == item) {
        entry.included = visible; arrange(); changed(); return true;
    }
    if (item == QStringLiteral("content") && strip->content && strip->items.isEmpty()) {
        strip->content->setVisible(visible); arrange(); changed(); return true;
    }
    return false;
}
bool CommandStripHost::isStripItemVisible(const QString& id, const QString& item) const
{
    const auto* strip = d->strips.value(id);
    if (!strip) return false;
    for (const auto& entry : strip->items) if (entry.id == item) return entry.included;
    return item == QStringLiteral("content") && strip->content && !strip->content->isHidden();
}

bool CommandStripHost::setStripSizing(const QString& id, StripSizing sizing)
{
    auto* strip = d->strips.value(id);
    if (!strip || (sizing != StripSizing::Content && sizing != StripSizing::Elastic)
        || (sizing == StripSizing::Elastic && !strip->constraints.elasticAllowed)) return false;
    if (isDragging()) finishDrag(false);
    if (strip->sizing == sizing) return true;
    strip->sizing = sizing;
    for (int row = 0; row < 2; ++row) if (d->rows[row].contains(id)) clearWidthPriority(row);
    if (sizing == StripSizing::Elastic) for (int row = 0; row < 2; ++row)
        if (d->rows[row].contains(id)) normalizeElastic(row, id);
    arrange(); changed(); return true;
}

CommandStripHost::StripSizing CommandStripHost::stripSizing(const QString& id) const
{
    const auto* strip = d->strips.value(id);
    return strip ? strip->sizing : StripSizing::Content;
}

bool CommandStripHost::setStripOptionsEnabled(const QString& id, bool enabled)
{
    auto* strip = d->strips.value(id);
    if (!strip) return false;
    if (strip->optionsEnabled == enabled) return true;
    if (isDragging()) finishDrag(false);
    if (!enabled && strip->tail->menu()) strip->tail->menu()->hide();
    strip->optionsEnabled = enabled;
    arrange();
    return true;
}

bool CommandStripHost::isStripOptionsEnabled(const QString& id) const
{
    const auto* strip = d->strips.value(id);
    return strip && strip->optionsEnabled;
}

bool CommandStripHost::setStripConstraints(const QString& id, StripConstraints constraints)
{
    auto* strip = d->strips.value(id);
    if (!strip || constraints.minimumContentWidth <= 0
        || constraints.minimumContentWidth > QWIDGETSIZE_MAX - handleWidth - tailWidth) return false;
    if (isDragging()) finishDrag(false);
    strip->constraints = constraints;
    if (!constraints.elasticAllowed) strip->sizing = StripSizing::Content;
    if (strip->requestedWidth >= 0)
        strip->requestedWidth = std::max(strip->requestedWidth, strip->minimumWidthForLayout());
    arrange();
    return true;
}

CommandStripHost::StripConstraints CommandStripHost::stripConstraints(const QString& id) const
{
    const auto* strip = d->strips.value(id);
    return strip ? strip->constraints : StripConstraints{};
}

void CommandStripHost::normalizeElastic(int row, const QString& preferred)
{
    QString winner;
    for (const auto& id : d->rows[row]) {
        auto* strip = d->strips.value(id);
        if (!strip->constraints.elasticAllowed) strip->sizing = StripSizing::Content;
        if (strip->sizing == StripSizing::Elastic) winner = id;
    }
    if (!preferred.isEmpty() && d->rows[row].contains(preferred)
        && d->strips.value(preferred)->sizing == StripSizing::Elastic) winner = preferred;
    for (const auto& id : d->rows[row]) if (id != winner) d->strips.value(id)->sizing = StripSizing::Content;
}

void CommandStripHost::removeFromRows(const QString& id)
{
    auto* removed = d->strips.value(id);
    for (int row = 0; row < 2; ++row) {
        const int index = d->rows[row].indexOf(id);
        if (index < 0) continue;
        StripFrame* previous = nullptr;
        StripFrame* next = nullptr;
        for (int before = index - 1; before >= 0; --before) {
            auto* candidate = d->strips.value(d->rows[row][before]);
            if (candidate->visible) { previous = candidate; break; }
        }
        for (int after = index + 1; after < d->rows[row].size(); ++after) {
            auto* candidate = d->strips.value(d->rows[row][after]);
            if (candidate->visible) { next = candidate; break; }
        }
        if (removed->visible && previous && previous->sizing == StripSizing::Content) {
            if (!next || next->sizing == StripSizing::Elastic) previous->requestedWidth = -1;
            else {
                const int previousWidth = isDragging() ? d->originalBounds.value(previous->id).width() : previous->width();
                const int removedWidth = isDragging() ? d->originalBounds.value(id).width() : removed->width();
                previous->requestedWidth = int(std::min<qint64>(std::numeric_limits<int>::max(),
                    qint64(previousWidth) + removedWidth + stripSpacing));
            }
        }
        d->rows[row].removeAt(index);
    }
}

bool CommandStripHost::moveStrip(const QString& id, int row, int index)
{
    if (!d->strips.contains(id) || row < 0 || row > 1) return false;
    if (isDragging()) finishDrag(false);
    // An unchanged placement must not run the vacancy rule a second time.
    const int currentIndex = d->rows[row].indexOf(id);
    if (currentIndex >= 0 && currentIndex == qBound(0, index, int(d->rows[row].size()) - 1)) return true;
    for (int sourceRow = 0; sourceRow < 2; ++sourceRow)
        if (sourceRow == row || d->rows[sourceRow].contains(id)) clearWidthPriority(sourceRow);
    removeFromRows(id);
    d->rows[row].insert(qBound(0, index, int(d->rows[row].size())), id);
    normalizeElastic(row, d->strips.value(id)->sizing == StripSizing::Elastic ? id : QString{});
    arrange(); changed(); return true;
}
bool CommandStripHost::setStripWidth(const QString& id, int width)
{
    auto* strip = d->strips.value(id);
    if (!strip || width < strip->minimumWidthForLayout()) return false;
    if (isDragging()) finishDrag(false);
    for (int row = 0; row < 2; ++row) if (d->rows[row].contains(id)) clearWidthPriority(row);
    strip->requestedWidth = width; arrange(); changed(); return true;
}

bool CommandStripHost::resetStripWidth(const QString& id)
{
    auto* strip = d->strips.value(id);
    if (!strip) return false;
    if (isDragging()) finishDrag(false);
    strip->requestedWidth = -1;
    strip->naturalWidthPriority = false;
    if (strip->visible && strip->sizing == StripSizing::Content) {
        for (int row = 0; row < 2; ++row) if (d->rows[row].contains(id)) clearWidthPriority(row);
        strip->naturalWidthPriority = true;
    }
    arrange(); changed(); return true;
}

void CommandStripHost::clearWidthPriority(int row)
{
    for (const auto& id : d->rows[row]) d->strips.value(id)->naturalWidthPriority = false;
}

void CommandStripHost::normalizeWidthPriority(int row)
{
    QString winner;
    for (const auto& id : d->rows[row]) {
        auto* strip = d->strips.value(id);
        if (strip->sizing == StripSizing::Elastic || strip->requestedWidth >= 0)
            strip->naturalWidthPriority = false;
        if (strip->naturalWidthPriority) winner = id;
    }
    for (const auto& id : d->rows[row]) if (id != winner) d->strips.value(id)->naturalWidthPriority = false;
}
void CommandStripHost::refreshLayout() { arrange(); }

void CommandStripHost::setContextMenuCustomizer(std::function<void(QMenu*)> customizer)
{
    d->contextMenuCustomizer = std::move(customizer);
}
void CommandStripHost::setLayoutChangedCallback(std::function<void()> callback)
{
    d->layoutChanged = std::move(callback);
}
void CommandStripHost::setSizingChangeRequest(std::function<void(const QString&, StripSizing)> request)
{
    d->sizingChangeRequest = std::move(request);
}
void CommandStripHost::setWidthResetRequest(std::function<void(const QString&)> request)
{
    d->widthResetRequest = std::move(request);
}
void CommandStripHost::changed() { if (d->layoutChanged) d->layoutChanged(); }

QList<QWidget*> CommandStripHost::windowDragAreas() const
{
    QList<QWidget*> areas = {d->rowWidgets[0], d->rowWidgets[1]};
    for (auto* strip : d->strips) {
        areas.append(strip); areas.append(strip->viewport);
        if (strip->content && strip->content->property("windowDragArea").toBool()) areas.append(strip->content);
    }
    return areas;
}

QMenu* CommandStripHost::createContextMenu(QWidget* parent)
{
    auto* menu = new QMenu(parent);
    auto* lock = menu->addAction(QStringLiteral("锁定工具条"));
    lock->setObjectName("LockStripsAction"); lock->setCheckable(true); lock->setChecked(d->locked);
    connect(lock, &QAction::toggled, this, [this](bool value) { setLocked(value); });
    menu->addSeparator();
    for (const auto& id : stripIds()) {
        auto* strip = d->strips.value(id);
        auto* action = menu->addAction(strip->title);
        action->setObjectName("StripVisibilityAction"); action->setData(id);
        action->setCheckable(true); action->setChecked(strip->visible);
        connect(action, &QAction::toggled, this, [this, id](bool value) { setStripVisible(id, value); });
    }
    if (d->contextMenuCustomizer) { menu->addSeparator(); d->contextMenuCustomizer(menu); }
    return menu;
}

QMenu* CommandStripHost::createOverflowMenu(const QString& id, QWidget* parent)
{
    if (!isStripOptionsEnabled(id)) return nullptr;
    auto* menu = new DismissOnlyMenu(parent);
    populateOverflowMenu(id, menu);
    return menu;
}

void CommandStripHost::populateOverflowMenu(const QString& id, QMenu* menu)
{
    const auto* strip = d->strips.value(id);
    if (!strip || !menu) return;
    QPointer<QMenu> oldCustomize = menu->findChild<QMenu*>("StripCustomizeMenu", Qt::FindDirectChildrenOnly);
    menu->clear();
    delete oldCustomize;
    for (const auto& item : strip->items) if (strip->overflow.contains(item.id) && item.action) {
        menu->addAction(item.action);
    }
    if (!menu->actions().isEmpty()) menu->addSeparator();
    auto* reset = menu->addAction(QStringLiteral("重置尺寸"));
    reset->setObjectName("StripResetWidthAction");
    connect(reset, &QAction::triggered, this, [this, id] {
        if (d->widthResetRequest) d->widthResetRequest(id);
        else resetStripWidth(id);
    });
    auto* customize = new DismissOnlyMenu(QStringLiteral("添加或移除按钮"), menu);
    menu->addMenu(customize);
    customize->setObjectName("StripCustomizeMenu");
    if (strip->constraints.elasticAllowed) {
        auto* elastic = customize->addAction(QStringLiteral("弹性空间"));
        elastic->setObjectName("StripElasticAction");
        elastic->setCheckable(true);
        elastic->setChecked(strip->sizing == StripSizing::Elastic);
        connect(elastic, &QAction::toggled, this, [this, id](bool checked) {
            const auto sizing = checked ? StripSizing::Elastic : StripSizing::Content;
            if (d->sizingChangeRequest) d->sizingChangeRequest(id, sizing);
            else setStripSizing(id, sizing);
        });
        customize->addSeparator();
    }
    if (strip->items.isEmpty() && strip->content) {
        auto* action = customize->addAction(strip->title);
        action->setData(QStringLiteral("content")); action->setCheckable(true);
        action->setChecked(!strip->content->isHidden());
        connect(action, &QAction::toggled, this, [this, id](bool included) { setStripItemVisible(id, "content", included); });
    }
    for (const auto& item : strip->items) if (item.action) {
        auto* action = customize->addAction(item.action->icon(), item.action->text());
        action->setData(item.id); action->setCheckable(true); action->setChecked(item.included);
        connect(action, &QAction::toggled, this, [this, id, itemId = item.id](bool included) {
            setStripItemVisible(id, itemId, included);
        });
    }
}

QSize CommandStripHost::sizeHint() const
{
    int width = 0;
    int height = 0;
    for (int row = 0; row < 2; ++row) {
        int rowWidth = rowMargin * 2;
        bool visible = false;
        for (const auto& id : d->rows[row]) {
            const auto* strip = d->strips.value(id);
            if (!strip->visible) continue;
            rowWidth += strip->naturalWidth() + stripSpacing; visible = true;
        }
        if (visible) { height += rowHeight; width = std::max(width, rowWidth); }
    }
    return {width, height};
}
QSize CommandStripHost::minimumSizeHint() const
{
    int minimum = 0;
    for (int row = 0; row < 2; ++row) {
        int rowMinimum = rowMargin * 2;
        int count = 0;
        for (const auto& id : d->rows[row]) {
            const auto* strip = d->strips.value(id);
            if (!strip->visible) continue;
            rowMinimum += strip->minimumWidthForLayout();
            ++count;
        }
        if (count > 0) minimum = std::max(minimum, rowMinimum + (count - 1) * stripSpacing);
    }
    return {minimum, sizeHint().height()};
}

QVector<int> CommandStripHost::rowWidths(const QStringList& visible, int available) const
{
    QVector<int> sizes;
    qint64 total = 0;
    int elastic = -1;
    int priority = -1;
    for (int index = 0; index < visible.size(); ++index) {
        const auto* strip = d->strips.value(visible[index]);
        const bool isElastic = strip->sizing == StripSizing::Elastic;
        const int preferred = strip->requestedWidth < 0 ? strip->naturalWidth() : strip->requestedWidth;
        const int value = isElastic ? strip->minimumWidthForLayout() : std::max(strip->minimumWidthForLayout(), preferred);
        sizes.append(value); total += value;
        if (isElastic) elastic = index;
        if (strip->naturalWidthPriority) priority = index;
    }
    QVector<int> compressionOrder;
    const auto appendRange = [&compressionOrder](int begin, int end, int step) {
        for (int index = begin; index != end; index += step) compressionOrder.append(index);
    };
    const int leftEnd = elastic < 0 ? int(visible.size()) : elastic;
    if (priority >= 0 && (elastic < 0 || priority < elastic)) {
        // Keep the reset strip's head, giving way toward the trailing edge.
        appendRange(leftEnd - 1, priority, -1);
        compressionOrder.append(priority);
        appendRange(priority - 1, -1, -1);
        // Do not make room across an exhausted elastic strip: doing so would
        // restore a compressed prefix and move the head we promised to keep.
        if (elastic >= 0) appendRange(elastic + 1, int(visible.size()), 1);
    } else if (priority > elastic && elastic >= 0) {
        // Keep its tail and the following right-anchored strips unchanged.
        appendRange(elastic + 1, priority, 1);
        appendRange(leftEnd - 1, -1, -1);
        compressionOrder.append(priority);
        appendRange(priority + 1, int(visible.size()), 1);
    } else {
        appendRange(leftEnd - 1, -1, -1);
        if (elastic >= 0) appendRange(elastic + 1, int(visible.size()), 1);
    }
    for (const int index : compressionOrder) {
        if (total <= available) break;
        const int minimum = d->strips.value(visible[index])->minimumWidthForLayout();
        const int reduction = int(std::min<qint64>(total - available, sizes[index] - minimum));
        sizes[index] -= reduction; total -= reduction;
    }
    if (elastic >= 0) sizes[elastic] += int(std::max<qint64>(0, qint64(available) - total));
    return sizes;
}

void CommandStripHost::arrange()
{
    if (d->arranging) return;
    QScopedValueRollback arranging(d->arranging, true);
    // QMenuBar caches its size hint until its actions change. Apply inclusion
    // before measuring either strip, so customization takes effect this frame.
    for (auto* strip : d->strips) if (qobject_cast<QMenuBar*>(strip->content.data())) {
        for (const auto& item : strip->items) if (item.action) item.action->setVisible(item.included);
    }
    // Constrain the host rather than clipping protected final strip geometry.
    setMinimumWidth(minimumSizeHint().width());
    int y = 0;
    for (int row = 0; row < 2; ++row) {
        QStringList visible;
        for (const auto& id : d->rows[row])
            if (d->strips.value(id)->visible && (id != d->dragging || d->reserveDragSlot)) visible.append(id);
        auto* rowWidget = d->rowWidgets[row];
        const bool showRow = !visible.isEmpty() || isDragging();
        rowWidget->setVisible(showRow);
        rowWidget->setGeometry(0, y, width(), showRow ? rowHeight : 0);
        if (showRow) y += rowHeight;
        const int available = std::max(0, width() - rowMargin * 2 - std::max(0, int(visible.size()) - 1) * stripSpacing);
        const auto sizes = rowWidths(visible, available);
        const auto elasticId = std::find_if(visible.begin(), visible.end(), [this](const QString& id) {
            return d->strips.value(id)->sizing == StripSizing::Elastic;
        });
        const int elasticIndex = elasticId == visible.end() ? -1 : int(std::distance(visible.begin(), elasticId));
        QVector<int> positions(visible.size());
        int x = rowMargin;
        for (int index = 0; index < visible.size(); ++index) {
            positions[index] = x;
            x += sizes[index] + stripSpacing;
        }
        // Position the right block directly from the host's trailing edge.
        // Never accumulate its placement from a clipped or rounded left block.
        int right = width() - rowMargin;
        for (int index = int(visible.size()) - 1; elasticIndex >= 0 && index > elasticIndex; --index) {
            positions[index] = right - sizes[index];
            right = positions[index] - stripSpacing;
        }
        int index = 0;
        for (const auto& id : d->rows[row]) {
            auto* strip = d->strips.value(id);
            if (strip->parentWidget() != rowWidget) strip->setParent(rowWidget);
            const bool occupies = strip->visible && (id != d->dragging || d->reserveDragSlot);
            strip->setVisible(occupies && id != d->dragging);
            if (!occupies) continue;
            strip->setGeometry(positions[index], 0, sizes[index], rowHeight);
            strip->layoutItems(); ++index;
        }
    }
    setFixedHeight(y); updateGeometry(); update();
}

void CommandStripHost::resizeEvent(QResizeEvent* event) { QFrame::resizeEvent(event); arrange(); }

QJsonObject CommandStripHost::saveLayout() const
{
    QJsonArray rows;
    QJsonObject strips;
    for (int row = 0; row < 2; ++row) {
        QJsonArray entries;
        const auto& savedRow = isDragging() ? d->originalRows[row] : d->rows[row];
        for (const auto& id : savedRow) {
            entries.append(id);
            const auto* strip = d->strips.value(id);
            QJsonArray excluded;
            for (const auto& item : strip->items) if (!item.included) excluded.append(item.id);
            const int savedWidth = isDragging() ? d->originalWidths.value(id, -1) : strip->requestedWidth;
            const auto savedSizing = isDragging() ? d->originalSizing.value(id, strip->sizing) : strip->sizing;
            QJsonObject entry{{"visible", strip->visible}, {"width", savedWidth},
                {"sizing", savedSizing == StripSizing::Elastic ? "elastic" : "content"},
                {"excluded", excluded}, {"contentVisible", !strip->content || !strip->content->isHidden()}};
            const bool priority = isDragging() ? d->originalWidthPriorities.value(id, false) : strip->naturalWidthPriority;
            if (priority) entry.insert("naturalWidthPriority", true);
            strips.insert(id, entry);
        }
        rows.append(entries);
    }
    return {{"locked", d->locked}, {"rows", rows}, {"strips", strips}};
}

void CommandStripHost::restoreLayout(const QJsonObject& json)
{
    const auto rows = json.value("rows").toArray();
    if (rows.size() != 2) return;
    QStringList proposed[2]; QSet<QString> seen;
    for (int row = 0; row < 2; ++row) for (const auto& entry : rows[row].toArray()) {
        if (!entry.isString() || entry.toString().isEmpty()) return;
        const auto id = entry.toString();
        if (!d->strips.contains(id)) continue;
        if (seen.contains(id)) return;
        proposed[row].append(id); seen.insert(id);
    }
    // New strips may be introduced by an application upgrade. Keep their
    // defaults while preserving the saved position of the known strips.
    for (int row = 0; row < 2; ++row) for (const auto& id : d->rows[row]) if (!seen.contains(id)) proposed[row].append(id);
    const auto settings = json.value("strips").toObject();
    const bool savedLocked = json.value("locked").toBool(true);
    for (const auto& id : stripIds()) {
        if (!settings.contains(id)) continue;
        const auto value = settings.value(id).toObject();
        const int requested = value.value("width").toInt(-1);
        const int savedMinimumWidth = (d->strips.value(id)->optionsEnabled ? tailWidth : 0)
            + (savedLocked ? 0 : handleWidth) + 12;
        if (requested != -1 && requested < savedMinimumWidth) return;
        if (value.contains("sizing") && value.value("sizing") != "elastic" && value.value("sizing") != "content") return;
        if (value.contains("naturalWidthPriority") && !value.value("naturalWidthPriority").isBool()) return;
    }
    if (isDragging()) finishDrag(false);
    d->rows[0] = proposed[0]; d->rows[1] = proposed[1];
    d->locked = savedLocked;
    for (auto* strip : d->strips) {
        strip->setLocked(d->locked);
        if (!settings.contains(strip->id)) continue;
        const auto value = settings.value(strip->id).toObject();
        strip->visible = value.value("visible").toBool(true);
        strip->requestedWidth = value.value("width").toInt(-1);
        strip->naturalWidthPriority = value.value("naturalWidthPriority").toBool(false);
        if (value.contains("sizing")) strip->sizing = value.value("sizing") == "elastic" ? StripSizing::Elastic : StripSizing::Content;
        const auto excluded = value.value("excluded").toArray();
        for (auto& item : strip->items) item.included = !excluded.contains(item.id);
        if (strip->content && strip->items.isEmpty()) strip->content->setVisible(value.value("contentVisible").toBool(true));
        if (strip->requestedWidth >= 0)
            strip->requestedWidth = std::max(strip->requestedWidth, strip->minimumWidthForLayout());
    }
    normalizeElastic(0); normalizeElastic(1);
    normalizeWidthPriority(0); normalizeWidthPriority(1);
    arrange();
}

bool CommandStripHost::event(QEvent* event)
{
    if (event->type() == QEvent::UngrabMouse && isDragging()) finishDrag(false);
    return QFrame::event(event);
}

bool CommandStripHost::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::ContextMenu) {
        contextMenuEvent(static_cast<QContextMenuEvent*>(event)); return true;
    }
    if (watched->objectName() != QStringLiteral("StripHandle") || d->locked) return QFrame::eventFilter(watched, event);
    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            d->pendingStrip = watched->property("stripId").toString();
            d->pressPosition = mouse->globalPosition().toPoint();
            d->pressOffset = mouse->position().toPoint();
            return true;
        }
    } else if (event->type() == QEvent::MouseMove && !d->pendingStrip.isEmpty()) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->buttons().testFlag(Qt::LeftButton) &&
            (mouse->globalPosition().toPoint() - d->pressPosition).manhattanLength() >= QApplication::startDragDistance()) {
            beginDrag(d->pendingStrip, mouse->globalPosition().toPoint(), d->pressOffset);
            d->pendingStrip.clear(); return true;
        }
    } else if (event->type() == QEvent::MouseButtonRelease) d->pendingStrip.clear();
    return QFrame::eventFilter(watched, event);
}

void CommandStripHost::beginDrag(const QString& id, QPoint globalPosition, QPoint offset)
{
    auto* strip = d->strips.value(id);
    if (!strip || d->locked || isDragging()) return;
    d->originalRows[0] = d->rows[0]; d->originalRows[1] = d->rows[1];
    for (auto* entry : d->strips) {
        d->originalWidths.insert(entry->id, entry->requestedWidth);
        d->originalBounds.insert(entry->id, entry->geometry());
        d->originalSizing.insert(entry->id, entry->sizing);
        d->originalWidthPriorities.insert(entry->id, entry->naturalWidthPriority);
    }
    const auto pixmap = strip->grab();
    d->preview = new QLabel(this, Qt::Tool | Qt::FramelessWindowHint | Qt::BypassWindowManagerHint);
    d->preview->setObjectName("StripDragPreview");
    d->preview->setAttribute(Qt::WA_ShowWithoutActivating);
    d->preview->setAttribute(Qt::WA_TransparentForMouseEvents);
    d->preview->setPixmap(pixmap); d->preview->resize(strip->size());
    d->preview->move(globalPosition - offset); d->preview->show();
    d->dragging = id; d->pressOffset = offset;
    d->marker = new QFrame(this); d->marker->setObjectName("StripDropMarker");
    d->marker->setStyleSheet(QStringLiteral("background: %1;").arg(palette().color(QPalette::Highlight).name()));
    d->marker->setAttribute(Qt::WA_TransparentForMouseEvents);
    grabMouse(Qt::SizeAllCursor); setFocus(Qt::MouseFocusReason);
    arrange(); updateDrag(globalPosition);
}

bool CommandStripHost::resizeDragBoundary(int row, int requestedLeft)
{
    auto* dragged = d->strips.value(d->dragging);
    const int originalIndex = d->originalRows[row].indexOf(d->dragging);
    StripFrame* previous = nullptr;
    StripFrame* next = nullptr;
    bool elasticBefore = false;
    if (originalIndex >= 0) {
        for (int before = 0; before < originalIndex; ++before) {
            auto* candidate = d->strips.value(d->originalRows[row][before]);
            if (!candidate->visible) continue;
            previous = candidate;
            elasticBefore |= candidate->sizing == StripSizing::Elastic;
        }
        for (int after = originalIndex + 1; after < d->originalRows[row].size(); ++after) {
            auto* candidate = d->strips.value(d->originalRows[row][after]);
            if (candidate->visible) { next = candidate; break; }
        }
    }
    const bool adjustBoundary = previous
        && requestedLeft >= d->originalBounds.value(previous->id).left() + previous->minimumWidthForLayout() / 2
        && (!next || requestedLeft < d->originalBounds.value(next->id).left() + next->minimumWidthForLayout() / 2);
    if (!adjustBoundary) return false;
    // On the right of the elastic owner, resizing transfers width between
    // adjacent strips while the right edge and all following strips stay put.
    d->reserveDragSlot = true;
    const auto previousBounds = d->originalBounds.value(previous->id);
    int left = std::max(requestedLeft, previousBounds.left() + previous->minimumWidthForLayout() + stripSpacing);
    if (elasticBefore && dragged->sizing == StripSizing::Content) {
        const auto ownBounds = d->originalBounds.value(d->dragging);
        left = std::min(left, ownBounds.right() - dragged->minimumWidthForLayout() + 1);
        dragged->requestedWidth = ownBounds.right() - left + 1;
    } else {
        int trailingMinimum = dragged->minimumWidthForLayout();
        for (int after = originalIndex + 1; after < d->rows[row].size(); ++after) {
            const auto* candidate = d->strips.value(d->rows[row][after]);
            if (candidate->visible) trailingMinimum += candidate->minimumWidthForLayout() + stripSpacing;
        }
        left = std::min(left, width() - rowMargin - trailingMinimum);
    }
    if (previous->sizing == StripSizing::Content)
        previous->requestedWidth = std::max(previous->minimumWidthForLayout(), left - previousBounds.left() - stripSpacing);
    return true;
}

void CommandStripHost::insertDraggedStrip(int row, QPoint position)
{
    d->reserveDragSlot = false;
    removeFromRows(d->dragging);
    arrange();
    int index = 0;
    for (int itemIndex = 0; itemIndex < d->rows[row].size(); ++itemIndex) {
        auto* strip = d->strips.value(d->rows[row][itemIndex]);
        if (!strip->visible) continue;
        const auto bounds = strip->geometry();
        if (position.x() < bounds.left() + strip->minimumWidthForLayout() / 2) break;
        index = itemIndex + 1;
        if (position.x() < bounds.right()) break;
    }
    // At a new insertion point, the handle becomes the left neighbour's
    // boundary. Other rows already received the vacated source slot above.
    for (int previous = index - 1; previous >= 0; --previous) {
        auto* strip = d->strips.value(d->rows[row][previous]);
        if (!strip->visible) continue;
        if (strip->sizing == StripSizing::Content) {
            strip->requestedWidth = std::max(strip->minimumWidthForLayout(),
                position.x() - d->pressOffset.x() - strip->geometry().left() - stripSpacing);
        }
        break;
    }
    d->rows[row].insert(index, d->dragging);
    auto* dragged = d->strips.value(d->dragging);
    normalizeElastic(row, dragged->sizing == StripSizing::Elastic ? d->dragging : QString{});
    d->reserveDragSlot = true;
}

void CommandStripHost::updateDrag(QPoint globalPosition)
{
    if (!isDragging()) return;
    if (d->preview) d->preview->move(globalPosition - d->pressOffset);
    for (int row = 0; row < 2; ++row) d->rows[row] = d->originalRows[row];
    for (auto* strip : d->strips) {
        strip->requestedWidth = d->originalWidths.value(strip->id, -1);
        strip->sizing = d->originalSizing.value(strip->id, StripSizing::Content);
        strip->naturalWidthPriority = d->originalWidthPriorities.value(strip->id, false);
    }
    const QPoint position = mapFromGlobal(globalPosition);
    const int row = qBound(0, position.y() / rowHeight, 1);
    for (int affected = 0; affected < 2; ++affected)
        if (affected == row || d->originalRows[affected].contains(d->dragging)) clearWidthPriority(affected);
    if (!resizeDragBoundary(row, position.x() - d->pressOffset.x())) insertDraggedStrip(row, position);
    arrange();
    auto* dragged = d->strips.value(d->dragging);
    if (d->preview) {
        d->preview->setPixmap(dragged->grab());
        d->preview->resize(dragged->size());
    }
    if (d->marker) {
        d->marker->setGeometry(qBound(0, dragged->geometry().left(), std::max(0, width() - 2)), row * rowHeight, 2, rowHeight);
        d->marker->show(); d->marker->raise();
    }
}

void CommandStripHost::finishDrag(bool commit)
{
    if (!isDragging()) return;
    if (!commit) {
        d->rows[0] = d->originalRows[0]; d->rows[1] = d->originalRows[1];
        for (auto* strip : d->strips) {
            strip->requestedWidth = d->originalWidths.value(strip->id, -1);
            strip->sizing = d->originalSizing.value(strip->id, StripSizing::Content);
            strip->naturalWidthPriority = d->originalWidthPriorities.value(strip->id, false);
        }
    }
    d->dragging.clear(); d->pendingStrip.clear(); d->originalWidths.clear();
    d->originalBounds.clear(); d->originalSizing.clear(); d->reserveDragSlot = false;
    d->originalWidthPriorities.clear();
    if (QWidget::mouseGrabber() == this) releaseMouse();
    delete d->preview; delete d->marker;
    d->preview.clear(); d->marker.clear(); arrange();
    if (commit) changed();
}

void CommandStripHost::mouseMoveEvent(QMouseEvent* event)
{
    if (isDragging()) { updateDrag(event->globalPosition().toPoint()); event->accept(); }
    else QFrame::mouseMoveEvent(event);
}
void CommandStripHost::mouseReleaseEvent(QMouseEvent* event)
{
    if (isDragging() && event->button() == Qt::LeftButton) {
        updateDrag(event->globalPosition().toPoint());
        finishDrag(rect().adjusted(-12, -12, 12, 12).contains(mapFromGlobal(event->globalPosition().toPoint())));
        event->accept();
    } else QFrame::mouseReleaseEvent(event);
}
void CommandStripHost::keyPressEvent(QKeyEvent* event)
{
    if (isDragging() && event->key() == Qt::Key_Escape) { finishDrag(false); event->accept(); }
    else QFrame::keyPressEvent(event);
}
void CommandStripHost::contextMenuEvent(QContextMenuEvent* event)
{
    if (isDragging()) { event->accept(); return; }
    QPointer<QMenu> menu = createContextMenu(this); menu->exec(event->globalPos()); delete menu; event->accept();
}

} // namespace window_system
