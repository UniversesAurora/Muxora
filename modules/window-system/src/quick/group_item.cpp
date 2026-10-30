// SPDX-License-Identifier: MIT
#include "group_item.hpp"
#include "button_item.hpp"
#include "group_surface_item.hpp"
#include "path_item.hpp"
#include "tab_bar_item.hpp"
#include "tab_label_text_item.hpp"
#include "style/frame_motion.hpp"
#include "style/tab_label_geometry.hpp"
#include <window_system/ui_scale.hpp>

#include <QCoreApplication>
#include <QCursor>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainterPath>
#include <QQuickWindow>
#include <QTimer>
#include <algorithm>

namespace window_system::quick {
namespace {

// The vertical column's inner edge: drag to resize, double click to reset.
class ColumnResizeHandle final : public QQuickItem {
public:
    explicit ColumnResizeHandle(QQuickItem* parent) : QQuickItem(parent)
    {
        setAcceptedMouseButtons(Qt::LeftButton);
        setCursor(Qt::SizeHorCursor);
    }
    std::function<void(int)> pressed, moved;
    std::function<void()> released, reset;

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        event->accept();
        if (pressed) pressed(event->globalPosition().toPoint().x());
    }
    void mouseMoveEvent(QMouseEvent* event) override { if (moved) moved(event->globalPosition().toPoint().x()); }
    void mouseReleaseEvent(QMouseEvent*) override { if (released) released(); }
    void mouseDoubleClickEvent(QMouseEvent* event) override { event->accept(); if (reset) reset(); }
};

QFont nameFont()
{
    // As the Widgets label: one point smaller at the interface zoom, demibold.
    auto font = QGuiApplication::font();
    font.setPointSizeF(font.pointSizeF() - interfaceScale());
    font.setWeight(QFont::DemiBold);
    return font;
}

void setText(TabLabelTextItem* item, const QRect& area, const QString& text, const QFont& font,
             const QColor& color, Qt::LayoutDirection direction = Qt::LeftToRight)
{
    if (area.isEmpty() || text.isEmpty()) { item->clearLabel(); item->setVisible(false); return; }
    TabLabelPresentation label;
    label.parts.tab = area; label.parts.title = area;
    label.state.shape = TabShape::Row; label.state.selected = true; label.state.title = text;
    label.font = font; label.direction = direction;
    label.naturalWidth = QFontMetrics(font).horizontalAdvance(text);
    TabStripLabelStyle style;
    style.palette.text = color;
    item->setLabel(label, style, TabTextRole::Literal);
    item->setPosition(area.topLeft());
    item->setVisible(true);
}

} // namespace

