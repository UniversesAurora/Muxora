// SPDX-License-Identifier: MIT
#include <window_system/interface_appearance.hpp>
#include <QCoreApplication>
#include "command_strip.hpp"
#include "strip_layout.hpp"
#include "strip_layout_state.hpp"
#include "commands/popup_menu.hpp"

#include <QAction>
#include <QVarLengthArray>
#include <QApplication>
#include <QContextMenuEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QStyleOptionToolButton>
#include <QStylePainter>
#include <QToolButton>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace window_system {
namespace {

// Rows have no outer gutter: like native caption buttons and VS Code's menu,
// the first and last strips touch the window border (or the screen edge).
constexpr int rowMargin = 0;

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
        setToolTip(QCoreApplication::translate("WindowSystem", "Toolbar options"));
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
        connect(popup, &QMenu::aboutToShow, this, [this, popup] {
            setPopupAnchor(popup, QRect(mapToGlobal(QPoint()), size()));
        });
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
        handle->setToolTip(QCoreApplication::translate("WindowSystem", "Drag to move the toolbar or resize the preceding toolbar"));
        handle->setAccessibleName(QCoreApplication::translate("WindowSystem", "%1 toolbar drag handle").arg(title));
        viewport->setObjectName("StripViewport");
        setProperty("windowDragArea", true);
        viewport->setProperty("windowDragArea", true);
    }

    void setLocked(bool locked)
    {
        locked_ = locked;
        handle->setVisible(!locked);
        handle->setCursor(locked ? Qt::ArrowCursor : Qt::SizeAllCursor);
        layoutItems();
    }

    [[nodiscard]] StripPolicyInput layoutPolicy() const
    {
        return {.elastic = sizing == CommandStripHost::StripSizing::Elastic,
            .elasticAllowed = constraints.elasticAllowed, .elasticRequired = constraints.elasticRequired,
            .naturalWidthPriority = naturalWidthPriority, .hasRequestedWidth = requestedWidth >= 0};
    }

    [[nodiscard]] int naturalWidth() const
    {
        int width = handle->isHidden() ? 0 : uiPixels(stripHandleWidth);
        if (content && !content->isHidden()) width += content->sizeHint().width();
        else for (const auto& item : items)
            if (item.included && item.action && item.action->isVisible() && item.button) width += item.button->sizeHint().width();
        return std::max(chromeMinimumWidth(), width + (optionsEnabled && !hideEmptyOptions() ? uiPixels(stripOptionsWidth) : 0));
    }

    [[nodiscard]] int minimumWidthForLayout() const
    {
        return constraints.compressible ? chromeMinimumWidth() : naturalWidth();
    }

    [[nodiscard]] int chromeMinimumWidth() const
    {
        // Keep the saved-width contract independent from this presentation
        // preference. A narrow strip must still have room to reveal overflow.
        return (optionsEnabled ? uiPixels(stripOptionsWidth) : 0) + (handle->isHidden() ? 0 : uiPixels(stripHandleWidth)) + uiPixels(constraints.minimumContentWidth);
    }

    void layoutItems()
    {
        const int handleSize = handle->isHidden() ? 0 : uiPixels(stripHandleWidth);
        // Decide from the complete available content width, before reserving
        // the tail. Otherwise reserving the tail itself can invent overflow.
        int contentWidth = 0;
        if (content && !content->isHidden()) contentWidth = content->sizeHint().width();
        else for (const auto& item : items)
            if (item.included && item.action && item.action->isVisible() && item.button)
                contentWidth += item.button->sizeHint().width();
        const bool optionsVisible = optionsEnabled && (!hideEmptyOptions() || contentWidth > width() - handleSize);
        const int optionsSize = optionsVisible ? uiPixels(stripOptionsWidth) : 0;
        handle->setGeometry(0, 0, handleSize, height());
        if (!optionsVisible && tail->menu() && tail->menu()->isVisible()) tail->menu()->close();
        tail->setVisible(optionsVisible);
        tail->setGeometry(std::max(0, width() - optionsSize), 0, optionsSize, height());
        viewport->setGeometry(handleSize, 0, std::max(0, width() - handleSize - optionsSize), height());
        overflow.clear();
        if (content) {
            // Menu actions retain natural geometry for our overflow menu;
            // ordinary content paints against the actual available viewport.
            const int contentGeometryWidth = qobject_cast<QMenuBar*>(content.data())
                ? std::max(viewport->width(), content->sizeHint().width()) : viewport->width();
            content->setGeometry(0, 0, contentGeometryWidth, height());
            if (auto* menu = qobject_cast<QMenuBar*>(content.data())) {
                for (const auto& item : items) {
                    if (!item.action) continue;
                    const auto available = item.action->property("commandMenuAvailable");
                    item.action->setVisible(item.included && (!available.isValid() || available.toBool()));
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
    bool hideOptionsWhenLocked = false;
    CommandStripHost::StripConstraints constraints;
    CommandStripHost::StripSizing sizing = CommandStripHost::StripSizing::Content;
    int requestedWidth = -1;
    bool naturalWidthPriority = false;

protected:
    void resizeEvent(QResizeEvent* event) override { QFrame::resizeEvent(event); layoutItems(); }
private:
    [[nodiscard]] bool hideEmptyOptions() const { return locked_ && hideOptionsWhenLocked; }
    bool locked_ = true;
};

QVarLengthArray<StripPolicyInput, 8> rowPolicies(const QStringList& row, const QHash<QString, StripFrame*>& strips)
{
    QVarLengthArray<StripPolicyInput, 8> result;
    for (const auto& id : row) result.append(strips.value(id)->layoutPolicy());
    return result;
}

} // namespace

struct CommandStripHost::Private {
    QHash<QString, StripFrame*> strips;
    QVector<QStringList> rows;
    QVector<Area> rowAreas;
    QVector<QWidget*> rowWidgets;
    QPointer<QFrame> bottom;
    int previewRows[2] = {-1, -1};
    int previewArea = -1;
    int dropRow = -1;
    bool locked = true;
    bool hideOptionsWhenLocked = false;
    bool arranging = false;
    double metricScale = interfaceScale();
    std::function<void(QMenu*)> contextMenuCustomizer;
    std::function<void()> layoutChanged;
    std::function<void(const QString&, StripSizing)> sizingChangeRequest;
    std::function<void(const QString&)> widthResetRequest;
    QString pendingStrip;
    QPoint pressPosition;
    QPoint pressOffset;
    QPoint lastDragPosition;
    QString dragging;
    QVector<QStringList> originalRows;
    QVector<Area> originalAreas;
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
    d->bottom = new QFrame(this);
    d->bottom->setObjectName("CommandStripHost");
    d->bottom->setProperty("toolbarArea", "bottom");
    d->bottom->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    d->bottom->setMinimumSize(0, 0);
    d->bottom->installEventFilter(this);
    ensureRow(Area::Top, 1);
    arrange();
}

CommandStripHost::~CommandStripHost()
{
    if (d->bottom) d->bottom->removeEventFilter(this);
    // Actions can outlive their presentations or emit while children tear down.
    // Stop callbacks before the private layout state is destroyed.
    for (auto* strip : d->strips) {
        strip->removeEventFilter(this);
        strip->handle->removeEventFilter(this);
        for (const auto& item : strip->items) if (item.action) disconnect(item.action, nullptr, this, nullptr);
        if (strip->tail->menu()) disconnect(strip->tail->menu(), nullptr, this, nullptr);
    }
    delete d->bottom.data();
}

void CommandStripHost::addStrip(QString id, QWidget* content, int row)
{
    addStrip(id, id, content, row);
}

void CommandStripHost::addStrip(QString id, QString title, QWidget* content, int row, StripSizing sizing)
{
    if (!content || id.isEmpty() || d->strips.contains(id)) return;
    auto* strip = new StripFrame(id, std::move(title), ensureRowWidget(ensureRow(Area::Top, std::max(0, row))));
    strip->content = content;
    strip->hideOptionsWhenLocked = d->hideOptionsWhenLocked;
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
        // See eventFilter(): the clipped part of a compressed menu bar must
        // not open the menus hidden there.
        menu->installEventFilter(this);
    }
    strip->handle->installEventFilter(this);
    strip->setLocked(d->locked);
    auto* overflowMenu = new DismissOnlyMenu(strip->tail);
    overflowMenu->setObjectName("StripOverflowMenu");
    strip->tail->setPopupMenu(overflowMenu);
    connect(overflowMenu, &QMenu::aboutToShow, this, [this, id, overflowMenu] { populateOverflowMenu(id, overflowMenu); });
    strip->installEventFilter(this);
    d->strips.insert(id, strip);
    d->rows[rowIndex(Area::Top, std::max(0, row))].append(id);
    normalizeElastic(rowIndex(Area::Top, std::max(0, row)), sizing == StripSizing::Elastic ? id : QString{});
    arrange();
    populateOverflowMenu(id, overflowMenu);
}

void CommandStripHost::addActionStrip(QString id, QString title, const QList<QAction*>& actions, int row, StripSizing sizing)
{
    if (id.isEmpty() || d->strips.contains(id)) return;
    auto* strip = new StripFrame(id, std::move(title), ensureRowWidget(ensureRow(Area::Top, std::max(0, row))));
    strip->sizing = sizing;
    int index = 0;
    strip->hideOptionsWhenLocked = d->hideOptionsWhenLocked;
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
    d->rows[rowIndex(Area::Top, std::max(0, row))].append(id);
    normalizeElastic(rowIndex(Area::Top, std::max(0, row)), sizing == StripSizing::Elastic ? id : QString{});
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
void CommandStripHost::setHideOptionsWhenLocked(bool hidden)
{
    if (d->hideOptionsWhenLocked == hidden) return;
    d->hideOptionsWhenLocked = hidden;
    for (auto* strip : d->strips) strip->hideOptionsWhenLocked = hidden;
    arrange();
}
bool CommandStripHost::hideOptionsWhenLocked() const { return d->hideOptionsWhenLocked; }
bool CommandStripHost::removeStrip(const QString& id)
{
    auto* strip = d->strips.value(id);
    if (!strip) return false;
    const QPointer<CommandStripHost> retained(this);
    if (isDragging()) finishDrag(false);
    if (!retained) return false;
    strip = d->strips.value(id);
    if (!strip) return false;
    strip->removeEventFilter(this);
    strip->handle->removeEventFilter(this);
    for (const auto& item : strip->items)
        if (item.action) disconnect(item.action, nullptr, this, nullptr);
    if (strip->tail->menu()) disconnect(strip->tail->menu(), nullptr, this, nullptr);
    removeFromRows(id);
    d->strips.remove(id);
    if (d->pendingStrip == id) d->pendingStrip.clear();
    delete strip;
    arrange(); changed();
    return true;
}
bool CommandStripHost::isDragging() const { return !d->dragging.isEmpty(); }
QWidget* CommandStripHost::stripWidget(const QString& id) const { return d->strips.value(id); }
QStringList CommandStripHost::stripIds() const
{
    QStringList result;
    for (const auto& row : d->rows) result.append(row);
    return result;
}
QString CommandStripHost::stripTitle(const QString& id) const
{
    const auto* strip = d->strips.value(id);
    return strip ? strip->title : QString{};
}
QStringList CommandStripHost::stripOrder(int row) const { return stripOrder(Area::Top, row); }
QStringList CommandStripHost::stripOrder(Area area, int row) const
{
    const int index = rowIndex(area, row);
    return index < 0 ? QStringList{} : d->rows[index];
}
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
        || (sizing == StripSizing::Content && strip->constraints.elasticRequired)
        || (sizing == StripSizing::Elastic && !strip->constraints.elasticAllowed)) return false;
    if (sizing == StripSizing::Elastic) for (int row = 0; row < d->rows.size(); ++row)
        if ((isDragging() ? d->originalRows.value(row) : d->rows[row]).contains(id)
            && !requiredElasticInRow(row, id, true).isEmpty()) return false;
    if (isDragging()) finishDrag(false);
    if (strip->sizing == sizing) return true;
    strip->sizing = sizing;
    for (int row = 0; row < d->rows.size(); ++row) if (d->rows[row].contains(id)) clearWidthPriority(row);
    if (sizing == StripSizing::Elastic) for (int row = 0; row < d->rows.size(); ++row)
        if (d->rows[row].contains(id)) normalizeElastic(row, id);
    arrange(); changed(); return true;
}

