#include "dock_surface.hpp"
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

namespace window_system {
namespace {
class DragTabBar final : public QTabBar {
public:
    std::function<void(int, QPoint)> drag;
    std::function<void(int, QPoint)> menu;
    using QTabBar::QTabBar;
protected:
    void mousePressEvent(QMouseEvent* event) override {
        start_ = event->position().toPoint();
        index_ = tabAt(start_);
        QTabBar::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (event->buttons().testFlag(Qt::LeftButton) && index_ >= 0 &&
            (event->position().toPoint() - start_).manhattanLength() >= QApplication::startDragDistance()) {
            const auto index = index_; index_ = -1;
            if (drag) drag(index, event->globalPosition().toPoint());
            return;
        }
        QTabBar::mouseMoveEvent(event);
    }
    void contextMenuEvent(QContextMenuEvent* event) override {
        if (menu) menu(tabAt(event->pos()), event->globalPos());
    }
private:
    QPoint start_;
    int index_ = -1;
};
class GroupTabs final : public QTabWidget {
public:
    using QTabWidget::QTabWidget;
    using QTabWidget::setTabBar;
};
class EdgeButton final : public QAbstractButton {
public:
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
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), underMouse() || hasFocus() ? QColor("#8970AF") : QColor("#6A568C"));
        p.setPen(Qt::white);
        if (vertical_) { p.translate(width(), 0); p.rotate(90); p.drawText(QRect(0, 0, height(), width()), Qt::AlignCenter, text()); }
        else p.drawText(rect(), Qt::AlignCenter, text());
    }