GroupItem::GroupItem(const Theme& theme, QQuickItem* parent)
    : QQuickItem(parent), theme_(theme), bar_(new TabBarItem(theme, this)),
      surface_(new GroupSurfaceItem(this)), card_(new PathItem(this)), corners_(new PathItem(this)),
      content_(new QQuickItem(this)),
      create_(new ButtonItem(theme, this)), types_(new ButtonItem(theme, this)),
      list_(new ButtonItem(theme, this)), count_(new ButtonItem(theme, this)),
      iconMode_(new ButtonItem(theme, this)), name_(new TabLabelTextItem(this)),
      title_(new TabLabelTextItem(this)), resizeHandle_(new ColumnResizeHandle(this))
{
    setAcceptHoverEvents(true);
    group_ = std::make_unique<TabGroupController>(theme, bar_->controller());
    clock_.start();
    surface_->setZ(0); card_->setZ(1); content_->setZ(2); content_->setClip(true); corners_->setZ(2.5);
    bar_->setZ(3);
    for (auto* control : {create_, types_, list_, count_, iconMode_}) control->setZ(4);
    name_->setZ(4); title_->setZ(4); resizeHandle_->setZ(5);
    create_->setShape(ButtonItem::Shape::SplitStart); types_->setShape(ButtonItem::Shape::SplitEnd);
    create_->setIcon(iconFor(QStringLiteral("add"), theme));
    create_->setToolTip(QCoreApplication::translate("WindowSystem", "New tab"));
    types_->setIcon(iconFor(QStringLiteral("chevron-down"), theme));
    types_->setToolTip(QCoreApplication::translate("WindowSystem", "Choose new tab type"));
    list_->setIcon(iconFor(QStringLiteral("tab-list"), theme));
    list_->setToolTip(QCoreApplication::translate("WindowSystem", "All tabs"));
    // The two halves of the new-tab button light up together.
    for (auto* part : {create_, types_})
        connect(part, &ButtonItem::hoveredChanged, this, [this] {
            const bool hovered = create_->hovered() || types_->hovered();
            create_->setEmphasized(hovered); types_->setEmphasized(hovered);
        });
    connect(create_, &ButtonItem::clicked, this, [this] { if (const auto callback = newTabRequested) callback(); });
    const auto typeMenu = [this] { if (const auto callback = newTabMenuRequested) callback(globalRect(types_)); };
    connect(types_, &ButtonItem::clicked, this, typeMenu);
    connect(create_, &ButtonItem::contextMenuRequested, this, typeMenu);
    connect(types_, &ButtonItem::contextMenuRequested, this, typeMenu);
    connect(list_, &ButtonItem::clicked, this, [this] { if (const auto callback = tabListRequested) callback(globalRect(list_)); });
    connect(count_, &ButtonItem::clicked, this, [this] {
        bar_->edit([&] { bar_->controller().setExpanded(!bar_->controller().expanded()); });
    });
    connect(iconMode_, &ButtonItem::clicked, this, [this] { edit([&] { group_->toggleIconMode(); }); });
    auto* handle = static_cast<ColumnResizeHandle*>(resizeHandle_);
    handle->pressed = [this](int x) { edit([&] { group_->beginColumnResize(x); }); };
    handle->moved = [this](int x) { if (group_->columnResizing()) edit([&] { group_->moveColumnResize(x); }); };
    handle->released = [this] { if (group_->columnResizing()) edit([&] { group_->endColumnResize(); }); };
    handle->reset = [this] { edit([&] { group_->resetColumnWidth(); }); };

    connect(bar_, &TabBarItem::currentChanged, this, [this](int index) {
        const QPointer<GroupItem> alive(this);
        if (index >= 0) resolvePage(bar_->controller().tabData(index).toUuid());
        if (!alive) return;
        showCurrentPage(); layoutChildren();
        if (const auto callback = currentChanged) callback(index);
    });
    connect(bar_, &TabBarItem::viewChanged, this, [this] { edit([&] { group_->viewChanged(); }); });
    connect(bar_, &TabBarItem::surfaceChanged, this, &GroupItem::syncSurface);
    connect(bar_, &TabBarItem::reservedSizeChanged, this, &GroupItem::layoutChildren);
    connect(bar_, &TabBarItem::controlsChanged, this, &GroupItem::applyStripControls);
    connect(bar_, &TabBarItem::scrollInputStarted, this, [this] { edit([&] { group_->cancelViewportReflow(); }); });
    connect(bar_, &TabBarItem::contextMenuRequested, this, [this](int tab, int folder, QPoint global) {
        if (const auto callback = stripMenuRequested) callback(tab, folder, global);
    });
    connect(bar_, &TabBarItem::actionRequested, this, [this](TabPointerDecision decision) {
        if (const auto callback = stripAction) callback(decision);
    });
    attachWindow(window());
    layoutChildren();
}

GroupItem::~GroupItem()
{
    for (const auto& connection : std::as_const(windowConnections_)) disconnect(connection);
    disconnect(frameConnection_);
    // Pages belong to their host; never destroy them with the group.
    for (const auto& page : std::as_const(pages_)) if (page) page->setParentItem(nullptr);
}

template <class Apply> void GroupItem::edit(Apply&& apply)
{
    const QPointer<GroupItem> alive(this);
    bar_->edit([&] { prepareGroup(); apply(); });
    if (alive) applyGroupChanges();
}