CommandStripHost::StripSizing CommandStripHost::stripSizing(const QString& id) const
{
    const auto* strip = d->strips.value(id);
    return strip ? strip->sizing : StripSizing::Content;
}

bool CommandStripHost::isStripSizingEditable(const QString& id) const
{
    const auto* strip = d->strips.value(id);
    if (!strip || !strip->constraints.elasticAllowed || strip->constraints.elasticRequired) return false;
    for (int row = 0; row < d->rows.size(); ++row)
        if ((isDragging() ? d->originalRows.value(row) : d->rows[row]).contains(id)
            && !requiredElasticInRow(row, id, true).isEmpty()) return false;
    return true;
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
        || constraints.minimumContentWidth > QWIDGETSIZE_MAX - uiPixels(stripHandleWidth) - uiPixels(stripOptionsWidth)
        || (constraints.elasticRequired && !constraints.elasticAllowed)) return false;
    if (constraints.elasticRequired) for (int row = 0; row < d->rows.size(); ++row)
        if ((isDragging() ? d->originalRows.value(row) : d->rows[row]).contains(id)
            && !requiredElasticInRow(row, id, true).isEmpty()) return false;
    if (isDragging()) finishDrag(false);
    strip->constraints = constraints;
    if (!constraints.elasticAllowed) strip->sizing = StripSizing::Content;
    for (int row = 0; row < d->rows.size(); ++row) if (d->rows[row].contains(id)) normalizeElastic(row);
    if (constraints.fixedWidth) strip->requestedWidth = -1;
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

