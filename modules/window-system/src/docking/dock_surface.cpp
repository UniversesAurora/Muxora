#include "dock_surface.hpp"
#include "edge_strip.hpp"
#include "edge_tab_button.hpp"
#include "peek_metrics.hpp"
#include "tab_strip.hpp"
#include "window_manager_p.hpp"
#include "panes/tab_content.hpp"
#include "style/compact_tool_button.hpp"
#include <QAbstractButton>
#include <QApplication>
#include <QBoxLayout>
#include <QContextMenuEvent>
#include <QEvent>
#include <QGridLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QTimer>
#include <algorithm>

namespace window_system {
namespace {
class PeekHeader final : public QWidget {
public:
    explicit PeekHeader(QWidget* parent) : QWidget(parent) { updatePeekHeaderHeight(this); }
    std::function<void(QPoint)> drag;
protected:
    bool event(QEvent* event) override {
        const bool handled = QWidget::event(event);
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
            updatePeekHeaderHeight(this);
        return handled;
    }
    void mousePressEvent(QMouseEvent* event) override { start_ = event->position().toPoint(); }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (event->buttons().testFlag(Qt::LeftButton) &&
            (event->position().toPoint() - start_).manhattanLength() >= QApplication::startDragDistance()) {
            const auto callback = drag; if (callback) callback(event->globalPosition().toPoint());
        }
    }
private:
    QPoint start_;
};
void configureSplitter(QSplitter* splitter) {
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(4);
}
} // namespace