void GroupItem::prepareGroup()
{
    TabGroupEnvironment environment;
    environment.size = size().toSize();
    if (window_) {
        environment.originInWindow = mapToItem(window_->contentItem(), QPointF()).toPoint();
        environment.windowClientBounds = QRect(window_->position(), window_->size());
        environment.windowActive = window_->isActive();
    }
    environment.contentMinimumWidth = contentMinimum_.width();
    environment.visible = isVisible() && window_ && window_->isVisible();
    environment.animations = motionAnimationsEnabled();
    group_->setEnvironment(environment);
    group_->setTimestampNs(clock_.nsecsElapsed());
}

void GroupItem::applyGroupChanges()
{
    const QPointer<GroupItem> alive(this);
    const auto changes = group_->takeChanges();
    if (changes.layout) layoutChildren();
    if (!alive) return;
    if (changes.surface) syncSurface();
    scheduleMotion();
    if (changes.reflow && !reflowQueued_) {
        reflowQueued_ = true;
        QTimer::singleShot(0, this, [this] { reflowQueued_ = false; edit([&] { group_->applyViewportReflow(); }); });
    }
    if (changes.scrollRestore) QTimer::singleShot(0, this, [this] { edit([&] { group_->finishScrollOffsetRestore(); }); });
    if (changes.columnState) if (const auto callback = verticalStateChanged) callback(changes.columnState->first, changes.columnState->second);
}

void GroupItem::attachWindow(QQuickWindow* window)
{
    if (window_ == window) return;
    for (const auto& connection : std::as_const(windowConnections_)) disconnect(connection);
    windowConnections_.clear();
    disconnect(frameConnection_); frameConnection_ = {};
    window_ = window;
    if (!window) return;
    const auto refocus = [this] {
        const auto callback = isActiveGroup;
        setGroupFocused(window_ && window_->isActive() && callback && callback());
    };
    windowConnections_.append(connect(window, &QWindow::activeChanged, this, refocus));
    const auto moved = [this] { edit([&] { group_->queueViewportReflow(); }); };
    windowConnections_.append(connect(window, &QWindow::xChanged, this, moved));
    windowConnections_.append(connect(window, &QWindow::widthChanged, this, moved));
    windowConnections_.append(connect(window, &QWindow::visibleChanged, this, moved));
}

void GroupItem::scheduleMotion()
{
    const bool running = group_->needsFrame() && window_;
    if (running && !frameConnection_) {
        frameConnection_ = connect(window_, &QQuickWindow::afterAnimating, this, &GroupItem::advanceMotion);
        window_->update();
    } else if (!running && frameConnection_) {
        disconnect(frameConnection_); frameConnection_ = {};
    }
}

void GroupItem::advanceMotion()
{
    edit([&] { group_->advance(clock_.nsecsElapsed()); });
    if (group_->needsFrame() && window_) window_->update();
}

bool GroupItem::vertical() const { return group_->vertical(); }

QRect GroupItem::globalRect(const QQuickItem* item) const
{
    return QRect(item->mapToGlobal(QPointF()).toPoint(), item->size().toSize());
}

void GroupItem::setForegroundLayer(QQuickItem* layer) { bar_->setForegroundLayer(layer); }

void GroupItem::addTab(QQuickItem* page, const QIcon& icon, const QString& title, TabId id)
{
    pages_.insert(id, page);
    if (page) { page->setParentItem(content_); page->setVisible(false); }
    bar_->edit([&] {
        auto& strip = bar_->controller();
        const int index = strip.addTab(icon, title);
        strip.setTabData(index, id); strip.setTabToolTip(index, title);
    });
    showCurrentPage();
    layoutChildren();
}

QQuickItem* GroupItem::takePage(TabId id)
{
    QPointer<QQuickItem> page = pages_.take(id);
    if (page) { page->setVisible(false); page->setParentItem(nullptr); }
    if (shownPage_ == page) shownPage_ = nullptr;
    bar_->edit([&] {
        auto& strip = bar_->controller();
        if (const int index = strip.indexOf(id); index >= 0) strip.removeTab(index);
    });
    showCurrentPage();
    layoutChildren();
    return page;
}

void GroupItem::setCurrentIndex(int index, bool reveal)
{
    edit([&] { group_->setCurrentIndex(index, reveal); });
    showCurrentPage();
}

int GroupItem::currentIndex() const { return bar_->controller().currentIndex(); }
int GroupItem::count() const { return bar_->controller().count(); }

