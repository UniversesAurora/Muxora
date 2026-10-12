#include "dock_surface.hpp"
#include "tab_strip.hpp"
#include "panes/tab_content.hpp"
#include <QAbstractButton>
#include <QApplication>
#include <QBoxLayout>
#include <QEvent>
#include <QLabel>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QTimer>
#include <algorithm>

namespace window_system {
namespace {
class EdgeButton final : public QAbstractButton {
public:
    std::function<void(QPoint)> drag;
    EdgeButton(QString title, bool vertical, QWidget* parent)
        : QAbstractButton(parent), vertical_(vertical) {
        setText(title); setToolTip(title); setAccessibleName(title);
        setObjectName("EdgeButton"); setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
    }
    QSize sizeHint() const override {
        const auto extent = qBound(64, fontMetrics().horizontalAdvance(text()) + 24, 180);
        return vertical_ ? QSize(26, extent) : QSize(extent, 26);
    }
protected:
    void mousePressEvent(QMouseEvent* event) override {
        start_ = event->position().toPoint(); QAbstractButton::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (event->buttons().testFlag(Qt::LeftButton) &&
            (event->position().toPoint() - start_).manhattanLength() >= QApplication::startDragDistance()) {
            setDown(false); const auto callback = drag;
            if (callback) callback(event->globalPosition().toPoint());
            return;
        }
        QAbstractButton::mouseMoveEvent(event);
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), underMouse() || hasFocus() ? QColor("#8970AF") : QColor("#6A568C"));
        p.setPen(Qt::white);
        if (vertical_) { p.translate(width(), 0); p.rotate(90); p.drawText(QRect(0, 0, height(), width()), Qt::AlignCenter, text()); }
        else p.drawText(rect(), Qt::AlignCenter, text());
    }
private:
    bool vertical_;
    QPoint start_;
};
class PeekHeader final : public QWidget {
public:
    using QWidget::QWidget;
    std::function<void(QPoint)> drag;
protected:
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
    closePeek(); groups_.clear(); edgeBars_.clear(); pinnedRegions_.clear(); contents_ = contents;
    if (root_) {
        layout()->removeWidget(root_);
        root_->hide();
        root_->deleteLater();
    }
    root_ = new QWidget(this);
    auto* outer = new QVBoxLayout(root_);
    outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(0);
    if (auto* top = buildEdge(Edge::Top, contents)) outer->addWidget(top);
    auto* middle = new QWidget(root_);
    auto* row = new QHBoxLayout(middle);
    row->setContentsMargins(0, 0, 0, 0); row->setSpacing(0);
    if (auto* left = buildEdge(Edge::Left, contents)) row->addWidget(left);
    auto* vertical = new QSplitter(Qt::Vertical, middle);
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
    row->addWidget(vertical, 1);
    if (auto* right = buildEdge(Edge::Right, contents)) row->addWidget(right);
    outer->addWidget(middle, 1);
    if (auto* bottom = buildEdge(Edge::Bottom, contents)) outer->addWidget(bottom);
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
    auto* bar = new QFrame(this); bar->setObjectName("EdgeBar");
    bar->setProperty("edge", int(edge)); edgeBars_.insert(edge, bar);
    auto* layout = new QBoxLayout(vertical ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight, bar);
    layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(2);
    auto* pinAll = new QToolButton(bar); pinAll->setObjectName("PinEdgeButton");
    pinAll->setIcon(iconFor("pin", manager_->theme())); pinAll->setToolTip(QStringLiteral("固定这一侧全部标签"));
    pinAll->setAccessibleName(pinAll->toolTip());
    QObject::connect(pinAll, &QToolButton::clicked, manager_, [this, edge] { manager_->pinEdge(window_, edge, true); });
    layout->addWidget(pinAll);
    for (const auto groupId : ids) {
        for (const auto tabId : manager_->model().group(groupId)->tabs) {
            auto* button = new EdgeButton(manager_->model().tab(tabId)->title, vertical, bar);
            button->setProperty("tabId", tabId);
            button->drag = [this, tabId](QPoint point) { manager_->beginTabDrag(tabId, point); };
            QObject::connect(button, &QAbstractButton::clicked, manager_, [this, groupId, tabId] {
                if (peek_ && peekTab_ == tabId) closePeek();
                else showEdgeGroup(groupId, tabId);
            });
            layout->addWidget(button);
        }
    }
    layout->addStretch();
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
        if (it.value() && it.value()->isVisible()) result.insert(it.key(), it.value()->viewportRectGlobal());
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
QHash<Edge, QRect> DockSurface::edgeBarRects() const
{
    QHash<Edge, QRect> result;
    for (auto it = edgeBars_.cbegin(); it != edgeBars_.cend(); ++it)
        if (it.value() && it.value()->isVisible()) result.insert(it.key(), QRect(it.value()->mapToGlobal(QPoint()), it.value()->size()));
    return result;
}
void DockSurface::setDragHover(QPoint point)
{
    for (const auto& widget : groups_) if (widget) widget->setDragHover(point);
}
QRect DockSurface::centralRectGlobal() const
{
    return center_ ? QRect(center_->mapToGlobal(QPoint()), center_->size()) : QRect();
}
void DockSurface::selectTab(TabId tab)
{
    const auto group = manager_->model().groupForTab(tab);
    const auto* record = manager_->model().group(group);
    if (record && record->location == GroupLocation::AutoHide) { showEdgeGroup(group, tab); return; }
    if (auto widget = groups_.value(group)) {
        const auto index = record->tabs.indexOf(tab);
        widget->setCurrentIndex(index);
    }
}
void DockSurface::showEdgeGroup(GroupId group, TabId tab)
{
    closePeek();
    const auto* record = manager_->model().group(group);
    if (!record || record->location != GroupLocation::AutoHide) return;
    if (tab.isNull()) tab = record->activeTab;
    if (!record->tabs.contains(tab)) return;
    manager_->updateActive(window_, group, tab);
    peekGroup_ = group;
    peekTab_ = tab;
    peek_ = new QFrame(this); peek_->setObjectName("PeekPanel");
    auto* layout = new QVBoxLayout(peek_); layout->setContentsMargins(2, 2, 2, 2); layout->setSpacing(0);
    auto* header = new PeekHeader(peek_); header->setObjectName("PeekTitleBar");
    header->drag = [this, tab](QPoint point) { manager_->beginTabDrag(tab, point); };
    auto* row = new QHBoxLayout(header); row->setContentsMargins(6, 1, 2, 1);
    auto* title = new QLabel(manager_->model().tab(tab)->title, header);
    title->setAttribute(Qt::WA_TransparentForMouseEvents); row->addWidget(title); row->addStretch();
    auto* pin = new QToolButton(header); pin->setIcon(iconFor("pin", manager_->theme()));
    pin->setToolTip(QStringLiteral("固定并占用布局空间")); row->addWidget(pin);
    auto* close = new QToolButton(header); close->setIcon(iconFor("close", manager_->theme())); row->addWidget(close);
    pin->setObjectName("PinPeekTabButton");
    QObject::connect(pin, &QToolButton::clicked, manager_, [this, tab] { manager_->pinTab(tab, true); });
    QObject::connect(close, &QToolButton::clicked, this, [this] { closePeek(); });
    layout->addWidget(header);
    if (auto content = contents_.value(tab)) layout->addWidget(content, 1);
    placePeek(); peek_->show(); peek_->raise();
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
    const auto h = qMin(height() - 40, qMax(140, height() / 2));
    const auto w = qMin(width() - 40, qMax(240, width() / 3));
    switch (group->edge) {
    case Edge::Left: peek_->setGeometry(26, 0, w, height()); break;
    case Edge::Right: peek_->setGeometry(width() - w - 26, 0, w, height()); break;
    case Edge::Top: peek_->setGeometry(0, 26, width(), h); break;
    case Edge::Bottom: peek_->setGeometry(0, height() - h - 26, width(), h); break;
    }
}
void DockSurface::resizeEvent(QResizeEvent* event) { QFrame::resizeEvent(event); restorePinnedExtents(); placePeek(); }
bool DockSurface::eventFilter(QObject* watched, QEvent* event)
{
    if (!peek_) return QFrame::eventFilter(watched, event);
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) { closePeek(); return true; }
    }
    if (event->type() == QEvent::MouseButtonPress) {
        auto* widget = qobject_cast<QWidget*>(watched);
        if (widget && widget->window() == window() && !peek_->isAncestorOf(widget) && widget != peek_ &&
            widget->objectName() != "EdgeButton") closePeek();
    }
    return QFrame::eventFilter(watched, event);
}
} // namespace window_system