QString CommandStripHost::requiredElasticInRow(int row, const QString& except, bool committed) const
{
    const auto& entries = committed && isDragging() ? d->originalRows.value(row) : d->rows[row];
    for (const auto& id : entries)
        if (id != except && d->strips.value(id)->constraints.elasticRequired) return id;
    return {};
}

void CommandStripHost::normalizeElastic(int row, const QString& preferred)
{
    const auto& entries = d->rows[row];
    const auto policies = rowPolicies(entries, d->strips);
    const int winner = stripElasticOwner({policies.constData(), size_t(policies.size())}, int(entries.indexOf(preferred)));
    for (int index = 0; index < entries.size(); ++index)
        d->strips.value(entries[index])->sizing = index == winner ? StripSizing::Elastic : StripSizing::Content;
}

void CommandStripHost::removeFromRows(const QString& id)
{
    const bool dragging = isDragging();
    for (auto& row : d->rows) {
        const int index = int(row.indexOf(id));
        if (index < 0) continue;
        QVarLengthArray<StripSlotInput, 8> slotInputs;
        for (const auto& entry : row) {
            const auto* strip = d->strips.value(entry);
            slotInputs.append({dragging ? d->originalBounds.value(entry).width() : strip->width(),
                strip->visible, strip->sizing == StripSizing::Elastic, strip->constraints.fixedWidth});
        }
        const auto adjustment = stripVacancyAdjustment({slotInputs.constData(), size_t(slotInputs.size())}, index, uiPixels(stripSpacing));
        if (adjustment) d->strips.value(row[adjustment->index])->requestedWidth = adjustment->requestedWidth;
        row.removeAt(index);
    }
}

QWidget* CommandStripHost::bottomArea() const { return d->bottom.data(); }
QWidget* CommandStripHost::areaWidget(Area area) const
{
    return area == Area::Top ? const_cast<CommandStripHost*>(this) : d->bottom.data();
}
int CommandStripHost::rowIndex(Area area, int row) const
{
    return stripRowIndex({d->rowAreas.constData(), size_t(d->rowAreas.size())}, area, row);
}
int CommandStripHost::ensureRow(Area area, int row)
{
    const int result = ensureStripRow(d->rows, d->rowAreas, area, row);
    while (d->rowWidgets.size() < d->rows.size()) d->rowWidgets.append(nullptr);
    return result;
}

