// SPDX-License-Identifier: MIT
#include "command_strip.hpp"

#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QContextMenuEvent>
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
#include <QToolButton>

#include <algorithm>
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

class OverflowButton final : public QAbstractButton {
public:
    explicit OverflowButton(QWidget* parent) : QAbstractButton(parent)
    {
        setObjectName("StripOverflowButton");
        setCursor(Qt::ArrowCursor);
        setToolTip(QStringLiteral("工具条选项"));
        setAccessibleName(toolTip());
        setFocusPolicy(Qt::StrongFocus);
        connect(this, &QAbstractButton::clicked, this, [this] { if (openMenu) openMenu(); });
    }
    bool hasOverflow = false;
    std::function<void()> openMenu;
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (underMouse() || hasFocus()) painter.fillRect(rect(), palette().color(QPalette::Highlight).lighter(170));
        painter.setPen(QPen(palette().color(QPalette::WindowText), 1.0));
        const qreal x = width() / 2.0;
        if (hasOverflow) {
            painter.drawPolyline(QPolygonF{{x - 2, 7}, {x, 9}, {x - 2, 11}});
            painter.drawPolyline(QPolygonF{{x + 1, 7}, {x + 3, 9}, {x + 1, 11}});
        }
        painter.drawPolyline(QPolygonF{{x - 3, height() - 11.0}, {x, height() - 8.0}, {x + 3, height() - 11.0}});
    }
    void enterEvent(QEnterEvent* event) override { update(); QAbstractButton::enterEvent(event); }
    void leaveEvent(QEvent* event) override { update(); QAbstractButton::leaveEvent(event); }
    void focusInEvent(QFocusEvent* event) override { update(); QAbstractButton::focusInEvent(event); }
    void focusOutEvent(QFocusEvent* event) override { update(); QAbstractButton::focusOutEvent(event); }
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return || event->key() == Qt::Key_Down) {
            if (openMenu) openMenu();
            event->accept();
        } else QAbstractButton::keyPressEvent(event);
    }
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
        if (content) width += content->sizeHint().width();
        else for (const auto& item : items)
            if (item.included && item.action && item.action->isVisible() && item.button) width += item.button->sizeHint().width();
        return std::max(minimumWidthForLayout(), width + tailWidth);
    }

    [[nodiscard]] int minimumWidthForLayout() const
    {
        return tailWidth + (handle->isHidden() ? 0 : handleWidth) + 12;
    }

    void layoutItems()
    {
        const int handleSize = handle->isHidden() ? 0 : handleWidth;
        handle->setGeometry(0, 0, handleSize, height());
        tail->setGeometry(std::max(0, width() - tailWidth), 0, tailWidth, height());
        viewport->setGeometry(handleSize, 0, std::max(0, width() - handleSize - tailWidth), height());
        overflow.clear();
        if (content) {
            const int contentWidth = fillWhenFirst ? viewport->width() : std::max(viewport->width(), content->sizeHint().width());
            content->setGeometry(0, 0, contentWidth, height());
            if (auto* menu = qobject_cast<QMenuBar*>(content.data())) {
                for (const auto& item : items) {
                    if (!item.action) continue;
                    item.action->setVisible(item.included);
                    if (item.included && menu->actionGeometry(item.action).right() >= viewport->width()) overflow.append(item.id);
                }
            } else if (content->sizeHint().width() > viewport->width()) overflow.append(QStringLiteral("content"));
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
    bool fillWhenFirst = false;
    int requestedWidth = -1;

protected:
    void resizeEvent(QResizeEvent* event) override { QFrame::resizeEvent(event); layoutItems(); }
};

} // namespace