void GroupItem::resolvePage(TabId tab)
{
    if (pages_.value(tab) || !pageRequested || !pages_.contains(tab)) return;
    const QPointer<GroupItem> alive(this);
    QPointer<QQuickItem> page = pageRequested(tab);
    if (!alive || !page || !pages_.contains(tab)) return;
    pages_.insert(tab, page);
    page->setParentItem(content_);
}

void GroupItem::showCurrentPage()
{
    const int index = currentIndex();
    const TabId tab = index >= 0 ? bar_->controller().tabData(index).toUuid() : TabId{};
    if (!tab.isNull()) resolvePage(tab);
    QQuickItem* page = pages_.value(tab).data();
    if (page == shownPage_) return;
    if (shownPage_) shownPage_->setVisible(false);
    shownPage_ = page;
    if (page) {
        page->setParentItem(content_);
        page->setPosition({}); page->setSize(content_->size());
        page->setVisible(true);
    }
}

void GroupItem::setTheme(const Theme& theme)
{
    theme_ = theme;
    bar_->edit([&] { bar_->controller().setTheme(theme); });
    for (auto* button : {create_, types_, list_, count_, iconMode_}) button->setTheme(theme);
    for (auto* button : std::as_const(sidebar_)) button->setTheme(theme);
    create_->setIcon(iconFor(QStringLiteral("add"), theme));
    types_->setIcon(iconFor(QStringLiteral("chevron-down"), theme));
    list_->setIcon(iconFor(QStringLiteral("tab-list"), theme));
    edit([&] { group_->setTheme(theme); });
}

void GroupItem::setAppearance(const TabAppearance& appearance, bool pinned)
{
    edit([&] { group_->setAppearance(appearance, pinned); });
}

void GroupItem::setStickyCount(int count)
{
    if (bar_->controller().stickyCount() == count) return;
    bar_->edit([&] { bar_->controller().setStickyCount(count); });
    layoutChildren();
}

void GroupItem::setTabMetadata(int index, const TabRecord& tab)
{
    bar_->edit([&] { bar_->controller().setTabMetadata(index, tab); });
}

void GroupItem::setGroupName(const QString& name)
{
    edit([&] { group_->setGroupName(name); });
    applyStripControls();
}

void GroupItem::setVerticalState(int width, bool iconMode) { edit([&] { group_->setVerticalState(width, iconMode); }); }
void GroupItem::setTabBarVisible(bool visible) { edit([&] { group_->setTabBarVisible(visible); }); }
void GroupItem::setContentEdgeToEdge(bool enabled) { edit([&] { group_->setContentEdgeToEdge(enabled); }); }

void GroupItem::setGroupFocused(bool focused) { edit([&] { group_->setGroupFocused(focused); }); }

void GroupItem::setPinnedActions(std::function<void()> current, std::function<void()> all)
{
    for (auto item : {std::tuple{std::move(current), QCoreApplication::translate("WindowSystem", "Unpin current tab"), QStringLiteral("unpin")},
                      std::tuple{std::move(all), QCoreApplication::translate("WindowSystem", "Unpin all sidebar tabs"), QStringLiteral("unpin-all")}}) {
        auto* button = new ButtonItem(theme_, this);
        button->setZ(4);
        button->setIcon(iconFor(std::get<2>(item), theme_));
        button->setToolTip(std::get<1>(item));
        connect(button, &ButtonItem::clicked, this, [callback = std::move(std::get<0>(item))] { if (callback) callback(); });
        sidebar_.append(button);
    }
    applyStripControls();
}

void GroupItem::setContentMinimum(QSize minimum)
{
    if (contentMinimum_ == minimum) return;
    contentMinimum_ = minimum;
    layoutChildren();
}

QSize GroupItem::minimumSize() const { return group_->minimumSize(contentMinimum_); }
QSize GroupItem::preferredSize() const { return group_->preferredSize(contentMinimum_); }

TabViewportState GroupItem::captureViewportState() const { return group_->captureViewportState(); }
void GroupItem::restoreViewportState(TabViewportState state) { edit([&] { group_->restoreViewportState(std::move(state)); }); }
void GroupItem::restoreScrollOffset(int offset) { edit([&] { group_->restoreScrollOffset(offset); }); }