private:
    bool vertical_;
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
    closePeek(); groups_.clear(); contents_ = contents;
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
    configureSplitter(vertical);
    if (auto* top = buildPinned(Edge::Top, contents)) vertical->addWidget(top);
    auto* horizontal = new QSplitter(Qt::Horizontal, vertical);
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
    for (int i = 0; i < horizontal->count(); ++i) sizes << (horizontal->widget(i) == center_ ? 900 : 250);
    horizontal->setSizes(sizes);
    vertical->addWidget(horizontal);
    vertical->setStretchFactor(vertical->indexOf(horizontal), 1);
    if (auto* bottom = buildPinned(Edge::Bottom, contents)) vertical->addWidget(bottom);
    sizes.clear();
    for (int i = 0; i < vertical->count(); ++i) sizes << (vertical->widget(i) == horizontal ? 700 : 170);
    vertical->setSizes(sizes);
    row->addWidget(vertical, 1);
    if (auto* right = buildEdge(Edge::Right, contents)) row->addWidget(right);
    outer->addWidget(middle, 1);
    if (auto* bottom = buildEdge(Edge::Bottom, contents)) outer->addWidget(bottom);
    layout()->addWidget(root_);
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
QTabWidget* DockSurface::buildGroup(GroupId id, const QHash<TabId, QPointer<TabContent>>& contents)
{
    const auto* group = manager_->model().group(id);
    if (!group) return nullptr;
    auto* tabs = new GroupTabs(this);
    tabs->setObjectName("TabGroup"); tabs->setProperty("groupId", id);
    tabs->setMinimumSize(165, 110);
    auto* bar = new DragTabBar(tabs);
    tabs->setTabBar(bar); bar->setObjectName("TabBar");
    bar->setExpanding(false); bar->setElideMode(Qt::ElideRight);
    tabs->setTabsClosable(true); tabs->setDocumentMode(true);
    tabs->setUsesScrollButtons(true);
    for (const auto tabId : group->tabs) {
        if (auto content = contents.value(tabId)) {
            const auto* record = manager_->model().tab(tabId);
            const auto index = tabs->addTab(content, iconFor("window", manager_->theme()), record->title);
            bar->setTabData(index, tabId); tabs->setTabToolTip(index, record->title);
        }
    }
    for (int i = 0; i < bar->count(); ++i)
        if (bar->tabData(i).toUuid() == group->activeTab) tabs->setCurrentIndex(i);
    groups_.insert(id, tabs);
    auto* corner = new QToolButton(tabs);
    corner->setIcon(iconFor(group->location == GroupLocation::Pinned ? "unpin" : "chevron-down", manager_->theme()));
    corner->setToolTip(group->location == GroupLocation::Pinned ? QStringLiteral("取消固定") : QStringLiteral("标签操作"));
    corner->setAccessibleName(corner->toolTip());
    tabs->setCornerWidget(corner);
    QObject::connect(corner, &QToolButton::clicked, manager_, [this, id, corner] {
        const auto* current = manager_->model().group(id);
        if (!current) return;
        if (current->location == GroupLocation::Pinned) manager_->pinGroup(id, false);
        else manager_->showTabMenu(current->activeTab, corner->mapToGlobal(QPoint(0, corner->height())));
    });
    bar->drag = [this, bar](int index, QPoint point) {
        manager_->beginTabDrag(bar->tabData(index).toUuid(), point);
    };
    bar->menu = [this, bar](int index, QPoint point) {
        if (index >= 0) manager_->showTabMenu(bar->tabData(index).toUuid(), point);
    };
    QObject::connect(tabs, &QTabWidget::currentChanged, manager_, [this, tabs, bar, id](int index) {
        if (index >= 0) manager_->updateActive(window_, id, bar->tabData(index).toUuid());
        tabs->update();
    });
    QObject::connect(tabs, &QTabWidget::tabCloseRequested, manager_, [this, bar](int index) {
        manager_->closeTab(bar->tabData(index).toUuid());
    });
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
    auto* layout = new QBoxLayout(vertical ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight, bar);
    layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(2);
    for (const auto groupId : ids) {
        for (const auto tabId : manager_->model().group(groupId)->tabs) {
            auto* button = new EdgeButton(manager_->model().tab(tabId)->title, vertical, bar);
            button->setProperty("tabId", tabId);
            QObject::connect(button, &QAbstractButton::clicked, manager_, [this, groupId, tabId] {
                if (peek_ && peekGroup_ == groupId && manager_->model().group(groupId)->activeTab == tabId) closePeek();
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
    auto* region = new QWidget(this); region->setObjectName("PinnedRegion");
    auto* layout = new QVBoxLayout(region); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(2);
    for (const auto id : ids) layout->addWidget(buildGroup(id, contents));
    return region;
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
    if (!tab.isNull()) manager_->updateActive(window_, group, tab);
    peekGroup_ = group;
    peek_ = new QFrame(this); peek_->setObjectName("PeekPanel");
    auto* layout = new QVBoxLayout(peek_); layout->setContentsMargins(2, 2, 2, 2); layout->setSpacing(0);
    auto* header = new QWidget(peek_);
    auto* row = new QHBoxLayout(header); row->setContentsMargins(6, 1, 2, 1);
    row->addWidget(new QLabel(QStringLiteral("临时展开"), header)); row->addStretch();
    auto* pin = new QToolButton(header); pin->setIcon(iconFor("pin", manager_->theme()));
    pin->setToolTip(QStringLiteral("固定并占用布局空间")); row->addWidget(pin);
    auto* close = new QToolButton(header); close->setIcon(iconFor("close", manager_->theme())); row->addWidget(close);
    QObject::connect(pin, &QToolButton::clicked, manager_, [this, group] { manager_->pinGroup(group, true); });
    QObject::connect(close, &QToolButton::clicked, this, [this] { closePeek(); });
    layout->addWidget(header);
    layout->addWidget(buildGroup(group, contents_), 1);
    placePeek(); peek_->show(); peek_->raise();
}
void DockSurface::closePeek()
{
    if (!peek_) return;
    for (auto content : contents_)
        if (content && peek_->isAncestorOf(content)) { content->setParent(this); content->hide(); }
    groups_.remove(peekGroup_); delete peek_; peek_.clear(); peekGroup_ = GroupId{};
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
void DockSurface::resizeEvent(QResizeEvent* event) { QFrame::resizeEvent(event); placePeek(); }
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