QWidget* CommandStripHost::ensureRowWidget(int row)
{
    if (row < 0 || row >= d->rows.size()) return nullptr;
    auto*& widget = d->rowWidgets[row];
    if (!widget) {
        widget = new QWidget(areaWidget(d->rowAreas[row]));
        widget->setObjectName("CommandStripRow");
        widget->setProperty("windowDragArea", true);
    } else if (widget->parentWidget() != areaWidget(d->rowAreas[row])) {
        widget->setParent(areaWidget(d->rowAreas[row]));
    }
    return widget;
}
void CommandStripHost::destroyRowWidget(int row)
{
    auto* widget = d->rowWidgets.value(row);
    if (!widget) return;
    // A strip can already be assigned to another logical row while its old
    // parent remains until arrangement. Keep its widget and contents alive.
    for (auto* strip : d->strips)
        if (strip->parentWidget() == widget) strip->setParent(this);
    d->rowWidgets[row] = nullptr;
    delete widget;
}
int CommandStripHost::rowCount(Area area) const
{
    int count = 0;
    for (int row = 0; row < d->rows.size(); ++row) if (d->rowAreas[row] == area) {
        const bool occupied = std::any_of(d->rows[row].cbegin(), d->rows[row].cend(), [this](const QString& id) {
            return d->strips.value(id)->visible && (id != d->dragging || d->reserveDragSlot);
        });
        count += occupied ? 1 : 0;
    }
    return count;
}
CommandStripHost::Area CommandStripHost::stripArea(const QString& id) const
{
    for (int row = 0; row < d->rows.size(); ++row)
        if (d->rows[row].contains(id)) return d->rowAreas[row];
    return Area::Top;
}
int CommandStripHost::rowExtent(int row) const
{
    int extent = std::max(uiPixels(compactRowHeight), fontMetrics().height() + 8);
    for (const auto& id : d->rows[row]) {
        const auto* strip = d->strips.value(id);
        if (strip->visible && strip->content && !strip->content->isHidden())
            extent = std::max({extent, strip->content->minimumSizeHint().height(), strip->content->minimumHeight()});
    }
    return extent;
}
void CommandStripHost::replaceRows(QVector<QStringList> rows, QVector<Area> areas, bool retainSpareRows)
{
    // Reparent presentations before releasing rows. Their contents and action
    // connections remain the same objects across area changes and restores.
    for (auto* strip : d->strips) strip->setParent(this);
    if (!retainSpareRows) while (d->rowWidgets.size() > rows.size()) {
        destroyRowWidget(int(d->rowWidgets.size()) - 1);
        d->rowWidgets.removeLast();
    }
    else for (int index = int(rows.size()); index < d->rowWidgets.size(); ++index)
        if (d->rowWidgets[index]) d->rowWidgets[index]->hide();
    while (d->rowWidgets.size() < rows.size()) d->rowWidgets.append(nullptr);
    d->rows = std::move(rows);
    d->rowAreas = std::move(areas);
    for (int row = 0; row < d->rows.size(); ++row) {
        if (!d->rows[row].isEmpty()) {
            if (d->rowWidgets[row]) d->rowWidgets[row]->setParent(areaWidget(d->rowAreas[row]));
        }
        else if (!retainSpareRows) destroyRowWidget(row);
        else if (d->rowWidgets[row]) d->rowWidgets[row]->hide();
    }
}
bool CommandStripHost::moveStrip(const QString& id, int row, int index)
{
    return moveStrip(id, Area::Top, row, index);
}
bool CommandStripHost::moveStrip(const QString& id, Area area, int row, int index)
{
    if (!d->strips.contains(id) || row < 0 || (area != Area::Top && area != Area::Bottom)) return false;
    const int existing = rowIndex(area, row);
    if (existing >= 0 && d->strips.value(id)->constraints.elasticRequired
        && !requiredElasticInRow(existing, id, true).isEmpty()) return false;
    if (isDragging()) finishDrag(false);
    const int target = ensureRow(area, row);
    // An unchanged placement must not run the vacancy rule a second time.
    const int currentIndex = d->rows[target].indexOf(id);
    if (currentIndex >= 0 && currentIndex == qBound(0, index, int(d->rows[target].size()) - 1)) return true;
    for (int sourceRow = 0; sourceRow < d->rows.size(); ++sourceRow)
        if (sourceRow == target || d->rows[sourceRow].contains(id)) clearWidthPriority(sourceRow);
    removeFromRows(id);
    d->rows[target].insert(qBound(0, index, int(d->rows[target].size())), id);
    normalizeElastic(target, d->strips.value(id)->sizing == StripSizing::Elastic ? id : QString{});
    arrange(); changed(); return true;
}
bool CommandStripHost::setStripWidth(const QString& id, int width)
{
    auto* strip = d->strips.value(id);
    if (!strip || strip->constraints.fixedWidth || width < strip->minimumWidthForLayout()) return false;
    if (isDragging()) finishDrag(false);
    for (int row = 0; row < d->rows.size(); ++row) if (d->rows[row].contains(id)) clearWidthPriority(row);
    strip->requestedWidth = uiPixels(width); arrange(); changed(); return true;
}