QRect GroupItem::contentRect() const { return group_->placement().geometry.content; }

QRect GroupItem::stripRect() const
{
    if (!group_->tabBarVisible() || !bar_->isVisible()) return {};
    return QRect(bar_->position().toPoint(), bar_->size().toSize());
}

void GroupItem::geometryChange(const QRectF& now, const QRectF& previous)
{
    QQuickItem::geometryChange(now, previous);
    if (now.size() != previous.size()) layoutChildren();
    else if (now.topLeft() != previous.topLeft()) edit([] {});
}

void GroupItem::itemChange(ItemChange change, const ItemChangeData& data)
{
    QQuickItem::itemChange(change, data);
    if (change == ItemSceneChange) attachWindow(data.window);
    if (change == ItemSceneChange || change == ItemVisibleHasChanged) edit([] {});
}

void GroupItem::hoverEnterEvent(QHoverEvent*)
{
    if (vertical()) bar_->edit([&] { bar_->controller().setColumnHovered(true); });
}

void GroupItem::hoverLeaveEvent(QHoverEvent*)
{
    if (vertical()) bar_->edit([&] { bar_->controller().setColumnHovered(false); });
}

void GroupItem::applyStripControls()
{
    // Horizontal: the new-tab pair right after the last tab; the list and
    // sidebar actions at the first row's end, the group name before them.
    const auto& metrics = bar_->controller().metrics();
    const bool compact = group_->compactPresentation();
    const int row = metrics.rowHeight;
    TabStripControls controls;
    if (!vertical()) {
        const bool creating = !compact && group_->appearance().showNewTabButton;
        controls.trailingPresent = creating;
        controls.trailingWidth = creating ? uiPixels(4) + metrics.newTabWidth + metrics.controlGap + metrics.typeWidth : 0;
        int end = 0;
        int parts = 0;
        if (!compact) {
            end += int(sidebar_.size()) * uiPixels(20); parts += int(sidebar_.size());
            end += metrics.listWidth; ++parts;
        }
        if (group_->placement().nameShown && !compact) {
            end += QFontMetrics(nameFont()).horizontalAdvance(group_->groupName()) + 12; ++parts;
        }
        controls.endWidth = end + std::max(0, parts - 1) * metrics.controlGap;
        controls.endPresent = parts > 0;
    }
    const auto& state = bar_->controller().controlState();
    if (state.countMeasured && countSize_ != state.countSize) countSize_ = state.countSize;
    controls.countSize = countSize_.isValid() ? countSize_ : QSize(metrics.countMinimumWidth, row);
    controls.countMinimumWidth = metrics.countMinimumWidth;
    controls.countPadding = uiPixels(14);
    controls.countPresent = true;
    controls.countShown = state.countShown;
    controls.ownerVisible = isVisible();
    bar_->setControls(controls);
    placeControls(group_->placement());
}

void GroupItem::layoutChildren()
{
    if (layingOut_) return;
    layingOut_ = true;
    TabGroupPlacement placement;
    bar_->edit([&] { prepareGroup(); placement = group_->place(); });
    const auto& geometry = placement.geometry;
    content_->setPosition(geometry.content.topLeft()); content_->setSize(geometry.content.size());
    syncCard(geometry);
    if (shownPage_) shownPage_->setSize(geometry.content.size());
    const bool barShown = group_->tabBarVisible();
    bar_->setVisible(barShown);
    if (barShown) {
        bar_->setPosition(placement.barOrigin);
        bar_->setSize(geometry.bar.size());
    }
    bar_->edit([&] { prepareGroup(); group_->placed(); });
    layingOut_ = false;
    applyStripControls();
    syncSurface();
    applyGroupChanges();
    if (const auto callback = minimumSizeChanged) callback();
}