DockSurface::DockSurface(WindowManager* manager, WindowId window, QWidget* parent)
    : QFrame(parent), manager_(manager), window_(window)
{
    setObjectName("DockSurface");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    qApp->installEventFilter(this);
}
void DockSurface::rebuild(const QHash<TabId, QPointer<TabContent>>& contents)
{
    restoreFocusScope();
    closePeek(); groups_.clear(); edgeBars_.clear(); pinnedRegions_.clear(); contents_ = contents;
    if (root_) {
        layout()->removeWidget(root_);
        root_->hide();
        root_->deleteLater();
    }
    root_ = new QWidget(this);
    auto* outer = new QGridLayout(root_);
    outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(0);
    outer->setRowStretch(1, 1); outer->setColumnStretch(1, 1);
    auto* topBar = buildEdge(Edge::Top, contents);
    auto* leftBar = buildEdge(Edge::Left, contents);
    auto* rightBar = buildEdge(Edge::Right, contents);
    auto* bottomBar = buildEdge(Edge::Bottom, contents);
    // Bottom owns both lower corners; the vertical bars own the upper
    // corners. Missing neighbours leave their corner to the remaining bar.
    if (leftBar) outer->addWidget(leftBar, 0, 0, bottomBar ? 2 : 3, 1);
    if (rightBar) outer->addWidget(rightBar, 0, 2, bottomBar ? 2 : 3, 1);
    if (topBar) {
        const int firstColumn = leftBar ? 1 : 0;
        const int lastColumn = rightBar ? 1 : 2;
        outer->addWidget(topBar, 0, firstColumn, 1, lastColumn - firstColumn + 1);
    }
    if (bottomBar) outer->addWidget(bottomBar, 2, 0, 1, 3);
    auto* vertical = new QSplitter(Qt::Vertical, root_);
    vertical_ = vertical;
    configureSplitter(vertical);
    if (auto* top = buildPinned(Edge::Top, contents)) vertical->addWidget(top);
    auto* horizontal = new QSplitter(Qt::Horizontal, vertical);
    horizontal_ = horizontal;
    configureSplitter(horizontal);
    if (auto* left = buildPinned(Edge::Left, contents)) horizontal->addWidget(left);
    const auto* window = manager_->model().window(window_);
    center_ = buildTree(window ? window->central : LayoutTree{}, contents);
    if (!center_) {
        auto* empty = new QLabel(QStringLiteral("这个工作区还没有标签\n\nCtrl+Shift+P 打开命令面板"), horizontal);
        empty->setAlignment(Qt::AlignCenter); empty->setObjectName("EmptyWorkspace");
        empty->setFocusPolicy(Qt::StrongFocus); center_ = empty;
    }
    horizontal->addWidget(center_);
    horizontal->setStretchFactor(horizontal->indexOf(center_), 1);
    if (auto* right = buildPinned(Edge::Right, contents)) horizontal->addWidget(right);
    QList<int> sizes;
    for (int i = 0; i < horizontal->count(); ++i) sizes << (horizontal->widget(i) == center_ ? 900
        : horizontal->widget(i)->property("preferredExtent").toInt());
    horizontal->setSizes(sizes);
    QObject::connect(horizontal, &QSplitter::splitterMoved, this, [this] { savePinnedExtents(); });
    vertical->addWidget(horizontal);
    vertical->setStretchFactor(vertical->indexOf(horizontal), 1);
    if (auto* bottom = buildPinned(Edge::Bottom, contents)) vertical->addWidget(bottom);
    sizes.clear();
    for (int i = 0; i < vertical->count(); ++i) sizes << (vertical->widget(i) == horizontal ? 700
        : vertical->widget(i)->property("preferredExtent").toInt());
    vertical->setSizes(sizes);
    QObject::connect(vertical, &QSplitter::splitterMoved, this, [this] { savePinnedExtents(); });
    outer->addWidget(vertical, 1, 1);
    layout()->addWidget(root_);
    // Layout insertion into an already visible parent defers the implicit show.
    // A completed API mutation must expose its replacement controls immediately,
    // especially when a drag continues in the same input event.
    layout()->activate();
    root_->show();
    root_->layout()->activate();
    restorePinnedExtents();
    // The final viewport size is assigned by the owning window's layout. Apply
    // pixel extents after that pass instead of repeatedly scaling saved widths
    // against a hard-coded central-region size during each rebuild.
    QTimer::singleShot(0, this, [this] { restorePinnedExtents(); });
    applyFocusScope();
}
QWidget* DockSurface::buildTree(LayoutTree node, const QHash<TabId, QPointer<TabContent>>& contents)
{
    if (!node) return nullptr;
    if (node->isLeaf()) return buildGroup(node->itemId, contents);
    auto* split = new QSplitter(node->orientation, this);
    split->setObjectName("GroupSplitter"); configureSplitter(split);
    split->addWidget(buildTree(node->first, contents));
    split->addWidget(buildTree(node->second, contents));
    split->setSizes({qRound(1000 * node->ratio), qRound(1000 * (1 - node->ratio))});
    split->setProperty("splitNodeId", node->id);
    const auto nodeId = node->id;
    QObject::connect(split, &QSplitter::splitterMoved, manager_, [this, split, nodeId] {
        const auto sizes = split->sizes();
        manager_->execute("layout.setRatio", {{"node", nodeId},
            {"ratio", double(sizes.value(0)) / qMax(1, sizes.value(0) + sizes.value(1))}}, window_);
    });
    return split;
}
GroupTabs* DockSurface::buildGroup(GroupId id, const QHash<TabId, QPointer<TabContent>>& contents)
{
    const auto* group = manager_->model().group(id);
    if (!group) return nullptr;
    auto* tabs = new GroupTabs(manager_->theme(), this);
    tabs->setObjectName("TabGroup"); tabs->setProperty("groupId", id);
    tabs->setTabBarVisible(group->location != GroupLocation::Central || manager_->tabBarsVisible(window_));
    auto* bar = tabs->tabBar();
    for (const auto tabId : group->tabs) {
        if (auto content = contents.value(tabId)) {
            const auto* record = manager_->model().tab(tabId);
            tabs->addTab(content, manager_->tabOptions(tabId).showIcon ? iconFor("window", manager_->theme()) : QIcon{},
                         record->title, tabId);
        }
    }
    for (int i = 0; i < bar->count(); ++i)
        if (bar->tabData(i).toUuid() == group->activeTab) tabs->setCurrentIndex(i);
    groups_.insert(id, tabs);
    tabs->hoverGeometryChanged = [this] { manager_->d->drag->refreshHover(); };
    if (group->location == GroupLocation::Pinned) {
        tabs->setPinnedActions([this, id] {
            const auto* current = manager_->model().group(id);
            if (current) manager_->pinTab(current->activeTab, false);
        }, [this, edge = group->edge] { manager_->pinEdge(window_, edge, false); });
    }
    bar->drag = [this, bar](int index, QPoint point) {
        manager_->beginTabDrag(bar->tabData(index).toUuid(), point);
    };
    bar->menu = [this, bar](int index, QPoint point) {
        if (index >= 0) manager_->showTabMenu(bar->tabData(index).toUuid(), point);
    };
    tabs->currentChanged = [this, tabs, bar, id](int index) {
        if (index >= 0) manager_->updateActive(window_, id, bar->tabData(index).toUuid());
        tabs->update();
    };
    tabs->closeRequested = [this, bar](int index) {
        manager_->closeTab(bar->tabData(index).toUuid());
    };
    return tabs;
}
QWidget* DockSurface::buildEdge(Edge edge, const QHash<TabId, QPointer<TabContent>>&)
{
    const auto* window = manager_->model().window(window_);
    if (!window) return nullptr;
    QVector<GroupId> ids;
    for (const auto id : window->edgeGroups) {
        const auto* group = manager_->model().group(id);
        if (group && group->edge == edge && group->location == GroupLocation::AutoHide) ids << id;
    }
    if (ids.isEmpty()) return nullptr;
    const bool vertical = edge == Edge::Left || edge == Edge::Right;
    auto* bar = new EdgeStrip(edge, manager_->theme(), this);
    bar->hoverGeometryChanged = [this] { manager_->d->drag->refreshHover(); };
    if (!manager_->sideBarsVisible(window_)) bar->hide();
    bar->setProperty("edge", int(edge)); edgeBars_.insert(edge, bar);
    for (const auto groupId : ids) {
        for (const auto tabId : manager_->model().group(groupId)->tabs) {
            auto* button = new EdgeTabButton(manager_->model().tab(tabId)->title, vertical,
                manager_->theme().metrics.edgeThickness, bar);
            button->setProperty("tabId", tabId);
            button->drag = [this, tabId](QPoint point) { manager_->beginTabDrag(tabId, point); };
            button->menu = [this, tabId](QPoint point) { manager_->showTabMenu(tabId, point); };
            QObject::connect(button, &QAbstractButton::clicked, manager_, [this, groupId, tabId] {
                if (peek_ && peekTab_ == tabId) closePeek();
                else showEdgeGroup(groupId, tabId);
            });
            bar->addButton(button);
        }
    }
    auto* pinAll = new CompactToolButton(bar, manager_->theme().metrics.edgeThickness); pinAll->setObjectName("PinEdgeButton");
    pinAll->setIcon(iconFor("pin-all", manager_->theme())); pinAll->setToolTip(QStringLiteral("固定这一侧全部标签"));
    pinAll->setAccessibleName(pinAll->toolTip());
    QObject::connect(pinAll, &QToolButton::clicked, manager_, [this, edge] { manager_->pinEdge(window_, edge, true); });
    bar->setTailWidget(pinAll);
    return bar;
}
QWidget* DockSurface::buildPinned(Edge edge, const QHash<TabId, QPointer<TabContent>>& contents)
{
    const auto* window = manager_->model().window(window_);
    if (!window) return nullptr;
    QVector<GroupId> ids;
    for (const auto id : window->edgeGroups) {
        const auto* group = manager_->model().group(id);
        if (group && group->edge == edge && group->location == GroupLocation::Pinned) ids << id;
    }
    if (ids.isEmpty()) return nullptr;
    const auto orientation = edge == Edge::Left || edge == Edge::Right ? Qt::Horizontal : Qt::Vertical;
    auto* region = new QSplitter(orientation, this); region->setObjectName("PinnedRegion");
    pinnedRegions_.insert(edge, region);
    configureSplitter(region);
    int totalExtent = 0;
    for (const auto id : ids) totalExtent += manager_->model().group(id)->edgeExtent;
    totalExtent += (ids.size() - 1) * region->handleWidth();
    region->setProperty("preferredExtent", totalExtent);
    if (edge == Edge::Right || edge == Edge::Bottom) std::reverse(ids.begin(), ids.end());
    for (const auto id : ids) region->addWidget(buildGroup(id, contents));
    QList<int> sizes;
    for (const auto id : ids) sizes << manager_->model().group(id)->edgeExtent;
    region->setSizes(sizes);
    QObject::connect(region, &QSplitter::splitterMoved, this, [this] { savePinnedExtents(); });
    return region;
}
void DockSurface::savePinnedExtents()
{
    if (hasContentFocusScope()) return;
    for (auto iterator = groups_.cbegin(); iterator != groups_.cend(); ++iterator) {
        const auto* record = manager_->model().group(iterator.key());
        if (!record || record->location != GroupLocation::Pinned || !iterator.value()) continue;
        const auto extent = record->edge == Edge::Left || record->edge == Edge::Right
            ? iterator.value()->width() : iterator.value()->height();
        manager_->execute("layout.setEdgeExtent", {{"group", iterator.key()}, {"extent", extent}}, window_);
    }
    for (const auto& region : pinnedRegions_) if (region)
        region->setProperty("preferredExtent", region->orientation() == Qt::Horizontal ? region->width() : region->height());
}
void DockSurface::restorePinnedExtents()
{
    if (hasContentFocusScope()) return;
    if (!horizontal_ || !vertical_) return;
    const auto requested = [](QWidget* widget) { return widget->property("preferredExtent").toInt(); };
    auto restoreOuter = [&](QSplitter* splitter, QWidget* flexible) {
        QList<int> sizes;
        int fixed = 0;
        for (int i = 0; i < splitter->count(); ++i)
            if (splitter->widget(i) != flexible) fixed += requested(splitter->widget(i));
        const auto available = splitter->orientation() == Qt::Horizontal ? splitter->width() : splitter->height();
        for (int i = 0; i < splitter->count(); ++i)
            sizes << (splitter->widget(i) == flexible
                ? qMax(1, available - fixed - (splitter->count() - 1) * splitter->handleWidth())
                : requested(splitter->widget(i)));
        splitter->setSizes(sizes);
    };
    restoreOuter(vertical_, horizontal_);
    restoreOuter(horizontal_, center_);
    for (const auto& region : pinnedRegions_) if (region) {
        QList<int> sizes;
        for (int i = 0; i < region->count(); ++i) {
            const auto* group = manager_->model().group(region->widget(i)->property("groupId").toUuid());
            sizes << (group ? group->edgeExtent : 250);
        }
        region->setSizes(sizes);
    }
}
QHash<GroupId, QRect> DockSurface::groupRects() const
{
    QHash<GroupId, QRect> result;
    for (auto it = groups_.cbegin(); it != groups_.cend(); ++it)
        if (it.value() && it.value()->isVisible())
            result.insert(it.key(), QRect(it.value()->mapToGlobal(QPoint()), it.value()->size()));
    return result;
}
GroupId DockSurface::groupAt(QPoint point) const
{
    const auto rects = groupRects();
    for (auto it = rects.cbegin(); it != rects.cend(); ++it) if (it.value().contains(point)) return it.key();
    return {};
}
QHash<GroupId, QRect> DockSurface::tabBarRects() const
{
    QHash<GroupId, QRect> result;
    for (auto it = groups_.cbegin(); it != groups_.cend(); ++it)
        if (it.value() && it.value()->isVisible() && !it.value()->viewportRectGlobal().isEmpty())
            result.insert(it.key(), it.value()->viewportRectGlobal());
    return result;
}
GroupId DockSurface::tabBarAt(QPoint point) const
{
    const auto rects = tabBarRects();
    for (auto it = rects.cbegin(); it != rects.cend(); ++it) if (it.value().contains(point)) return it.key();
    return {};
}
int DockSurface::tabInsertionIndex(GroupId group, QPoint point) const
{
    const auto widget = groups_.value(group); return widget ? widget->insertionIndex(point) : -1;
}
int DockSurface::dragLabelExtent(TabId tab, bool edge) const
{
    const auto* record = manager_->model().tab(tab);
    if (!record) return 0;
    if (edge) return qBound(64, fontMetrics().horizontalAdvance(record->title) + 24, 180);
    if (const auto group = groups_.value(manager_->model().groupForTab(tab))) {
        const auto* bar = group->tabBar();
        for (int i = 0; i < bar->count(); ++i) if (bar->tabData(i).toUuid() == tab) return bar->tabRect(i).width();
    }
    const int icon = manager_->tabOptions(tab).showIcon ? manager_->theme().metrics.iconSize + 4 : 0;
    return qBound(1, fontMetrics().horizontalAdvance(record->title) + icon + 35, QWIDGETSIZE_MAX);
}
QHash<Edge, QRect> DockSurface::edgeBarRects() const
{
    QHash<Edge, QRect> result;
    for (auto it = edgeBars_.cbegin(); it != edgeBars_.cend(); ++it)
        if (it.value() && it.value()->isVisible()) result.insert(it.key(), QRect(it.value()->mapToGlobal(QPoint()), it.value()->size()));
    return result;
}
int DockSurface::edgeTabInsertionIndex(Edge edge, QPoint point) const
{
    const auto* bar = dynamic_cast<EdgeStrip*>(edgeBars_.value(edge).data());
    return bar ? bar->insertionIndex(point) : -1;
}
QRect DockSurface::edgeTabInsertionRect(Edge edge, int index) const
{
    const auto* bar = dynamic_cast<EdgeStrip*>(edgeBars_.value(edge).data());
    return bar ? bar->insertionMarkerRect(index) : QRect{};
}
void DockSurface::setDragHover(QPoint point)
{
    for (const auto& widget : groups_) if (widget) widget->setDragHover(point);
    for (const auto& widget : edgeBars_) if (auto* strip = dynamic_cast<EdgeStrip*>(widget.data())) strip->setDragHover(point);
}
void DockSurface::setInsertionPreview(GroupId group, int index, int extent)
{
    for (auto it = groups_.cbegin(); it != groups_.cend(); ++it)
        if (it.value()) it.value()->setInsertionPreview(it.key() == group ? index : -1, extent);
    for (const auto& widget : edgeBars_) if (auto* strip = dynamic_cast<EdgeStrip*>(widget.data())) strip->setInsertionPreview(-1, 0);
}
void DockSurface::setEdgeInsertionPreview(Edge edge, int index, int extent)
{
    for (const auto& widget : groups_) if (widget) widget->setInsertionPreview(-1, 0);
    for (auto it = edgeBars_.cbegin(); it != edgeBars_.cend(); ++it)
        if (auto* strip = dynamic_cast<EdgeStrip*>(it.value().data())) strip->setInsertionPreview(it.key() == edge ? index : -1, extent);
}
void DockSurface::clearInsertionPreviews()
{
    for (const auto& widget : groups_) if (widget) widget->setInsertionPreview(-1, 0);
    for (const auto& widget : edgeBars_) if (auto* strip = dynamic_cast<EdgeStrip*>(widget.data())) strip->setInsertionPreview(-1, 0);
}
QRect DockSurface::insertionPreviewRect(GroupId group) const
{
    const auto widget = groups_.value(group);
    return widget ? widget->insertionPreviewRectGlobal() : QRect{};
}
QRect DockSurface::edgeInsertionPreviewRect(Edge edge) const
{
    const auto* strip = dynamic_cast<EdgeStrip*>(edgeBars_.value(edge).data());
    return strip ? strip->insertionPreviewRect() : QRect{};
}
void DockSurface::setDragPresentation(bool compact, bool edge)
{
    for (auto it = groups_.cbegin(); it != groups_.cend(); ++it) if (it.value()) {
        it.value()->setDragPresentation(compact, edge);
        if (!compact) {
            const auto* group = manager_->model().group(it.key());
            it.value()->setTabBarVisible(group && (group->location != GroupLocation::Central || manager_->tabBarsVisible(window_)));
        }
    }
}
QRect DockSurface::centralRectGlobal() const
{
    if (hasContentFocusScope())
        if (auto group = groups_.value(focusState_.group))
            return QRect(group->mapToGlobal(QPoint()), group->size());
    return center_ ? QRect(center_->mapToGlobal(QPoint()), center_->size()) : QRect();
}
void DockSurface::selectTab(TabId tab)
{
    const auto group = manager_->model().groupForTab(tab);
    const auto* record = manager_->model().group(group);
    if (record && record->location == GroupLocation::AutoHide && !focusedAutoHideGroup_) {
        showEdgeGroup(group, tab); return;
    }
    if (auto widget = groups_.value(group)) {
        const auto index = record->tabs.indexOf(tab);
        widget->setCurrentIndex(index);
    }
}
void DockSurface::showEdgeGroup(GroupId group, TabId tab)
{
    const auto* record = manager_->model().group(group);
    if (!record || record->location != GroupLocation::AutoHide) return;
    if (tab.isNull()) tab = record->activeTab;
    if (!record->tabs.contains(tab)) return;
    if (hasContentFocusScope()) {
        if (group == focusState_.group && (focusState_.mode != ViewMode::PaneFullscreen || tab == focusState_.tab))
            selectTab(tab);
        else
            manager_->activateTab(tab);
        return;
    }
    closePeek();
    manager_->updateActive(window_, group, tab);
    peekGroup_ = group;
    peekTab_ = tab;
    peek_ = new QFrame(this); peek_->setObjectName("PeekPanel");
    auto* layout = new QVBoxLayout(peek_); layout->setContentsMargins(2, 2, 2, 2); layout->setSpacing(0);
    auto* header = new PeekHeader(peek_); header->setObjectName("PeekTitleBar");
    header->drag = [this, tab](QPoint point) { manager_->beginTabDrag(tab, point); };
    auto* row = new QHBoxLayout(header); row->setContentsMargins(6, 1, 2, 1);
    row->setSpacing(2);
    auto* title = new QLabel(manager_->model().tab(tab)->title, header);
    title->setAttribute(Qt::WA_TransparentForMouseEvents); row->addWidget(title); row->addStretch();
    auto* pin = new CompactToolButton(header, peekButtonExtent); pin->setIcon(iconFor("pin", manager_->theme()));
    pin->setToolTip(QStringLiteral("固定并占用布局空间")); pin->setAccessibleName(pin->toolTip()); row->addWidget(pin);
    auto* close = new CompactToolButton(header, peekButtonExtent); close->setIcon(iconFor("close", manager_->theme())); row->addWidget(close);
    close->setObjectName("ClosePeekTabButton");
    close->setToolTip(QStringLiteral("关闭标签")); close->setAccessibleName(close->toolTip());
    pin->setObjectName("PinPeekTabButton");
    QObject::connect(pin, &QToolButton::clicked, manager_, [this, tab] { manager_->pinTab(tab, true); });
    QObject::connect(close, &QToolButton::clicked, manager_, [this, tab] { manager_->closeTab(tab); });
    layout->addWidget(header);
    if (auto content = contents_.value(tab)) {
        layout->addWidget(content, 1);
        // Refresh parks tabs explicitly hidden. Showing their new ancestor
        // does not undo that flag, so restore the actual content visibility.
        content->show();
    }
    placePeek(); peek_->show(); peek_->raise();
    layout->activate();
}
void DockSurface::setTabBarsVisible(bool visible)
{
    for (auto it = groups_.cbegin(); it != groups_.cend(); ++it) {
        const auto* record = manager_->model().group(it.key());
        if (it.value() && record)
            it.value()->setTabBarVisible(!focusState_.zen && (record->location != GroupLocation::Central || visible));
    }
    if (root_) root_->layout()->activate();
    placePeek();
}
void DockSurface::setSideBarsVisible(bool visible)
{
    if (!visible) closePeek();
    for (auto bar : edgeBars_) if (bar) bar->setVisible(visible);
    if (root_) root_->layout()->activate();
    restorePinnedExtents();
    placePeek();
}
bool DockSurface::hasContentFocusScope() const
{
    return focusState_.mode == ViewMode::GroupFullscreen || focusState_.mode == ViewMode::PaneFullscreen;
}
bool DockSurface::ownsTab(TabId tab) const
{
    const auto* group = manager_->model().group(manager_->model().groupForTab(tab));
    return group && group->windowId == window_;
}
void DockSurface::restoreFocusScope()
{
    focusProjection_.restore();
    for (auto iterator = groups_.cbegin(); iterator != groups_.cend(); ++iterator) {
        const auto* group = manager_->model().group(iterator.key());
        if (iterator.value() && group && group->location == GroupLocation::Pinned)
            iterator.value()->setTabBarVisible(true);
    }
    for (auto iterator = contents_.cbegin(); iterator != contents_.cend(); ++iterator)
        if (iterator.value() && ownsTab(iterator.key())) iterator.value()->setFocusedPane({});
    if (focusedAutoHideGroup_) {
        // The transient group owns only chrome. Return the same live tab views
        // before deleting its stack, including after a manager refresh parks
        // those views in storage.
        for (const auto& content : contents_) if (content && focusedAutoHideGroup_->isAncestorOf(content)) {
            content->setParent(this);
            content->hide();
        }
        const auto group = focusedAutoHideGroup_->property("groupId").toUuid();
        if (groups_.value(group) == focusedAutoHideGroup_) groups_.remove(group);
        delete focusedAutoHideGroup_;
        focusedAutoHideGroup_.clear();
        if (vertical_) vertical_->setVisible(!focusedVerticalWasHidden_);
    }
    if (root_) root_->show();
}
void DockSurface::applyFocusScope()
{
    if (!hasContentFocusScope()) return;
    const auto* record = manager_->model().group(focusState_.group);
    if (!record || record->windowId != window_) {
        focusState_.mode = ViewMode::Normal;
        return;
    }
    closePeek();
    auto* target = groups_.value(focusState_.group).data();
    if (record->location == GroupLocation::AutoHide) {
        focusedAutoHideGroup_ = buildGroup(record->id, contents_);
        target = focusedAutoHideGroup_;
        if (!target) return;
        focusedVerticalWasHidden_ = vertical_ && vertical_->isHidden();
        if (vertical_) vertical_->hide();
        auto* outer = root_ ? qobject_cast<QGridLayout*>(root_->layout()) : nullptr;
        if (!outer) return;
        outer->addWidget(target, 1, 1);
        target->show();
    } else {
        focusProjection_.apply(target, root_);
    }
    if (target) target->setTabBarVisible(!focusState_.zen
        && (record->location != GroupLocation::Central || focusState_.effective.tabBars));
    if (focusState_.mode == ViewMode::PaneFullscreen)
        if (auto content = contents_.value(focusState_.tab)) content->setFocusedPane(focusState_.pane);
    layout()->activate();
}
void DockSurface::setFocusScope(const WindowViewState& state)
{
    QPointer<QWidget> focused = QApplication::focusWidget();
    if (focused && !isAncestorOf(focused)) focused.clear();
    const bool wasFocused = hasContentFocusScope();
    const bool focusedMode = state.mode == ViewMode::GroupFullscreen || state.mode == ViewMode::PaneFullscreen;
    const auto* group = manager_->model().group(state.group);
    const bool validTarget = group && group->windowId == window_
        && (state.mode != ViewMode::PaneFullscreen || (group->tabs.contains(state.tab)
            && manager_->model().panes(state.tab).contains(state.pane)));
    // Display preference changes must not replace the projection's original
    // splitter sizes with dimensions already enlarged by the focused scope.
    // Keep the group projection until its target actually changes.
    if (wasFocused && focusedMode && validTarget && focusState_.group == state.group) {
        const bool samePane = focusState_.mode == ViewMode::PaneFullscreen && state.mode == ViewMode::PaneFullscreen
            && focusState_.tab == state.tab && focusState_.pane == state.pane;
        if (!samePane) {
            for (auto iterator = contents_.cbegin(); iterator != contents_.cend(); ++iterator)
                if (iterator.value() && ownsTab(iterator.key())) iterator.value()->setFocusedPane({});
            if (state.mode == ViewMode::PaneFullscreen)
                if (auto content = contents_.value(state.tab)) content->setFocusedPane(state.pane);
        }
        focusState_ = state;
        for (auto iterator = contents_.cbegin(); iterator != contents_.cend(); ++iterator)
            if (iterator.value() && ownsTab(iterator.key())) iterator.value()->setBarsVisible(!state.zen);
        setTabBarsVisible(state.effective.tabBars);
        setSideBarsVisible(state.effective.sideBars);
        if (focused && focused->isVisible()) focused->setFocus(Qt::OtherFocusReason);
        return;
    }
    if (!wasFocused && (state.mode == ViewMode::GroupFullscreen || state.mode == ViewMode::PaneFullscreen)) {
        previousPeekGroup_ = peekGroup_;
        previousPeekTab_ = peekTab_;
    }
    restoreFocusScope();
    focusState_ = state;
    if (hasContentFocusScope() && !validTarget)
        focusState_.mode = ViewMode::Normal;
    applyFocusScope();
    for (auto iterator = contents_.cbegin(); iterator != contents_.cend(); ++iterator)
        if (iterator.value() && ownsTab(iterator.key())) iterator.value()->setBarsVisible(!state.zen);
    setTabBarsVisible(state.effective.tabBars);
    setSideBarsVisible(state.effective.sideBars);
    if (wasFocused && !hasContentFocusScope()) {
        const auto* previous = manager_->model().group(previousPeekGroup_);
        if (previous && previous->location == GroupLocation::AutoHide && previous->tabs.contains(previousPeekTab_))
            showEdgeGroup(previousPeekGroup_, previousPeekTab_);
        previousPeekGroup_ = GroupId{};
        previousPeekTab_ = TabId{};
    }
    if (focused && focused->isVisible()) focused->setFocus(Qt::OtherFocusReason);
}
void DockSurface::closePeek()
{
    if (!peek_) return;
    for (const auto& content : contents_)
        if (content && peek_->isAncestorOf(content)) { content->setParent(this); content->hide(); }
    delete peek_; peek_.clear(); peekGroup_ = GroupId{}; peekTab_ = TabId{};
}
void DockSurface::placePeek()
{
    if (!peek_) return;
    const auto* group = manager_->model().group(peekGroup_);
    if (!group) return;
    QRect available = rect();
    if (auto bar = edgeBars_.value(Edge::Left); bar && !bar->isHidden()) available.setLeft(bar->mapTo(this, QPoint(bar->width(), 0)).x());
    if (auto bar = edgeBars_.value(Edge::Right); bar && !bar->isHidden()) available.setRight(bar->mapTo(this, QPoint()).x() - 1);
    if (auto bar = edgeBars_.value(Edge::Top); bar && !bar->isHidden()) available.setTop(bar->mapTo(this, QPoint(0, bar->height())).y());
    if (auto bar = edgeBars_.value(Edge::Bottom); bar && !bar->isHidden()) available.setBottom(bar->mapTo(this, QPoint()).y() - 1);
    const auto h = qMin(available.height(), qMax(140, available.height() / 2));
    const auto w = qMin(available.width(), qMax(240, available.width() / 3));
    switch (group->edge) {
    case Edge::Left: peek_->setGeometry(available.x(), available.y(), w, available.height()); break;
    case Edge::Right: peek_->setGeometry(available.right() - w + 1, available.y(), w, available.height()); break;
    case Edge::Top: peek_->setGeometry(available.x(), available.y(), available.width(), h); break;
    case Edge::Bottom: peek_->setGeometry(available.x(), available.bottom() - h + 1, available.width(), h); break;
    }
}
void DockSurface::resizeEvent(QResizeEvent* event) { QFrame::resizeEvent(event); restorePinnedExtents(); placePeek(); }
bool DockSurface::eventFilter(QObject* watched, QEvent* event)
{
    if (!peek_) return QFrame::eventFilter(watched, event);
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        auto* widget = qobject_cast<QWidget*>(watched);
        if (key->key() == Qt::Key_Escape && widget && widget->window() == window() &&
            !QApplication::activePopupWidget()) { closePeek(); return true; }
    }
    if (event->type() == QEvent::MouseButtonPress) {
        auto* widget = qobject_cast<QWidget*>(watched);
        if (widget && widget->window() == window() && !peek_->isAncestorOf(widget) && widget != peek_ &&
            widget->objectName() != "EdgeButton") closePeek();
    }
    return QFrame::eventFilter(watched, event);
}
} // namespace window_system