bool CommandStripHost::resetStripWidth(const QString& id)
{
    auto* strip = d->strips.value(id);
    if (!strip) return false;
    if (isDragging()) finishDrag(false);
    strip->requestedWidth = -1;
    strip->naturalWidthPriority = false;
    if (strip->visible && strip->sizing == StripSizing::Content) {
        for (int row = 0; row < d->rows.size(); ++row) if (d->rows[row].contains(id)) clearWidthPriority(row);
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
    const auto& entries = d->rows[row];
    const auto policies = rowPolicies(entries, d->strips);
    const int winner = stripWidthPriorityOwner({policies.constData(), size_t(policies.size())});
    for (int index = 0; index < entries.size(); ++index)
        d->strips.value(entries[index])->naturalWidthPriority = index == winner;
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
    QList<QWidget*> areas;
    areas.append(d->bottom.data());
    for (auto* row : d->rowWidgets) if (row) areas.append(row);
    for (auto* strip : d->strips) {
        areas.append(strip); areas.append(strip->viewport);
        if (strip->content && strip->content->property("windowDragArea").toBool()) areas.append(strip->content);
    }
    return areas;
}

QMenu* CommandStripHost::createContextMenu(QWidget* parent)
{
    auto* menu = new DismissOnlyMenu(parent);
    auto* lock = menu->addAction(QCoreApplication::translate("WindowSystem", "Lock toolbars"));
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
    auto* reset = menu->addAction(QCoreApplication::translate("WindowSystem", "Reset size"));
    reset->setObjectName("StripResetWidthAction");
    connect(reset, &QAction::triggered, this, [this, id] {
        if (d->widthResetRequest) d->widthResetRequest(id);
        else resetStripWidth(id);
    });
    if (isStripSizingEditable(id)) {
        auto* elastic = menu->addAction(QCoreApplication::translate("WindowSystem", "Flexible space"));
        elastic->setObjectName("StripElasticAction");
        elastic->setCheckable(true);
        elastic->setChecked(strip->sizing == StripSizing::Elastic);
        connect(elastic, &QAction::toggled, this, [this, id](bool checked) {
            const auto sizing = checked ? StripSizing::Elastic : StripSizing::Content;
            if (d->sizingChangeRequest) d->sizingChangeRequest(id, sizing);
            else setStripSizing(id, sizing);
        });
    }
    auto* customize = new DismissOnlyMenu(QCoreApplication::translate("WindowSystem", "Add or remove buttons"), menu);
    menu->addMenu(customize);
    customize->setObjectName("StripCustomizeMenu");
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

QSize CommandStripHost::areaSizeHint(Area area, bool minimum) const
{
    int width = 0;
    int height = 0;
    for (int row = 0; row < d->rows.size(); ++row) {
        if (d->rowAreas[row] != area) continue;
        int rowWidth = uiPixels(rowMargin) * 2;
        int count = 0;
        for (const auto& id : d->rows[row]) {
            const auto* strip = d->strips.value(id);
            if (!strip->visible) continue;
            rowWidth += minimum ? strip->minimumWidthForLayout() : strip->naturalWidth();
            ++count;
        }
        if (count) {
            height += rowExtent(row);
            width = std::max(width, rowWidth + (count - 1) * uiPixels(stripSpacing));
        }
    }
    return {width, height};
}
QSize CommandStripHost::sizeHint() const { return areaSizeHint(Area::Top, false); }
QSize CommandStripHost::minimumSizeHint() const { return areaSizeHint(Area::Top, true); }

StripRowLayout CommandStripHost::rowLayout(const QStringList& visible, int width) const
{
    QVarLengthArray<StripWidthInput, 8> inputs;
    for (const auto& id : visible) {
        const auto* strip = d->strips.value(id);
        const int preferred = strip->requestedWidth < 0 || strip->constraints.fixedWidth
            ? strip->naturalWidth() : strip->requestedWidth;
        inputs.append({strip->minimumWidthForLayout(), preferred,
            strip->sizing == StripSizing::Elastic, strip->naturalWidthPriority});
    }
    return layoutStripRow({inputs.constData(), size_t(inputs.size())}, width, uiPixels(rowMargin), uiPixels(stripSpacing));
}

void CommandStripHost::arrange()
{
    if (d->arranging) return;
    QScopedValueRollback arranging(d->arranging, true);
    // QMenuBar caches its size hint until its actions change. Apply inclusion
    // before measuring either strip, so customization takes effect this frame.
    for (auto* strip : d->strips) if (auto* menu = qobject_cast<QMenuBar*>(strip->content.data())) {
        QList<ActionItem> current;
        for (auto* action : menu->actions()) {
            const auto old = std::find_if(strip->items.cbegin(), strip->items.cend(), [action](const auto& item) { return item.action == action; });
            current.append(old != strip->items.cend() ? *old : ActionItem{action->objectName(), action, {}, true});
        }
        strip->items = std::move(current);
        for (const auto& item : strip->items) if (item.action) {
            const auto available = item.action->property("commandMenuAvailable");
            item.action->setVisible(item.included && (!available.isValid() || available.toBool()));
        }
    }
    // Each dock area owns its vertical geometry; both use one strip registry.
    setMinimumWidth(std::max(areaSizeHint(Area::Top, true).width(), areaSizeHint(Area::Bottom, true).width()));
    if (d->bottom) d->bottom->setMinimumWidth(areaSizeHint(Area::Bottom, true).width());
    QVarLengthArray<StripAreaRowInput, 8> rowInputs;
    for (int row = 0; row < d->rows.size(); ++row) {
        const bool occupied = std::any_of(d->rows[row].cbegin(), d->rows[row].cend(), [this](const QString& id) {
            return d->strips.value(id)->visible && (id != d->dragging || d->reserveDragSlot);
        });
        const bool show = occupied || row == d->previewRows[int(d->rowAreas[row])];
        rowInputs.append({d->rowAreas[row], show ? rowExtent(row) : 0, show});
    }
    auto areas = layoutStripAreas({rowInputs.constData(), size_t(rowInputs.size())});
    setFixedHeight(areas.heights[0]);
    if (d->bottom) d->bottom->setFixedHeight(areas.heights[1]);
    for (int row = 0; row < d->rows.size(); ++row) {
        const Area area = d->rowAreas[row];
        auto* host = areaWidget(area);
        QStringList visible;
        for (const auto& id : d->rows[row])
            if (d->strips.value(id)->visible && (id != d->dragging || d->reserveDragSlot)) visible.append(id);
        const bool preview = row == d->previewRows[int(area)];
        const bool showRow = !visible.isEmpty() || preview;
        if (!showRow) {
            destroyRowWidget(row);
            for (const auto& id : d->rows[row]) {
                auto* strip = d->strips.value(id);
                if (strip->parentWidget() != this) strip->setParent(this);
                strip->hide();
            }
            continue;
        }
        auto* rowWidget = ensureRowWidget(row);
        const int extent = rowExtent(row);
        const int y = areas.takeRow(area, extent);
        rowWidget->setVisible(showRow);
        rowWidget->setGeometry(0, y, host->width(), showRow ? extent : 0);
        const auto layout = rowLayout(visible, host->width());
        int index = 0;
        for (const auto& id : d->rows[row]) {
            auto* strip = d->strips.value(id);
            if (strip->parentWidget() != rowWidget) strip->setParent(rowWidget);
            const bool occupies = strip->visible && (id != d->dragging || d->reserveDragSlot);
            strip->setVisible(occupies && id != d->dragging);
            if (!occupies) continue;
            strip->setGeometry(layout.positions[index], 0, layout.widths[index], extent);
            strip->layoutItems(); ++index;
        }
    }
    while (d->rowWidgets.size() > d->rows.size()) {
        destroyRowWidget(int(d->rowWidgets.size()) - 1);
        d->rowWidgets.removeLast();
    }
    updateGeometry(); update();
    if (d->bottom) { d->bottom->updateGeometry(); d->bottom->update(); }
}

void CommandStripHost::resizeEvent(QResizeEvent* event) { QFrame::resizeEvent(event); arrange(); }

int CommandStripHost::minimumSavedWidth(bool optionsEnabled, bool locked)
{
    return minimumStripSavedWidth(optionsEnabled, locked);
}
QJsonObject CommandStripHost::saveLayout() const
{
    const bool dragging = isDragging();
    StripLayoutSnapshot snapshot;
    snapshot.rows = dragging ? d->originalRows : d->rows;
    snapshot.areas = dragging ? d->originalAreas : d->rowAreas;
    snapshot.locked = d->locked;
    for (const auto& row : snapshot.rows) for (const auto& id : row) {
        const auto* strip = d->strips.value(id);
        StripLayoutPreferences value;
        value.visible = strip->visible;
        const int width = dragging ? d->originalWidths.value(id, -1) : strip->requestedWidth;
        value.width = width < 0 ? -1 : qRound(width / interfaceScale());
        const auto sizing = dragging ? d->originalSizing.value(id, strip->sizing) : strip->sizing;
        value.elastic = sizing == StripSizing::Elastic;
        for (const auto& item : strip->items) if (!item.included) value.excluded.append(item.id);
        value.contentVisible = !strip->content || !strip->content->isHidden();
        value.naturalWidthPriority = dragging ? d->originalWidthPriorities.value(id, false) : strip->naturalWidthPriority;
        snapshot.strips.insert(id, std::move(value));
    }
    return encodeStripLayout(snapshot);
}

void CommandStripHost::restoreLayout(const QJsonObject& json)
{
    QHash<QString, StripRestorePolicy> policies;
    for (auto* strip : d->strips) policies.insert(strip->id,
        {strip->optionsEnabled, strip->constraints.elasticRequired, strip->id == QStringLiteral("caption-controls")});
    const auto restored = decodeStripLayout(json, d->rows, d->rowAreas, policies);
    if (!restored) return;
    if (isDragging()) finishDrag(false);
    replaceRows(restored->rows, restored->areas);
    d->locked = restored->locked;
    for (auto* strip : d->strips) {
        strip->setLocked(d->locked);
        const auto found = restored->strips.constFind(strip->id);
        if (found == restored->strips.cend()) continue;
        const auto& value = *found;
        strip->visible = value.visible;
        strip->requestedWidth = value.width < 0 ? -1 : uiPixels(value.width);
        strip->naturalWidthPriority = value.naturalWidthPriority;
        if (value.elastic) strip->sizing = *value.elastic ? StripSizing::Elastic : StripSizing::Content;
        for (auto& item : strip->items) item.included = !value.excluded.contains(item.id);
        if (strip->content && strip->items.isEmpty()) strip->content->setVisible(value.contentVisible);
        if (strip->constraints.fixedWidth) strip->requestedWidth = -1;
        if (strip->requestedWidth >= 0)
            strip->requestedWidth = std::max(strip->requestedWidth, strip->minimumWidthForLayout());
    }
    for (int row = 0; row < d->rows.size(); ++row) { normalizeElastic(row); normalizeWidthPriority(row); }
    arrange();
}

bool CommandStripHost::event(QEvent* event)
{
    if (d && !qFuzzyCompare(d->metricScale, interfaceScale())) {
        const double factor = interfaceScale() / d->metricScale;
        for (auto* strip : d->strips) if (strip->requestedWidth >= 0) strip->requestedWidth = qRound(strip->requestedWidth * factor);
        d->metricScale = interfaceScale();
    }
    if (event->type() == QEvent::UngrabMouse && isDragging()) finishDrag(false);
    if (event->type() == QEvent::FontChange && d->bottom) d->bottom->setFont(font());
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) arrange();
    return QFrame::event(event);
}

bool CommandStripHost::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == d->bottom && (event->type() == QEvent::Resize || event->type() == QEvent::FontChange
        || event->type() == QEvent::StyleChange)) arrange();
    if (auto* bar = qobject_cast<QMenuBar*>(watched)) {
        // A compressed strip clips its menu bar; the clipped menus are listed
        // in the strip's overflow menu instead. While one menu is open, Qt
        // forwards pointer moves to the bar by position alone, so moving
        // across the clipped area would open an invisible menu. Only the
        // visible part of the bar reacts to the pointer.
        const auto type = event->type();
        if (type == QEvent::MouseMove || type == QEvent::MouseButtonPress
            || type == QEvent::MouseButtonRelease || type == QEvent::MouseButtonDblClick) {
            const QPoint position = static_cast<QMouseEvent*>(event)->position().toPoint();
            if (bar->rect().contains(position) && !bar->visibleRegion().contains(position)) return true;
        }
        return false; // Including its context menu, which the window handles.
    }
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
    d->originalRows = d->rows; d->originalAreas = d->rowAreas;
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
    d->preview->setAttribute(Qt::WA_WindowPropagation);
    d->preview->setPixmap(pixmap); d->preview->resize(strip->size());
    d->preview->move(globalPosition - offset); d->preview->show();
    d->dragging = id; d->pressOffset = offset;
    d->marker = new QFrame(this); d->marker->setObjectName("StripDropMarker");
    d->marker->setStyleSheet(QStringLiteral("background: %1;").arg(palette().color(QPalette::Highlight).name()));
    d->marker->setAttribute(Qt::WA_TransparentForMouseEvents);
    grabMouse(Qt::SizeAllCursor); setFocus(Qt::MouseFocusReason);
    updateDrag(globalPosition);
}