struct CommandStripHost::Private {
    QHash<QString, StripFrame*> strips;
    QStringList rows[2];
    QWidget* rowWidgets[2] = {};
    bool locked = true;
    std::function<void(QMenu*)> contextMenuCustomizer;
    std::function<void()> layoutChanged;
    QString pendingStrip;
    QPoint pressPosition;
    QPoint pressOffset;
    QString dragging;
    QStringList originalRows[2];
    QHash<QString, int> originalWidths;
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

CommandStripHost::~CommandStripHost() = default;

void CommandStripHost::addStrip(QString id, QWidget* content, int row)
{
    addStrip(id, id, content, row);
}

void CommandStripHost::addStrip(QString id, QString title, QWidget* content, int row, bool fillWhenFirst)
{
    if (!content || id.isEmpty() || d->strips.contains(id)) return;
    auto* strip = new StripFrame(id, std::move(title), d->rowWidgets[qBound(0, row, 1)]);
    strip->content = content;
    strip->fillWhenFirst = fillWhenFirst;
    content->setParent(strip->viewport);
    content->show();
    if (auto* menu = qobject_cast<QMenuBar*>(content)) {
        int index = 0;
        for (auto* action : menu->actions()) {
            auto itemId = action->objectName();
            if (itemId.isEmpty()) itemId = QString::number(index);
            strip->items.append({itemId, action, {}, true});
            ++index;
        }
    }
    strip->handle->installEventFilter(this);
    strip->setLocked(d->locked);
    strip->tail->openMenu = [this, id] {
        auto* strip = d->strips.value(id);
        if (!strip) return;
        QPointer<QMenu> menu = createOverflowMenu(id, this);
        if (menu) { menu->exec(strip->tail->mapToGlobal(QPoint(0, strip->tail->height()))); delete menu; }
    };
    strip->installEventFilter(this);
    d->strips.insert(id, strip);
    d->rows[qBound(0, row, 1)].append(id);
    arrange();
}

void CommandStripHost::addActionStrip(QString id, QString title, const QList<QAction*>& actions, int row)
{
    if (id.isEmpty() || d->strips.contains(id)) return;
    auto* strip = new StripFrame(id, std::move(title), d->rowWidgets[qBound(0, row, 1)]);
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
        ++index;
    }
    strip->handle->installEventFilter(this);
    strip->setLocked(d->locked);
    strip->tail->openMenu = [this, id] {
        auto* strip = d->strips.value(id);
        if (!strip) return;
        QPointer<QMenu> menu = createOverflowMenu(id, this);
        if (menu) { menu->exec(strip->tail->mapToGlobal(QPoint(0, strip->tail->height()))); delete menu; }
    };
    strip->installEventFilter(this);
    d->strips.insert(id, strip);
    d->rows[qBound(0, row, 1)].append(id);
    arrange();
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
        strip->content->setVisible(visible); changed(); return true;
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

bool CommandStripHost::moveStrip(const QString& id, int row, int index)
{
    if (!d->strips.contains(id) || row < 0 || row > 1) return false;
    d->rows[0].removeAll(id); d->rows[1].removeAll(id);
    d->rows[row].insert(qBound(0, index, int(d->rows[row].size())), id);
    arrange(); changed(); return true;
}
bool CommandStripHost::setStripWidth(const QString& id, int width)
{
    auto* strip = d->strips.value(id);
    if (!strip || width < strip->minimumWidthForLayout()) return false;
    strip->requestedWidth = width; arrange(); changed(); return true;
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
void CommandStripHost::changed() { if (d->layoutChanged) d->layoutChanged(); }

QList<QWidget*> CommandStripHost::windowDragAreas() const
{
    QList<QWidget*> areas = {d->rowWidgets[0], d->rowWidgets[1]};
    for (auto* strip : d->strips) {
        areas.append(strip); areas.append(strip->viewport);
        if (strip->fillWhenFirst && strip->content) areas.append(strip->content);
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
    auto* strip = d->strips.value(id);
    if (!strip) return nullptr;
    auto* menu = new QMenu(parent);
    for (const auto& item : strip->items) if (strip->overflow.contains(item.id) && item.action) {
        menu->addAction(item.action);
    }
    if (!menu->actions().isEmpty()) menu->addSeparator();
    auto* customize = menu->addMenu(QStringLiteral("添加或移除按钮"));
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
    return menu;
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
QSize CommandStripHost::minimumSizeHint() const { return {0, sizeHint().height()}; }

void CommandStripHost::arrange()
{
    int y = 0;
    for (int row = 0; row < 2; ++row) {
        QStringList visible;
        for (const auto& id : d->rows[row]) if (d->strips.value(id)->visible && id != d->dragging) visible.append(id);
        auto* rowWidget = d->rowWidgets[row];
        const bool showRow = !visible.isEmpty() || isDragging();
        rowWidget->setVisible(showRow);
        rowWidget->setGeometry(0, y, width(), showRow ? rowHeight : 0);
        if (showRow) y += rowHeight;
        const int available = std::max(0, width() - rowMargin * 2 - std::max(0, int(visible.size()) - 1) * stripSpacing);
        QVector<int> sizes;
        int total = 0;
        for (const auto& id : visible) {
            const auto* strip = d->strips.value(id);
            const int value = std::max(strip->minimumWidthForLayout(), strip->requestedWidth < 0 ? strip->naturalWidth() : strip->requestedWidth);
            sizes.append(value); total += value;
        }
        if (!visible.isEmpty() && d->strips.value(visible.first())->fillWhenFirst && total < available) {
            sizes[0] += available - total; total = available;
        }
        // Reserve enough space for every visible handle and tail. The buttons
        // shrink into each strip's overflow menu rather than widening the window.
        for (int index = 0; index < sizes.size() && total > available; ++index) {
            const int minimum = d->strips.value(visible[index])->minimumWidthForLayout();
            const int reduction = std::min(total - available, std::max(0, sizes[index] - minimum));
            sizes[index] -= reduction; total -= reduction;
        }
        int x = rowMargin;
        int index = 0;
        for (const auto& id : d->rows[row]) {
            auto* strip = d->strips.value(id);
            if (strip->parentWidget() != rowWidget) strip->setParent(rowWidget);
            const bool show = strip->visible && id != d->dragging;
            strip->setVisible(show);
            if (!show) continue;
            strip->setGeometry(x, 0, std::max(0, std::min(sizes[index], width() - x)), rowHeight);
            strip->layoutItems(); x += sizes[index++] + stripSpacing;
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
            strips.insert(id, QJsonObject{{"visible", strip->visible}, {"width", savedWidth},
                {"excluded", excluded}, {"contentVisible", !strip->content || !strip->content->isHidden()}});
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
        const auto id = entry.toString();
        if (!d->strips.contains(id) || seen.contains(id)) return;
        proposed[row].append(id); seen.insert(id);
    }
    // New strips may be introduced by an application upgrade. Keep their
    // defaults while preserving the saved position of the known strips.
    for (int row = 0; row < 2; ++row) for (const auto& id : d->rows[row]) if (!seen.contains(id)) proposed[row].append(id);
    const auto settings = json.value("strips").toObject();
    const bool savedLocked = json.value("locked").toBool(true);
    const int savedMinimumWidth = tailWidth + (savedLocked ? 0 : handleWidth) + 12;
    for (const auto& id : stripIds()) {
        if (!settings.contains(id)) continue;
        const auto value = settings.value(id).toObject();
        const int requested = value.value("width").toInt(-1);
        if (requested != -1 && requested < savedMinimumWidth) return;
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
        const auto excluded = value.value("excluded").toArray();
        for (auto& item : strip->items) item.included = !excluded.contains(item.id);
        if (strip->content && strip->items.isEmpty()) strip->content->setVisible(value.value("contentVisible").toBool(true));
    }
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
    for (auto* entry : d->strips) d->originalWidths.insert(entry->id, entry->requestedWidth);
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

void CommandStripHost::updateDrag(QPoint globalPosition)
{
    if (!isDragging()) return;
    if (d->preview) d->preview->move(globalPosition - d->pressOffset);
    for (int row = 0; row < 2; ++row) { d->rows[row] = d->originalRows[row]; d->rows[row].removeAll(d->dragging); }
    for (auto* strip : d->strips) strip->requestedWidth = d->originalWidths.value(strip->id, -1);
    arrange();
    const QPoint position = mapFromGlobal(globalPosition);
    const int row = qBound(0, position.y() / rowHeight, 1);
    int index = 0;
    int markerX = rowMargin;
    for (int itemIndex = 0; itemIndex < d->rows[row].size(); ++itemIndex) {
        auto* strip = d->strips.value(d->rows[row][itemIndex]);
        if (!strip->visible) continue;
        const auto bounds = strip->geometry();
        if (position.x() < bounds.left() + strip->minimumWidthForLayout() / 2) break;
        index = itemIndex + 1;
        markerX = bounds.right() + stripSpacing;
        if (position.x() < bounds.right()) {
            strip->requestedWidth = std::max(strip->minimumWidthForLayout(), position.x() - bounds.left() - stripSpacing);
            markerX = bounds.left() + strip->requestedWidth + stripSpacing;
            break;
        }
    }
    d->rows[row].insert(index, d->dragging);
    arrange();
    if (d->marker) {
        d->marker->setGeometry(qBound(0, markerX, std::max(0, width() - 2)), row * rowHeight, 2, rowHeight);
        d->marker->show(); d->marker->raise();
    }
}

void CommandStripHost::finishDrag(bool commit)
{
    if (!isDragging()) return;
    if (!commit) {
        d->rows[0] = d->originalRows[0]; d->rows[1] = d->originalRows[1];
        for (auto* strip : d->strips) strip->requestedWidth = d->originalWidths.value(strip->id, -1);
    }
    d->dragging.clear(); d->pendingStrip.clear(); d->originalWidths.clear();
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