void GroupItem::placeControls(const TabGroupPlacement& placement)
{
    const auto& metrics = bar_->controller().metrics();
    const auto& state = bar_->controller().controlState();
    const bool compact = group_->compactPresentation();
    const bool shown = group_->tabBarVisible();
    const int row = metrics.rowHeight;
    const QPoint bar = placement.barOrigin;
    const auto place = [](QQuickItem* item, QRect area) {
        item->setPosition(area.topLeft()); item->setSize(area.size()); item->setVisible(!area.isEmpty());
    };
    count_->setVisible(shown && state.countShown);
    if (count_->isVisible()) {
        place(count_, state.rects.count.isNull() ? QRect{} : QRect(bar + state.rects.count.topLeft(), countSize_));
        count_->setText(state.countText, QGuiApplication::font());
        count_->setToolTip(!bar_->controller().expanded()
            ? QCoreApplication::translate("WindowSystem", "%n more tab(s) in other rows; click to expand", nullptr, bar_->controller().hiddenTabs())
            : QCoreApplication::translate("WindowSystem", "Collapse other rows"));
    }
    const bool creating = shown && !compact && group_->appearance().showNewTabButton;
    if (!vertical()) {
        iconMode_->setVisible(false); title_->setVisible(false); resizeHandle_->setVisible(false);
        create_->setIconRail({}); create_->setText({}, {});
        const QRect trailing = state.rects.trailing.isNull() ? QRect{} : state.rects.trailing.translated(bar);
        const QSize split(metrics.newTabWidth, std::max(1, row - uiPixels(4)));
        const int top = trailing.top() + (row - split.height()) / 2;
        create_->setVisible(creating && !trailing.isEmpty()); types_->setVisible(create_->isVisible());
        if (create_->isVisible()) {
            place(create_, QRect(QPoint(trailing.left() + uiPixels(4), top), split));
            place(types_, QRect(QPoint(create_->x() + split.width() + metrics.controlGap, top), QSize(metrics.typeWidth, split.height())));
        }
        // The end area, from its right edge: list, sidebar actions, name.
        const QRect end = state.rects.end.isNull() ? QRect{} : state.rects.end.translated(bar);
        int x = end.right() + 1;
        const bool ending = shown && !compact && !end.isEmpty();
        list_->setVisible(ending);
        if (ending) { x -= metrics.listWidth; place(list_, QRect(x, end.top(), metrics.listWidth, row)); x -= metrics.controlGap; }
        for (auto it = sidebar_.rbegin(); it != sidebar_.rend(); ++it) {
            (*it)->setVisible(ending);
            if (ending) { x -= uiPixels(20); place(*it, QRect(x, end.top(), uiPixels(20), row)); x -= metrics.controlGap; }
        }
        if (ending && placement.nameShown) {
            const auto font = nameFont();
            const int width = QFontMetrics(font).horizontalAdvance(group_->groupName()) + 12;
            setText(name_, QRect(x - width + 6, end.top(), width - 12, row), group_->groupName(), font, theme_.mutedText);
        } else { name_->clearLabel(); name_->setVisible(false); }
        return;
    }
    // Vertical: a footer with new tab, the sidebar actions and icon mode.
    name_->clearLabel(); name_->setVisible(false); list_->setVisible(false);
    const auto& geometry = placement.geometry;
    const QRect footer = geometry.footer.translated(geometry.panel.topLeft());
    const bool left = bar_->controller().placement() == TabBarPlacement::Left;
    const bool icons = group_->iconMode() && !bar_->controller().peeking();
    const auto newTab = QCoreApplication::translate("WindowSystem", "New tab");
    const auto controls = layoutTabColumnControls({group_->shownColumnWidth(), metrics.verticalInset, row, metrics.newTabWidth,
        uiPixels(26), metrics.typeWidth, metrics.controlGap, uiPixels(tabActionSize), int(sidebar_.size()),
        QFontMetrics(QGuiApplication::font()).horizontalAdvance(newTab), icons, compact});
    const bool footerShown = shown && geometry.footerVisible;
    const int inset = metrics.verticalInset;
    const int top = footer.top() + (row - controls.combined.height()) / 2;
    // From the outer edge: new tab; from the inner edge: icon mode, actions.
    int start = left ? footer.left() + inset : footer.right() + 1 - inset;
    const auto fromStart = [&](QQuickItem* item, QSize size) {
        const QRect area = left ? QRect(QPoint(start, top), size) : QRect(QPoint(start - size.width(), top), size);
        place(item, area); start += left ? size.width() + 2 : -(size.width() + 2);
    };
    int end = left ? footer.right() + 1 - inset : footer.left() + inset;
    const auto fromEnd = [&](QQuickItem* item, QSize size) {
        const QRect area = left ? QRect(QPoint(end - size.width(), top), size) : QRect(QPoint(end, top), size);
        place(item, area); end += left ? -(size.width() + 2) : size.width() + 2;
    };
    create_->setVisible(footerShown && creating);
    types_->setVisible(footerShown && creating && controls.types);
    create_->setShape(left ? ButtonItem::Shape::SplitStart : ButtonItem::Shape::SplitEnd);
    types_->setShape(left ? ButtonItem::Shape::SplitEnd : ButtonItem::Shape::SplitStart);
    create_->setText(controls.text ? newTab : QString(), QGuiApplication::font());
    create_->setIconRail(metrics.verticalIconWidth / 2 - inset, left ? Qt::LeftEdge : Qt::RightEdge);
    if (create_->isVisible()) {
        fromStart(create_, controls.primary);
        if (types_->isVisible()) {
            start += left ? metrics.controlGap - 2 : -(metrics.controlGap - 2);
            fromStart(types_, QSize(metrics.typeWidth, controls.primary.height()));
        }
    } else types_->setVisible(false);
    iconMode_->setVisible(footerShown && controls.toggle);
    iconMode_->setIcon(iconFor(group_->iconMode() == left ? QStringLiteral("chevron-right") : QStringLiteral("chevron-left"), theme_));
    iconMode_->setToolTip(group_->iconMode() ? QCoreApplication::translate("WindowSystem", "Show tab titles")
                                              : QCoreApplication::translate("WindowSystem", "Show only tab icons"));
    if (iconMode_->isVisible()) fromEnd(iconMode_, QSize(uiPixels(26), row));
    for (auto it = sidebar_.rbegin(); it != sidebar_.rend(); ++it) {
        (*it)->setVisible(footerShown && controls.actions);
        if ((*it)->isVisible()) fromEnd(*it, QSize(uiPixels(20), row));
    }
    if (geometry.titleVisible)
        setText(title_, geometry.title.translated(geometry.panel.topLeft()), group_->groupName(), nameFont(), theme_.mutedText);
    else { title_->clearLabel(); title_->setVisible(false); }
    place(resizeHandle_, shown && geometry.resizeVisible ? geometry.resizeHandle : QRect{});
}