bool CommandStripHost::resizeDragBoundary(int row, int requestedLeft)
{
    auto* dragged = d->strips.value(d->dragging);
    const int originalIndex = d->originalRows.value(row).indexOf(d->dragging);
    StripFrame* previous = nullptr;
    StripFrame* next = nullptr;
    bool elasticBefore = false;
    if (originalIndex >= 0) {
        for (int before = 0; before < originalIndex; ++before) {
            auto* candidate = d->strips.value(d->originalRows.value(row)[before]);
            if (!candidate->visible) continue;
            previous = candidate;
            elasticBefore |= candidate->sizing == StripSizing::Elastic;
        }
        for (int after = originalIndex + 1; after < d->originalRows.value(row).size(); ++after) {
            auto* candidate = d->strips.value(d->originalRows.value(row)[after]);
            if (candidate->visible) { next = candidate; break; }
        }
    }
    // Dragging a handle moves the boundary at the strip's left edge. The strip
    // on the row's anchored side of that boundary changes width; the strips on
    // the free side keep theirs and move, and the elastic strip absorbs the
    // difference. Left of the elastic strip (anchored to the left edge) that is
    // the previous strip, and the dragged one moves with the pointer. Right of
    // it (anchored to the right edge) it is the dragged strip itself: its left
    // edge follows the pointer, its right edge stays, and the strips before it
    // only move. A fixed-width strip on the anchored side cannot change, so the
    // boundary stays and the drag can only reorder.
    const bool rightOfElastic = elasticBefore && dragged->sizing == StripSizing::Content;
    auto* resized = rightOfElastic ? dragged : previous;
    const bool adjustBoundary = previous && resized && resized->sizing == StripSizing::Content
        && !resized->constraints.fixedWidth
        && requestedLeft >= d->originalBounds.value(previous->id).left() + previous->minimumWidthForLayout() / 2
        && (!next || requestedLeft < d->originalBounds.value(next->id).left() + next->minimumWidthForLayout() / 2);
    if (!adjustBoundary) return false;
    d->reserveDragSlot = true;
    if (rightOfElastic) {
        const auto ownBounds = d->originalBounds.value(d->dragging);
        const int left = std::min(requestedLeft, ownBounds.right() - dragged->minimumWidthForLayout() + 1);
        dragged->requestedWidth = ownBounds.right() - left + 1;
        return true;
    }
    const auto previousBounds = d->originalBounds.value(previous->id);
    int left = std::max(requestedLeft, previousBounds.left() + previous->minimumWidthForLayout() + uiPixels(stripSpacing));
    int trailingMinimum = dragged->minimumWidthForLayout();
    for (int after = originalIndex + 1; after < d->rows[row].size(); ++after) {
        const auto* candidate = d->strips.value(d->rows[row][after]);
        if (candidate->visible) trailingMinimum += candidate->minimumWidthForLayout() + uiPixels(stripSpacing);
    }
    left = std::min(left, areaWidget(d->rowAreas[row])->width() - uiPixels(rowMargin) - trailingMinimum);
    previous->requestedWidth = std::max(previous->minimumWidthForLayout(), left - previousBounds.left() - uiPixels(stripSpacing));
    return true;
}

void CommandStripHost::insertDraggedStrip(int row, QPoint position)
{
    if (d->strips.value(d->dragging)->constraints.elasticRequired
        && !requiredElasticInRow(row, d->dragging).isEmpty()) return;
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
    // At a new insertion point the handle becomes the boundary with the left
    // neighbour, resized as in resizeDragBoundary(): left of the elastic strip
    // the left neighbour reaches the pointer and the inserted strip keeps its
    // width; right of it the inserted strip spans from the pointer to its
    // right neighbour, and the left neighbour keeps its width. Other rows
    // already received the vacated source slot above.
    auto* dragged = d->strips.value(d->dragging);
    const int handle = position.x() - d->pressOffset.x();
    bool rightOfElastic = false;
    for (int before = 0; before < index; ++before) {
        const auto* strip = d->strips.value(d->rows[row][before]);
        rightOfElastic |= strip->visible && strip->sizing == StripSizing::Elastic;
    }
    if (rightOfElastic && dragged->sizing == StripSizing::Content) {
        if (!dragged->constraints.fixedWidth) {
            int boundary = areaWidget(d->rowAreas[row])->width() - uiPixels(rowMargin);
            for (int after = index; after < d->rows[row].size(); ++after) {
                const auto* strip = d->strips.value(d->rows[row][after]);
                if (strip->visible) { boundary = strip->geometry().left() - uiPixels(stripSpacing); break; }
            }
            dragged->requestedWidth = std::max(dragged->minimumWidthForLayout(), boundary - handle);
        }
    } else {
        for (int previous = index - 1; previous >= 0; --previous) {
            auto* strip = d->strips.value(d->rows[row][previous]);
            if (!strip->visible) continue;
            if (strip->sizing == StripSizing::Content && !strip->constraints.fixedWidth)
                strip->requestedWidth = std::max(strip->minimumWidthForLayout(), handle - strip->geometry().left() - uiPixels(stripSpacing));
            break;
        }
    }
    d->rows[row].insert(index, d->dragging);
    normalizeElastic(row, dragged->sizing == StripSizing::Elastic ? d->dragging : QString{});
    d->reserveDragSlot = true;
}