void GroupItem::syncCard(const TabGroupGeometry& geometry)
{
    const bool shown = geometry.cardVisible && !group_->contentEdgeToEdge() && !geometry.content.isEmpty();
    card_->setVisible(shown); corners_->setVisible(shown);
    if (!shown) return;
    const QRectF body(geometry.content);
    const qreal radius = std::min({qreal(bar_->controller().metrics().cardRadius), body.width() / 2, body.height() / 2});
    QPainterPath card;
    card.addRoundedRect(body, radius, radius);
    card_->setSize(size()); card_->setFill(theme_.surface); card_->setPath(card);
    // Pages are clipped to the card's rectangle; the dock's color covers
    // what lies outside its rounded corners.
    QPainterPath corners;
    const qreal diameter = 2 * radius;
    const auto corner = [&](QPointF square, qreal start) {
        QPainterPath piece;
        piece.moveTo(square);
        piece.arcTo(QRectF(start == 90 || start == 180 ? body.left() : body.right() - diameter,
                           start == 90 || start == 0 ? body.top() : body.bottom() - diameter, diameter, diameter), start, 90);
        piece.closeSubpath();
        corners.addPath(piece);
    };
    corner(body.topLeft(), 90); corner(body.topRight(), 0);
    corner(body.bottomLeft(), 180); corner(body.bottomRight(), 270);
    corners_->setSize(size()); corners_->setFill(theme_.headerBackground); corners_->setPath(corners);
}

void GroupItem::syncSurface()
{
    surface_->setPosition({});
    surface_->setSize(size());
    surface_->setFrame(group_->surfaceFrame(true));
}

} // namespace window_system::quick