void CommandStripHost::prepareDragRows()
{
    d->previewRows[0] = d->previewRows[1] = -1;
    if (d->previewArea < 0) return;
    const Area area = Area(d->previewArea);
    int lastOccupied = -1;
    int local = 0;
    for (int row = 0; row < d->rows.size(); ++row) if (d->rowAreas[row] == area) {
        const bool occupied = std::any_of(d->rows[row].cbegin(), d->rows[row].cend(), [this](const QString& id) {
            return id != d->dragging && d->strips.value(id)->visible;
        });
        if (occupied) lastOccupied = local;
        ++local;
    }
    d->previewRows[int(area)] = ensureRow(area, lastOccupied + 1);
}
int CommandStripHost::dragRowAt(QPoint globalPosition)
{
    for (const Area area : {Area::Top, Area::Bottom}) {
        auto* host = areaWidget(area);
        if (!host || host->isHidden()) continue;
        const QPoint position = host->mapFromGlobal(globalPosition);
        if (position.x() < -12 || position.x() > host->width() + 12) continue;
        int lastOccupied = -1;
        int local = 0;
        for (int row = 0; row < d->rows.size(); ++row) {
            if (d->rowAreas[row] != area) continue;
            const bool occupied = std::any_of(d->rows[row].cbegin(), d->rows[row].cend(), [this](const QString& id) {
                return id != d->dragging && d->strips.value(id)->visible;
            });
            if (occupied) lastOccupied = local;
            ++local;
            if (!d->rowWidgets[row] || d->rowWidgets[row]->isHidden()) continue;
            if (d->rowWidgets[row]->geometry().adjusted(-12, 0, 12, 0).contains(position)) return row;
        }
        // This target has no drawing or reserved layout height until the
        // pointer reaches it. It also makes an empty bottom dock discoverable.
        const QRect newRowBand(0, area == Area::Top ? host->height() : -uiPixels(compactRowHeight),
                              host->width(), uiPixels(compactRowHeight));
        if (newRowBand.adjusted(-12, 0, 12, 0).contains(position))
            return ensureRow(area, lastOccupied + 1);
    }
    return -1;
}
void CommandStripHost::updateDrag(QPoint globalPosition)
{
    if (!isDragging()) return;
    d->lastDragPosition = globalPosition;
    if (d->preview) d->preview->move(globalPosition - d->pressOffset);
    // Resolve against the currently drawn row before resetting preview state.
    const int row = dragRowAt(globalPosition);
    const Area targetArea = row >= 0 ? d->rowAreas[row] : Area::Top;
    int localRow = 0;
    int lastOccupied = -1;
    int local = 0;
    if (row >= 0) for (int current = 0; current < d->rows.size(); ++current) {
        if (d->rowAreas[current] != targetArea) continue;
        if (current == row) localRow = local;
        const bool occupied = std::any_of(d->rows[current].cbegin(), d->rows[current].cend(), [this](const QString& id) {
            return id != d->dragging && d->strips.value(id)->visible;
        });
        if (occupied) lastOccupied = local;
        ++local;
    }
    d->previewArea = row >= 0 && localRow >= lastOccupied ? int(targetArea) : -1;
    replaceRows(d->originalRows, d->originalAreas, true);
    for (auto* strip : d->strips) {
        strip->requestedWidth = d->originalWidths.value(strip->id, -1);
        strip->sizing = d->originalSizing.value(strip->id, StripSizing::Content);
        strip->naturalWidthPriority = d->originalWidthPriorities.value(strip->id, false);
    }
    d->reserveDragSlot = false;
    d->dropRow = -1;
    prepareDragRows(); arrange();
    if (row >= 0) {
        const int target = ensureRow(targetArea, localRow);
        const QPoint position = areaWidget(targetArea)->mapFromGlobal(globalPosition);
        for (int affected = 0; affected < d->rows.size(); ++affected)
            if (affected == target || d->originalRows.value(affected).contains(d->dragging)) clearWidthPriority(affected);
        const bool originalArea = target < d->originalAreas.size() && d->originalAreas[target] == targetArea;
        if (!originalArea || !resizeDragBoundary(target, position.x() - d->pressOffset.x()))
            insertDraggedStrip(target, position);
        if (d->rows[target].contains(d->dragging)) d->dropRow = target;
        prepareDragRows(); arrange();
    } else {
        removeFromRows(d->dragging);
        prepareDragRows(); arrange();
    }
    auto* dragged = d->strips.value(d->dragging);
    if (d->preview) {
        d->preview->setPixmap(dragged->grab());
        d->preview->resize(dragged->size());
    }
    if (d->marker) {
        if (d->dropRow < 0 || !d->rowWidgets.value(d->dropRow)) d->marker->hide();
        else {
            auto* host = areaWidget(d->rowAreas[d->dropRow]);
            if (d->marker->parentWidget() != host) d->marker->setParent(host);
            const auto bounds = d->rowWidgets[d->dropRow]->geometry();
            d->marker->setGeometry(qBound(0, dragged->geometry().left(), std::max(0, host->width() - 2)),
                bounds.y(), 2, bounds.height());
            d->marker->show(); d->marker->raise();
        }
    }
}
void CommandStripHost::finishDrag(bool commit)
{
    if (!isDragging()) return;
    if (!commit) {
        replaceRows(d->originalRows, d->originalAreas);
        for (auto* strip : d->strips) {
            strip->requestedWidth = d->originalWidths.value(strip->id, -1);
            strip->sizing = d->originalSizing.value(strip->id, StripSizing::Content);
            strip->naturalWidthPriority = d->originalWidthPriorities.value(strip->id, false);
        }
    }
    d->dragging.clear(); d->pendingStrip.clear(); d->originalWidths.clear();
    d->originalBounds.clear(); d->originalSizing.clear(); d->reserveDragSlot = false;
    d->originalRows.clear(); d->originalAreas.clear();
    d->originalWidthPriorities.clear();
    d->previewRows[0] = d->previewRows[1] = -1;
    d->previewArea = -1;
    d->dropRow = -1;
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
        // Removing an empty source row can move the remaining rows while the
        // pointer is stationary. Commit the last visible insertion preview;
        // a release alone must not select a different newly exposed row.
        if (event->globalPosition().toPoint() != d->lastDragPosition)
            updateDrag(event->globalPosition().toPoint());
        finishDrag(d->dropRow >= 0);
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
