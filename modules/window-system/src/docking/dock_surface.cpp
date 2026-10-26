#include <QCoreApplication>
#include "dock_surface.hpp"
#include "edge_strip.hpp"
#include "edge_tab_button.hpp"
#include "peek_metrics.hpp"
#include "peek_resize_handle.hpp"
#include "splitter_layout_pass.hpp"
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
#include <QPainter>
#include <QPaintEvent>
#include <QScopeGuard>
#include <QScopedValueRollback>
#include <QSplitter>
#include <QSplitterHandle>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QTimer>
#include <algorithm>
#include <utility>

namespace window_system {
namespace {
constexpr int surfaceOuterMargin = 4;

// A group's tab folders as its strip shows them, in strip order.
QVector<TabStripFolder> stripFolders(const LayoutModel& model, const GroupRecord& group)
{
    QVector<TabStripFolder> result;
    for (int index = 0; index < group.tabs.size(); ++index) {
        const auto* tab = model.tab(group.tabs.at(index));
        if (!tab || tab->folder.isNull()) continue;
        if (!result.isEmpty() && result.last().id == tab->folder) { ++result.last().count; continue; }
        const auto* folder = model.folder(tab->folder);
        if (!folder) continue;
        result.append({folder->id, folder->name, folder->colorId, folder->collapsed, index, 1});
    }
    return result;
}
}
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
// Splitters that hold group strips report their synchronous geometry passes,
// including handle drags, so a rebuild can tell when moving a strip out of
// them is unsafe (see SplitterLayoutPass).
class GroupSplitterHandle final : public QSplitterHandle {
public:
    using QSplitterHandle::QSplitterHandle;
protected:
    void resizeEvent(QResizeEvent* event) override
    {
        // Both Qt's tiny-handle margins and our wider margins synchronously
        // dispatch Resize again. Let the outer pass finish the final mask.
        if (updatingMargins_) return;
        const QScopedValueRollback guard(updatingMargins_, true);
        QSplitterHandle::resizeEvent(event);
        // Use QSplitter's own overlap mechanism: margins receive the mouse,
        // the contents rectangle is painted, and Qt still owns drag limits.
        const int margin = std::max(0, (TabMetrics{}.resizeBand - splitter()->handleWidth() + 1) / 2);
        setAttribute(Qt::WA_MouseNoMask, margin > 0);
        if (orientation() == Qt::Horizontal) setContentsMargins(margin, 0, margin, 0);
        else setContentsMargins(0, margin, 0, margin);
        if (margin) setMask(QRegion(contentsRect())); else clearMask();
    }
    bool event(QEvent* event) override
    {
        const SplitterLayoutPass pass;
        return QSplitterHandle::event(event);
    }
private:
    bool updatingMargins_ = false;
};
QSize grownBy(QSize size, QMargins margins) { return size + QSize(margins.left() + margins.right(), margins.top() + margins.bottom()); }
class GroupSplitter final : public QSplitter {
public:
    using QSplitter::QSplitter;
    // QSplitter places its children in the contents rectangle but leaves the
    // margins out of its size hints.
    QSize sizeHint() const override { return grownBy(QSplitter::sizeHint(), contentsMargins()); }
    QSize minimumSizeHint() const override { return grownBy(QSplitter::minimumSizeHint(), contentsMargins()); }
protected:
    bool event(QEvent* event) override
    {
        const SplitterLayoutPass pass;
        return QSplitter::event(event);
    }
    QSplitterHandle* createHandle() override
    {
        auto* handle = new GroupSplitterHandle(orientation(), this);
        handle->setAutoFillBackground(false);
        return handle;
    }
};

// A removed group has no new bar to finish its labels' departure. This layer
// retains only visible label drawing data, never pages, panes or widgets.
class RetiringLabels final : public QWidget {
public:
    RetiringLabels(TabBar::VisualState labels, const Theme& theme, QWidget* parent)
        : QWidget(parent), labels_(std::move(labels)), painter_(TabMetrics::forTheme(theme, font()), TabPalette::forTheme(theme))
    {
        setObjectName("RetiringTabLabels"); setAttribute(Qt::WA_TransparentForMouseEvents);
        QRect bounds;
        for (auto it = labels_.begin(); it != labels_.end();)
            if (!it->retired) it = labels_.erase(it); else { bounds |= it->global; ++it; }
        const auto local = bounds.translated(-parent->mapToGlobal(QPoint())).intersected(parent->rect());
        setGeometry(local); origin_ = parent->mapToGlobal(local.topLeft());
        connect(&visibility_, &QVariantAnimation::valueChanged, this, [this] { update(); });
        connect(&visibility_, &QVariantAnimation::finished, this, &QObject::deleteLater);
        if (local.isEmpty()) { deleteLater(); return; }
        show(); raise(); visibility_.animateTo(0, Motion::Presence, true);
    }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter paint(this);
        for (const auto& label : std::as_const(labels_))
            TabBar::paintRetiredLabel(paint, painter_, label, origin_, visibility_.value());
    }
private:
    TabBar::VisualState labels_;
    TabPainter painter_;
    ProgressAnimation visibility_{1};
    QPoint origin_;
};
void configureSplitter(QSplitter* splitter, int width) {
    splitter->setAutoFillBackground(false);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(qMax(1, width));
}
void configureGroupSplitter(QSplitter* splitter, const Theme& theme) {
    splitter->setProperty("windowSystemGroupSplitter", true);
    configureSplitter(splitter, theme.metrics.groupSplitterWidth);
}
// Takes a reused strip out of the previous tree without editing the item
// list of its splitter, for a rebuild inside a splitter geometry pass.
// replaceWidget() keeps the list intact and leaves an inert placeholder in
// the old tree, which is deleted with the old root. It parents the strip to
// nothing for a moment, so its style is resolved again when it returns to
// the window; outside a pass the strip moves directly instead.
void detachFromSplitter(QWidget* widget)
{
    auto* splitter = qobject_cast<QSplitter*>(widget->parentWidget());
    const int index = splitter ? splitter->indexOf(widget) : -1;
    if (index >= 0) splitter->replaceWidget(index, new QWidget);
}
int pinnedExtent(LayoutTree tree, const LayoutModel& model, Qt::Orientation axis, int gap) {
    if (!tree) return 0;
    if (tree->isLeaf()) {
        const auto* group = model.group(tree->itemId);
        return group ? group->edgeExtent : 0;
    }
    const int first = pinnedExtent(tree->first, model, axis, gap);
    const int second = pinnedExtent(tree->second, model, axis, gap);
    return tree->orientation == axis ? first + second + gap : qMax(first, second);
}
} // namespace

DockSurface::DockSurface(WindowManager* manager, WindowId window, QWidget* parent)
    : QFrame(parent), manager_(manager), window_(window)
{
    setObjectName("DockSurface");
    setAutoFillBackground(false);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
    qApp->installEventFilter(this);
    QObject::connect(qApp, &QGuiApplication::fontDatabaseChanged, this, [this] { dragTitleLayouts_.clear(); });
}
DockSurface::~DockSurface()
{
    // QWidget deletes its children after this class's members are gone.
    // A QPointer/context still exists during that base destructor; neither
    // protects a callback which reads our already-destroyed group tables.
    qApp->removeEventFilter(this);
    for (auto* child : findChildren<QObject*>()) {
        child->removeEventFilter(this);
        QObject::disconnect(child, nullptr, this, nullptr);
        if (auto* group = dynamic_cast<GroupTabs*>(child)) {
            group->surfaceInvalidated = {};
            group->hoverGeometryChanged = {};
        }
    }
}
void DockSurface::rebuild(const QHash<TabId, QPointer<TabContent>>& contents)
{
    const QPointer<DockSurface> self(this);
    const bool animate = root_ && isVisible() && !manager_->d->drag->hasStableLayout() && TabBar::animationsAllowed();
    QHash<GroupId, TabBar::VisualState> previousVisuals;
    QSet<GroupId> peekedGroups;
    for (auto it = groups_.cbegin(); it != groups_.cend(); ++it)
        if (it.value() && it.value()->tabBar()->peeking()) peekedGroups.insert(it.key());
    QSet<GroupId> previousGroupsIds;
    if (animate) for (auto it = groups_.cbegin(); it != groups_.cend(); ++it) {
        previousGroupsIds.insert(it.key());
        const auto* record = manager_->model().group(it.key());
        const auto built = builtGroups_.constFind(it.key());
        if (it.value() && (!record || built == builtGroups_.cend() || built->tabs != record->tabs || built->stickyTabs != record->stickyTabs))
            previousVisuals.insert(it.key(), it.value()->tabBar()->captureVisualState());
    }
    const auto presentChanges = [&] {
        if (!self) return;
        for (const auto id : peekedGroups) if (const auto view = groups_.value(id)) view->restoreColumnPeek();
        if (!animate) return;
        for (auto it = previousVisuals.begin(); it != previousVisuals.end(); ++it) {
            const auto view = groups_.value(it.key());
            if (!view) { new RetiringLabels(std::move(it.value()), manager_->theme(), this); continue; }
            const QPointer<TabBar> bar(view->tabBar());
            TabBar::VisualState departures;
            for (auto label = it.value().begin(); label != it.value().end(); ++label) {
                const int index = bar->indexOf(label.key());
                if (label->retired && (index < 0 || label->sticky != (index < bar->stickyCount()))) {
                    departures.insert(label.key(), *label);
                    label->retired.reset();
                }
            }
            // A closing last row can already be outside the new bar's bounds.
            // Paint departures in the surface, independently of its new size.
            if (!departures.isEmpty()) new RetiringLabels(std::move(departures), manager_->theme(), this);
            QTimer::singleShot(0, bar, [bar, state = std::move(it.value())]() mutable { if (bar) bar->transitionFrom(std::move(state)); });
        }
        for (auto it = groups_.cbegin(); it != groups_.cend(); ++it) if (it.value() && !previousGroupsIds.contains(it.key())) {
            const QPointer<TabBar> bar(it.value()->tabBar());
            QTimer::singleShot(0, bar, [bar] { if (bar) bar->transitionFrom({}); });
        }
    };
    ++rebuildGeneration_;
    dragTitleLayouts_.clear();
    restoreFocusScope();
    closePeek();
    if (updateInPlace(contents)) { presentChanges(); return; }
    QHash<GroupId, GroupTabs::SurfaceState> previousSurfaces;
    for (auto it = groups_.cbegin(); it != groups_.cend(); ++it)
        if (it.value()) previousSurfaces.insert(it.key(), it.value()->captureSurfaceState());
    const auto previousGroups = std::exchange(groups_, {});
    const auto previousSignatures = std::exchange(builtGroups_, {});
    edgeBars_.clear(); pinnedRegions_.clear(); splitters_.clear(); builtRatios_.clear(); contents_ = contents;
    const QPointer<QWidget> previousRoot = root_;
    if (previousRoot) {
        layout()->removeWidget(previousRoot);
        previousRoot->hide();
    }
    // The manager parks every view that changes window or is deleted, so the
    // old groups hold only pages of tabs that stay here. They move directly
    // into the new groups instead of through the manager's storage.
    QVector<QPointer<QWidget>> carried;
    const auto carryPages = [&carried](GroupTabs* group) {
        QVector<TabId> ids;
        for (int index = 0; index < group->tabBar()->count(); ++index) ids << group->tabBar()->tabData(index).toUuid();
        for (const auto id : ids) if (auto* page = group->takePage(id)) carried << page;
    };
    // A drag keeps transient state on the strips (dragged label, insertion
    // preview, compact presentation); rebuild them while it lasts.
    const bool dragging = manager_->d->drag->isActive();
    // Inside a splitter geometry pass (or a guarded geometry update) the old
    // splitters may still be iterating their children.
    const bool layingOut = SplitterLayoutPass::active() || manager_->d->geometryUpdateDepth > 0;
    reusableGroups_.clear();
    for (auto it = previousGroups.cbegin(); it != previousGroups.cend(); ++it) {
        auto* group = it.value().data();
        if (!group) continue;
        const auto* record = manager_->model().group(it.key());
        const auto signature = previousSignatures.constFind(it.key());
        if (!dragging && record && record->windowId == window_ && record->location != GroupLocation::AutoHide
            && signature != previousSignatures.cend() && *signature == signatureOf(*record)
            && holdsPages(*group, *record, contents)) {
            if (layingOut) detachFromSplitter(group);
            reusableGroups_.insert(it.key(), group);
            builtGroups_.insert(it.key(), *signature);
            continue;
        }
        carryPages(group);
    }
    root_ = new QWidget(this);
    root_->setObjectName("DockSurfaceRoot");
    auto* outer = new QGridLayout(root_);
    outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(0);
    outer->setRowStretch(2, 1); outer->setColumnStretch(2, 1);
    for (const auto edge : {Edge::Top, Edge::Left, Edge::Right, Edge::Bottom}) buildEdge(edge, contents);
    updateRailLayout();
    auto* vertical = new GroupSplitter(Qt::Vertical, root_);
    vertical_ = vertical;
    configureGroupSplitter(vertical, manager_->theme());
    // With each group's own margin, cards stay 6 from the window's edges.
    vertical->setContentsMargins(surfaceOuterMargin, surfaceOuterMargin, surfaceOuterMargin, surfaceOuterMargin);
    if (auto* top = buildPinned(Edge::Top, contents)) vertical->addWidget(top);
    auto* horizontal = new GroupSplitter(Qt::Horizontal, vertical);
    horizontal_ = horizontal;
    configureGroupSplitter(horizontal, manager_->theme());
    if (auto* left = buildPinned(Edge::Left, contents)) horizontal->addWidget(left);
    const auto* window = manager_->model().window(window_);
    center_ = buildTree(window ? window->central : LayoutTree{}, contents);
    if (!center_) {
        auto* empty = new QLabel(QCoreApplication::translate("WindowSystem", "This workspace has no tabs yet\n\nCtrl+Shift+P opens the command palette"), horizontal);
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
    outer->addWidget(vertical, 1, 1, 3, 3);
    // Every reusable group has a leaf in the new trees; release any other.
    for (auto it = reusableGroups_.cbegin(); it != reusableGroups_.cend(); ++it) {
        builtGroups_.remove(it.key());
        if (!it.value()) continue;
        carryPages(it.value());
        it.value()->deleteLater();
    }
    reusableGroups_.clear();
    // A carried page without a group here belongs to an auto-hide tab. Park
    // it as the manager would: deleting the old root or a released strip
    // must never delete a live view.
    for (const auto& page : carried)
        if (page && !root_->isAncestorOf(page)) {
            page->setParent(manager_->d->storage);
            if (page) page->hide();
        }
    for (auto it = groups_.cbegin(); it != groups_.cend(); ++it)
        if (it.value() && previousGroups.value(it.key()) != it.value())
            if (const auto previous = previousSurfaces.constFind(it.key()); previous != previousSurfaces.cend())
                it.value()->inheritSurface(*previous, isVisible() && TabBar::animationsAllowed());
    if (previousRoot) previousRoot->deleteLater();
    builtStructure_ = structureSignature();
    builtThemeRevision_ = manager_->d->themeRevision;
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
    restoreStripScrollOffsets();
    presentChanges();
}
void DockSurface::configureEdgeButton(EdgeTabButton& button, TabId tab, int ordinal, const TabAppearance& appearance) const
{
    const auto* record = manager_->model().tab(tab);
    button.setPreset(manager_->tabColorPreset(record ? record->colorId : QString()));
    button.setTitleClipping(appearance.clipSidebarTitles, appearance.maximumSidebarTitleWidth);
    button.setPresentation(appearance.showSidebarIcons && manager_->tabOptions(tab).showIcon
        ? iconFor("window", manager_->theme()) : QIcon{}, appearance.showSidebarNumbers ? ordinal : 0);
}
QVector<QPair<GroupId, TabId>> DockSurface::edgeTabs(Edge edge) const
{
    QVector<QPair<GroupId, TabId>> result;
    const auto* window = manager_->model().window(window_);
    if (!window) return result;
    for (const auto id : window->edgeGroups)
        if (const auto* group = manager_->model().group(id); group && group->edge == edge && group->location == GroupLocation::AutoHide)
            for (const auto tab : group->tabs) result.append({id, tab});
    return result;
}
bool DockSurface::updateTabAppearance()
{
    // An appearance change only relabels what is built: strips keep their
    // pages and rails keep their buttons. Unlike a refresh, the cost does not
    // grow with the views, pages or rail rebuilds of every tab, so a slider
    // dragged through many values stays responsive.
    if (!root_ || manager_->d->drag->isActive() || builtThemeRevision_ != manager_->d->themeRevision
        || builtStructure_ != structureSignature()) return false;
    const auto& appearance = manager_->d->appearance;
    for (auto it = groups_.cbegin(); it != groups_.cend(); ++it) {
        const auto* record = manager_->model().group(it.key());
        const auto built = builtGroups_.constFind(it.key());
        if (!it.value() || !record || built == builtGroups_.cend()) return false;
        auto expected = *built; expected.appearance = appearance;
        if (expected != signatureOf(*record)) return false;
    }
    for (const auto edge : {Edge::Top, Edge::Left, Edge::Right, Edge::Bottom}) {
        const auto tabs = edgeTabs(edge);
        const auto* strip = dynamic_cast<EdgeStrip*>(edgeBars_.value(edge).data());
        if (!strip) { if (tabs.isEmpty()) continue; return false; }
        const auto buttons = strip->buttons();
        if (buttons.size() != tabs.size()) return false;
        for (qsizetype index = 0; index < tabs.size(); ++index)
            if (!dynamic_cast<EdgeTabButton*>(buttons[index]) || buttons[index]->property("tabId").toUuid() != tabs[index].second) return false;
    }
    // GroupTabs and rail buttons never call back into the host here.
    for (auto it = groups_.cbegin(); it != groups_.cend(); ++it) {
        const auto* record = manager_->model().group(it.key());
        it.value()->setAppearance(appearance, record->location == GroupLocation::Pinned);
        builtGroups_[it.key()].appearance = appearance;
    }
    for (const auto edge : {Edge::Top, Edge::Left, Edge::Right, Edge::Bottom}) {
        auto* strip = dynamic_cast<EdgeStrip*>(edgeBars_.value(edge).data());
        if (!strip) continue;
        const auto anchor = strip->scrollAnchor();
        const auto tabs = edgeTabs(edge);
        const auto buttons = strip->buttons();
        for (qsizetype index = 0; index < tabs.size(); ++index)
            configureEdgeButton(*static_cast<EdgeTabButton*>(buttons[index]), tabs[index].second, int(index + 1), appearance);
        strip->restoreScrollAnchor(anchor);
    }
    edgeAppearance_ = appearance;
    placePeek();
    return true;
}
DockSurface::StructureSignature DockSurface::structureSignature() const
{
    StructureSignature result;
    const auto* window = manager_->model().window(window_);
    if (!window) return result;
    const std::function<void(const LayoutTree&)> visit = [&](const LayoutTree& node) {
        if (!node) { result.append({QUuid{}, -2}); return; }
        if (node->isLeaf()) { result.append({node->itemId, -1}); return; }
        result.append({node->id, int(node->orientation)});
        visit(node->first);
        visit(node->second);
    };
    visit(window->central);
    for (const auto edge : {Edge::Top, Edge::Left, Edge::Right, Edge::Bottom})
        visit(manager_->model().pinnedLayout(window_, edge));
    return result;
}
bool DockSurface::updateInPlace(const QHash<TabId, QPointer<TabContent>>& contents)
{
    // Reparenting a strip makes Qt resolve style sheet, font and palette again
    // for its whole subtree, all tab pages included. While the splitter
    // structure is unchanged, keep the root, the splitters and every unchanged
    // strip where they are, and swap only the changed strips in place.
    if (!root_ || !vertical_ || !horizontal_ || !center_ || manager_->d->drag->isActive()
        || builtThemeRevision_ != manager_->d->themeRevision || builtStructure_ != structureSignature()) return false;
    const QPointer<DockSurface> retained(this);
    QVector<QPair<GroupId, QPointer<GroupTabs>>> changed;
    const auto currentGroups = groups_;
    for (auto it = currentGroups.cbegin(); it != currentGroups.cend(); ++it) {
        const auto* record = manager_->model().group(it.key());
        auto* splitter = it.value() ? qobject_cast<QSplitter*>(it.value()->parentWidget()) : nullptr;
        if (!record || !splitter || splitter->indexOf(it.value()) < 0) return false;
        const auto signature = builtGroups_.constFind(it.key());
        const auto next = signatureOf(*record);
        const bool mounted = holdsPages(*it.value(), *record, contents);
        if (signature != builtGroups_.cend() && mounted && *signature != next) {
            auto presentationOnly = *signature;
            presentationOnly.appearance = next.appearance;
            if (presentationOnly == next) {
                // Updating appearance must not unmount the live slider or
                // editor that is currently changing that appearance.
                const auto state = manager_->model().stateId();
                const auto generation = rebuildGeneration_;
                it.value()->setAppearance(next.appearance, next.location == GroupLocation::Pinned);
                if (!retained || !manager_) return true;
                if (rebuildGeneration_ != generation) return true;
                if (manager_->model().stateId() != state || manager_->tabAppearance() != next.appearance
                    || manager_->d->themeRevision != next.themeRevision
                    || !it.value() || groups_.value(it.key()) != it.value())
                    return false;
                builtGroups_.insert(it.key(), next);
                continue;
            }
        }
        if (signature == builtGroups_.cend() || *signature != next || !mounted) changed.append({it.key(), it.value()});
    }
    contents_ = contents;
    for (const auto& bar : std::as_const(edgeBars_))
        if (bar) { root_->layout()->removeWidget(bar); bar->hide(); bar->deleteLater(); }
    edgeBars_.clear();
    for (const auto edge : {Edge::Top, Edge::Left, Edge::Right, Edge::Bottom}) buildEdge(edge, contents);
    updateRailLayout();
    QVector<QPointer<QWidget>> carried;
    for (const auto& [id, old] : changed) {
        if (!old) continue;
        QVector<TabId> ids;
        for (int index = 0; index < old->tabBar()->count(); ++index) ids << old->tabBar()->tabData(index).toUuid();
        for (const auto tab : ids) if (auto* page = old->takePage(tab)) carried << page;
        auto* fresh = buildGroup(id, contents);
        auto* splitter = qobject_cast<QSplitter*>(old->parentWidget());
        const int index = splitter ? splitter->indexOf(old) : -1;
        if (!fresh || index < 0) continue;
        {
            // replaceWidget() keeps the splitter's item list intact, so it is
            // safe even while that splitter is positioning its children.
            const SplitterLayoutPass pass;
            splitter->replaceWidget(index, fresh);
        }
        if (center_ == old) center_ = fresh;
        fresh->tabBar()->inheritHover(*old->tabBar());
        old->deleteLater();
    }
    for (auto it = groups_.cbegin(); it != groups_.cend(); ++it) {
        const auto* record = manager_->model().group(it.key());
        if (it.value() && record) {
            it.value()->setTabBarVisible(record->location != GroupLocation::Central
                || centralTabBarVisible(manager_->tabBarsVisible(window_)));
            it.value()->setVerticalState(record->tabStripWidth, record->tabStripIconsOnly);
            it.value()->setGroupName(record->name);
            it.value()->tabBar()->setFolders(stripFolders(manager_->model(), *record));
        }
    }
    // A page that no group adopted belongs to an auto-hide tab; never leave a
    // live view inside a discarded strip.
    for (const auto& page : carried)
        if (page && !root_->isAncestorOf(page)) {
            page->setParent(manager_->d->storage);
            if (page) page->hide();
        }
    const auto* window = manager_->model().window(window_);
    applyChangedRatios(window ? window->central : LayoutTree{});
    for (const auto edge : {Edge::Top, Edge::Left, Edge::Right, Edge::Bottom}) {
        const auto tree = manager_->model().pinnedLayout(window_, edge);
        applyChangedRatios(tree);
        if (const auto region = pinnedRegions_.value(edge))
            region->setProperty("preferredExtent", pinnedExtent(tree, manager_->model(), region->orientation(), region->handleWidth()));
    }
    root_->layout()->activate();
    restorePinnedExtents();
    QTimer::singleShot(0, this, [this] { restorePinnedExtents(); });
    applyFocusScope();
    restoreStripScrollOffsets();
    return true;
}
void DockSurface::applyChangedRatios(LayoutTree node)
{
    if (!node || node->isLeaf()) return;
    const auto built = builtRatios_.constFind(node->id);
    if (built == builtRatios_.cend() || *built != node->ratio) {
        if (const auto splitter = splitters_.value(node->id)) {
            const SplitterLayoutPass pass;
            splitter->setSizes({qRound(1000 * node->ratio), qRound(1000 * (1 - node->ratio))});
        }
        builtRatios_.insert(node->id, node->ratio);
    }
    applyChangedRatios(node->first);
    applyChangedRatios(node->second);
}
void DockSurface::captureStripScrollOffsets()
{
    // Capture before content reparenting changes the old viewport sizes.
    // These caches contain only the currently mounted strips, not tab content
    // or an unbounded history of removed groups.
    groupViewportStates_.clear(); edgeScrollAnchors_.clear(); edgeAnchorAppearance_ = edgeAppearance_;
    for (auto it = groups_.cbegin(); it != groups_.cend(); ++it)
        if (it.value()) groupViewportStates_.insert(it.key(), it.value()->captureViewportState());
    for (auto it = edgeBars_.cbegin(); it != edgeBars_.cend(); ++it)
        if (auto* strip = dynamic_cast<EdgeStrip*>(it.value().data())) edgeScrollAnchors_.insert(it.key(), strip->scrollAnchor());
}
void DockSurface::restoreStripScrollOffsets()
{
    for (auto it = groups_.cbegin(); it != groups_.cend(); ++it)
        if (it.value() && groupViewportStates_.contains(it.key())) it.value()->restoreViewportState(groupViewportStates_.value(it.key()));
    for (auto it = edgeBars_.cbegin(); it != edgeBars_.cend(); ++it)
        if (auto* strip = dynamic_cast<EdgeStrip*>(it.value().data()); strip && edgeScrollAnchors_.contains(it.key())) {
            // Structural changes keep the offset; changed label lengths keep the anchored tab.
            const auto anchor = edgeScrollAnchors_.value(it.key());
            if (manager_->tabAppearance() == edgeAnchorAppearance_) strip->restoreScrollOffset(anchor.offset);
            else strip->restoreScrollAnchor(anchor);
        }
    edgeAppearance_ = manager_->tabAppearance();
}
void DockSurface::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    paintBackplate(painter, event->rect());
}
void DockSurface::paintBackplate(QPainter& painter, const QRect& exposed)
{
    if (!manager_ || compactPresentation_) return;
    painter.save();
    painter.setClipRect(exposed, Qt::IntersectClip);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(exposed, manager_->theme().headerBackground);
    for (const auto& group : std::as_const(groups_))
        if (group && group->isVisibleTo(this))
            group->paintSurface(painter, elevation_, group->mapTo(this, QPoint()), exposed);
    painter.restore();
}
void DockSurface::invalidateGroupSurface(GroupId id)
{
    const auto group = groups_.value(id);
    const auto previous = surfaceBounds_.value(id);
    const auto current = group && group->isVisibleTo(this)
        ? group->surfacePaintRegion().translated(group->mapTo(this, QPoint())) : QRegion{};
    if (group) surfaceBounds_.insert(id, current); else surfaceBounds_.remove(id);
    const auto dirty = previous | current;
    if (dirty.isEmpty()) return;
    update(dirty);
    for (const auto& item : std::as_const(groups_)) if (item && item->isVisibleTo(this))
        item->invalidateSurfaceCorners(dirty.boundingRect().translated(-item->mapTo(this, QPoint())));
}
void DockSurface::refreshGroupFocus()
{
    for (const auto& group : groups_) if (group) group->refreshFocus();
}
bool DockSurface::scrollStripAt(QPoint global, QPoint delta, bool smooth)
{
    const auto* hit = childAt(mapFromGlobal(global));
    for (const auto& group : groups_) if (group && group->isVisible() && hit && group->isAncestorOf(hit)
        && group->scrollStripAt(global, delta, smooth)) return true;
    for (const auto& widget : edgeBars_)
        if (auto* strip = dynamic_cast<EdgeStrip*>(widget.data()); strip && strip->isVisible() && hit && strip->isAncestorOf(hit)
            && strip->viewportRectGlobal().contains(global)) {
            strip->scrollByWheel(delta, smooth); return true;
        }
    return false;
}
QWidget* DockSurface::buildTree(LayoutTree node, const QHash<TabId, QPointer<TabContent>>& contents)
{
    if (!node) return nullptr;
    if (node->isLeaf()) {
        if (const auto reused = reusableGroups_.take(node->itemId)) {
            // Callbacks capture this surface, the group ID and the strip's
            // own bar, all unchanged. Only the strip's visibility depends on
            // window-wide state (single-tab hiding, bar preferences).
            const auto* group = manager_->model().group(node->itemId);
            reused->setTabBarVisible(group->location != GroupLocation::Central
                || centralTabBarVisible(manager_->tabBarsVisible(window_)));
            groups_.insert(node->itemId, reused);
            return reused.data();
        }
        return buildGroup(node->itemId, contents);
    }
    auto* split = new GroupSplitter(node->orientation, this);
    split->setObjectName("GroupSplitter"); configureGroupSplitter(split, manager_->theme());
    split->addWidget(buildTree(node->first, contents));
    split->addWidget(buildTree(node->second, contents));
    split->setSizes({qRound(1000 * node->ratio), qRound(1000 * (1 - node->ratio))});
    split->setProperty("splitNodeId", node->id);
    splitters_.insert(node->id, split);
    builtRatios_.insert(node->id, node->ratio);
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
    tabs->setAppearance(manager_->tabAppearance(), group->location == GroupLocation::Pinned);
    tabs->setTabBarVisible(group->location != GroupLocation::Central || centralTabBarVisible(manager_->tabBarsVisible(window_)));
    tabs->pageRequested = [this](TabId tab) -> QWidget* {
        if (!manager_) return nullptr;
        const QPointer<TabContent> content(manager_->ensureTabView(tab));
        if (!manager_ || !content) return nullptr;
        contents_.insert(tab, content);
        return content.data();
    };
    auto* bar = tabs->tabBar();
    for (const auto tabId : group->tabs) {
        const auto* record = manager_->model().tab(tabId);
        tabs->addTab(contents.value(tabId), manager_->tabOptions(tabId).showIcon ? iconFor("window", manager_->theme()) : QIcon{},
                     record->title, tabId);
        tabs->setTabMetadata(tabs->tabBar()->count() - 1, *record);
    }
    for (int i = 0; i < bar->count(); ++i)
        if (bar->tabData(i).toUuid() == group->activeTab) tabs->setCurrentIndex(i, !groupViewportStates_.contains(id));
    tabs->setStickyCount(group->stickyTabs);
    bar->setFolders(stripFolders(manager_->model(), *group));
    // Folder headers: collapse, menu, a new tab inside, and carrying it.
    const auto folderId = [bar](int folder) { return folder >= 0 && folder < bar->folders().size() ? bar->folders().at(folder).id : QUuid{}; };
    bar->toggleFolder = [manager = QPointer<WindowManager>(manager_), folderId](int folder) {
        const auto* record = manager ? manager->model().folder(folderId(folder)) : nullptr;
        if (!record) return;
        auto changed = *record; changed.collapsed = !changed.collapsed;
        manager->updateTabFolder(changed);
    };
    bar->folderMenu = [manager = QPointer<WindowManager>(manager_), folderId](int folder, QPoint point) {
        if (manager) manager->showTabFolderMenu(folderId(folder), point);
    };
    bar->newTabInFolder = [manager = QPointer<WindowManager>(manager_), window = window_, id, folderId](int folder) {
        if (manager) manager->requestNewTab(window, id, {}, folderId(folder));
    };
    bar->folderDrag = [manager = QPointer<WindowManager>(manager_), folderId](int folder, QPoint point) {
        if (manager) manager->beginTabFolderDrag(folderId(folder), point);
    };
    tabs->unstickRequested = [this, bar](int index) {
        if (manager_ && index >= 0 && index < bar->count()) manager_->setTabSticky(bar->tabData(index).toUuid(), false);
    };
    groups_.insert(id, tabs);
    builtGroups_.insert(id, signatureOf(*group));
    tabs->newTabRequested = [manager = QPointer<WindowManager>(manager_), window = window_, id] {
        if (manager) manager->requestNewTab(window, id);
    };
    tabs->newTabMenuRequested = [manager = QPointer<WindowManager>(manager_), window = window_, id](QWidget* parent) {
        return manager ? manager->createTabCreationMenu(window, id, parent) : nullptr;
    };
    // A strip can outlive this surface until its deferred deletion; its
    // callbacks then do nothing.
    const QPointer<DockSurface> surface(this);
    tabs->surfaceInvalidated = [surface, id] { if (surface) surface->invalidateGroupSurface(id); };
    QObject::connect(tabs, &QObject::destroyed, this, [surface, tabs, id] {
        if (surface && (!surface->groups_.value(id) || surface->groups_.value(id).data() == tabs))
            surface->invalidateGroupSurface(id);
    });
    tabs->hoverGeometryChanged = [surface] { if (surface && surface->manager_) surface->manager_->d->drag->refreshHover(); };
    tabs->isActiveGroup = [this, surface, id] {
        const auto* window = surface && manager_ ? manager_->model().window(window_) : nullptr;
        return window && window->activeGroup == id;
    };
    tabs->refreshFocus();
    tabs->setVerticalState(group->tabStripWidth, group->tabStripIconsOnly);
    tabs->setGroupName(group->name);
    tabs->verticalStateChanged = [manager = QPointer<WindowManager>(manager_), id](int width, bool iconsOnly) {
        if (manager && manager->model().group(id)) manager->setGroupTabStrip(id, width, iconsOnly);
    };
    if (group->location == GroupLocation::Pinned) {
        tabs->setPinnedActions([this, id] {
            if (!manager_) return;
            const auto* current = manager_->model().group(id);
            if (current) manager_->pinTab(current->activeTab, false);
        }, [this, edge = group->edge] { if (manager_) manager_->pinEdge(window_, edge, false); });
    }
    bar->drag = [this, bar](int index, QPoint point) {
        if (manager_) manager_->beginTabDrag(bar->tabData(index).toUuid(), point);
    };
    tabs->groupMenuRequested = [manager = QPointer<WindowManager>(manager_),id](QPoint point) {
        if(manager) manager->showGroupMenu(id,point);
    };
    bar->menu = [this, bar, id](int index, QPoint point) {
        if (manager_ && index >= 0) manager_->showTabMenu(bar->tabData(index).toUuid(), point);
        else if(manager_) manager_->showGroupMenu(id,point);
    };
    tabs->currentChanged = [this, surface, tabs, bar, id](int index) {
        if (!surface || !manager_) return;
        const QPointer<GroupTabs> retained(tabs);
        if (index >= 0) manager_->updateActive(window_, id, bar->tabData(index).toUuid());
        if (retained) retained->update();
    };
    tabs->closeRequested = [this, bar](int index) {
        if (manager_) manager_->closeTab(bar->tabData(index).toUuid());
    };
    return tabs;
}
DockSurface::GroupSignature DockSurface::signatureOf(const GroupRecord& group) const
{
    GroupSignature result;
    result.tabs = group.tabs;
    result.stickyTabs = group.stickyTabs;
    result.activeTab = group.activeTab;
    result.location = group.location;
    result.edge = group.edge;
    result.appearance = manager_->d->appearance;
    result.themeRevision = manager_->d->themeRevision;
    const auto& descriptors = manager_->d->descriptors;
    for (const auto id : group.tabs) {
        const auto* tab = manager_->model().tab(id);
        result.titles << (tab ? tab->title : QString());
        result.colorIds << (tab ? tab->colorId : QString());
        const auto descriptor = tab ? descriptors.constFind(tab->activePane) : descriptors.cend();
        result.icons << (descriptor != descriptors.cend() ? descriptor->showIcon : TabOptions{}.showIcon);
    }
    return result;
}
bool DockSurface::holdsPages(const GroupTabs& widget, const GroupRecord& group,
                             const QHash<TabId, QPointer<TabContent>>& contents) const
{
    if (widget.count() != group.tabs.size() || widget.page(int(group.tabs.size()))) return false;
    for (int index = 0; index < group.tabs.size(); ++index) {
        const auto id = group.tabs.at(index);
        const auto content = contents.value(id);
        if (widget.tabBar()->tabData(index).toUuid() != id) return false;
        if (!content) {
            if (!widget.pageIsPlaceholder(id)) return false;
        } else if (widget.pageIsPlaceholder(id) || widget.page(index) != content.data()) return false;
    }
    return true;
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
    bar->hoverGeometryChanged = [this] {
        const QPointer<DockSurface> self(this);
        placePeek();
        if (self && manager_) manager_->d->drag->refreshHover();
    };
    bar->setVisible(manager_->sideBarsVisible(window_));
    bar->setProperty("edge", int(edge)); edgeBars_.insert(edge, bar);
    int ordinal = 0;
    const auto& appearance = manager_->d->appearance;
    for (const auto groupId : ids) {
        for (const auto tabId : manager_->model().group(groupId)->tabs) {
            ++ordinal;
            auto* button = new EdgeTabButton(manager_->model().tab(tabId)->title, vertical,
                manager_->theme().metrics.edgeThickness, bar);
            configureEdgeButton(*button, tabId, ordinal, appearance);
            button->setCheckable(true);
            button->setChecked(manager_->context(window_).tab == tabId);
            button->setProperty("tabId", tabId);
            button->drag = [this, tabId](QPoint point) { if (manager_) manager_->beginTabDrag(tabId, point); };
            button->menu = [this, tabId](QPoint point) { if (manager_) manager_->showTabMenu(tabId, point); };
            button->close = [this, tabId] { if (manager_) manager_->closeTab(tabId); };
            QObject::connect(button, &QAbstractButton::clicked, manager_, [this, groupId, tabId] {
                if (peek_ && peekTab_ == tabId) closePeek();
                else showEdgeGroup(groupId, tabId);
            });
            bar->addButton(button);
        }
    }
    auto* pinAll = new CompactToolButton(bar, manager_->theme().metrics.edgeThickness); pinAll->setObjectName("PinEdgeButton");
    pinAll->setIcon(iconFor("pin-all", manager_->theme())); pinAll->setToolTip(QCoreApplication::translate("WindowSystem", "Pin all sidebar tabs"));
    pinAll->setAccessibleName(pinAll->toolTip());
    QObject::connect(pinAll, &QToolButton::clicked, manager_, [this, edge] { manager_->pinEdge(window_, edge, true); });
    bar->setTailWidget(pinAll);
    return bar;
}
void DockSurface::updateRailLayout()
{
    auto* outer = root_ ? qobject_cast<QGridLayout*>(root_->layout()) : nullptr;
    if (!outer) return;
    const auto visible = [this](Edge edge) {
        const auto bar = edgeBars_.value(edge);
        return bar && !bar->isHidden();
    };
    const bool left = visible(Edge::Left), right = visible(Edge::Right);
    const bool top = visible(Edge::Top), bottom = visible(Edge::Bottom);
    // Only perpendicular rails need separation. The workspace spans these
    // tracks so its edges remain flush with each rail, without a surrounding
    // gutter. Missing neighbours leave no gap at the outer corners.
    outer->setColumnMinimumWidth(1, left && top ? 2 : 0);
    outer->setColumnMinimumWidth(3, right && top ? 2 : 0);
    outer->setRowMinimumHeight(1, 0);
    outer->setRowMinimumHeight(3, bottom && (left || right) ? 2 : 0);
    for (const auto& bar : edgeBars_) if (bar) outer->removeWidget(bar);
    // Bottom owns both lower corners; vertical rails own the upper corners.
    // A missing or hidden neighbour gives its corner to the remaining rail.
    if (left) outer->addWidget(edgeBars_.value(Edge::Left), 0, 0, bottom ? 3 : 5, 1);
    if (right) outer->addWidget(edgeBars_.value(Edge::Right), 0, 4, bottom ? 3 : 5, 1);
    if (top) {
        const int first = left ? 2 : 0, last = right ? 2 : 4;
        outer->addWidget(edgeBars_.value(Edge::Top), 0, first, 1, last - first + 1);
    }
    if (bottom) outer->addWidget(edgeBars_.value(Edge::Bottom), 4, 0, 1, 5);
    // Adding a rail that was parked on the surface reparents it, and Qt defers
    // the implicit show to the next event-loop turn. Peek placement and pinned
    // extents are computed synchronously after this, so show requested rails now.
    for (const auto& [edge, shown] : {std::pair{Edge::Left, left}, std::pair{Edge::Right, right},
                                      std::pair{Edge::Top, top}, std::pair{Edge::Bottom, bottom}})
        if (shown) edgeBars_.value(edge)->show();
}
QWidget* DockSurface::buildPinned(Edge edge, const QHash<TabId, QPointer<TabContent>>& contents)
{
    const auto tree = manager_->model().pinnedLayout(window_, edge);
    if (!tree) return nullptr;
    const auto orientation = edge == Edge::Left || edge == Edge::Right ? Qt::Horizontal : Qt::Vertical;
    auto* body = buildTree(tree, contents);
    auto* region = !tree->isLeaf() && tree->orientation == orientation ? qobject_cast<QSplitter*>(body) : nullptr;
    if (!region) {
        region = new GroupSplitter(orientation, this);
        region->addWidget(body);
    }
    region->setObjectName("PinnedRegion");
    pinnedRegions_.insert(edge, region);
    configureGroupSplitter(region, manager_->theme());
    region->setProperty("preferredExtent", pinnedExtent(tree, manager_->model(), orientation, region->handleWidth()));
    QObject::connect(region, &QSplitter::splitterMoved, this, [this] { savePinnedExtents(); });
    return region;
}
void DockSurface::savePinnedExtents()
{
    if (!manager_) return;
    if (hasContentFocusScope()) return;
    const QPointer<DockSurface> self(this);
    const QPointer<WindowManager> owner(manager_);
    const auto generation = rebuildGeneration_;
    QVector<QPair<GroupId, int>> updates;
    for (auto iterator = groups_.cbegin(); iterator != groups_.cend(); ++iterator) {
        const auto* record = manager_->model().group(iterator.key());
        if (!record || record->location != GroupLocation::Pinned || !iterator.value()) continue;
        const auto extent = record->edge == Edge::Left || record->edge == Edge::Right
            ? iterator.value()->width() : iterator.value()->height();
        updates.append({iterator.key(), extent});
    }
    // Host notifications may close this window or rebuild the same surface.
    // Neither member iterators nor geometry from the old view survive that.
    const auto regions = pinnedRegions_.values();
    const auto window = window_;
    for (const auto& [group, extent] : updates) {
        owner->execute("layout.setEdgeExtent", {{"group", group}, {"extent", extent}}, window);
        if (!self || !owner || self->rebuildGeneration_ != generation || !owner->model().window(window)) return;
    }
    for (const auto& region : regions) {
        if (region) region->setProperty("preferredExtent", region->orientation() == Qt::Horizontal ? region->width() : region->height());
        if (!self || !owner || self->rebuildGeneration_ != generation) return;
    }
}
void DockSurface::restorePinnedExtents()
{
    if (!manager_) return;
    if (hasContentFocusScope()) return;
    if (!horizontal_ || !vertical_) return;
    const QPointer<DockSurface> self(this);
    const QPointer<WindowManager> owner(manager_);
    const auto generation = rebuildGeneration_;
    const auto window = window_;
    const QPointer<QSplitter> horizontal(horizontal_), vertical(vertical_);
    const QPointer<QWidget> center(center_);
    // setSizes synchronously resizes host content. A callback may destroy its
    // manager, but Qt must finish the active splitter layout before releasing
    // the UI. Rebuilding also invalidates all geometry from this generation.
    ++owner->d->geometryUpdateDepth;
    const auto releaseGeometry = qScopeGuard([owner] { if (owner) --owner->d->geometryUpdateDepth; });
    const auto current = [&] {
        return self && owner && self->rebuildGeneration_ == generation
            && owner->model().window(window) && horizontal && vertical && center
            && self->horizontal_ == horizontal && self->vertical_ == vertical && self->center_ == center;
    };
    const auto requested = [](QWidget* widget) { return widget->property("preferredExtent").toInt(); };
    const auto restoreOuter = [&](QPointer<QSplitter> splitter, QPointer<QWidget> flexible) {
        if (!current() || !splitter || !flexible) return false;
        QList<int> sizes;
        int fixed = 0;
        for (int i = 0; i < splitter->count(); ++i)
            if (splitter->widget(i) != flexible) fixed += requested(splitter->widget(i));
        const auto available = splitter->orientation() == Qt::Horizontal ? splitter->width() : splitter->height();
        for (int i = 0; i < splitter->count(); ++i)
            sizes << (splitter->widget(i) == flexible
                ? qMax(1, available - fixed - (splitter->count() - 1) * splitter->handleWidth())
                : requested(splitter->widget(i)));
        {
            const SplitterLayoutPass pass;
            splitter->setSizes(sizes);
        }
        return current() && splitter && flexible;
    };
    if (!restoreOuter(vertical, horizontal)) return;
    if (!restoreOuter(horizontal, center)) return;
    // Nested pinned splitters retain their own ratios. Resizing the outer
    // region must not reset an independent inner boundary to leaf extents.
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
QRect DockSurface::groupContentRect(GroupId group) const
{
    const auto view = groups_.value(group);
    return view ? view->contentRectGlobal() : QRect{};
}
QHash<GroupId, QRect> DockSurface::tabBarRects() const
{
    QHash<GroupId, QRect> result;
    for (auto it = groups_.cbegin(); it != groups_.cend(); ++it)
        if (it.value() && it.value()->isVisible() && !it.value()->viewportRectGlobal().isEmpty())
            result.insert(it.key(), it.value()->viewportRectGlobal());
    return result;
}
QVector<DockStrip> DockSurface::dockStrips() const
{
    QVector<DockStrip> result;
    for (const auto& view : groups_) {
        if (!view || !view->isVisible()) continue;
        const auto rect = view->stripPanelRectGlobal();
        if (rect.isEmpty()) continue;
        Edge edge = Edge::Top;
        switch (view->tabBar()->placement()) {
        case TabBarPlacement::Top: edge = Edge::Top; break;
        case TabBarPlacement::Bottom: edge = Edge::Bottom; break;
        case TabBarPlacement::Left: edge = Edge::Left; break;
        case TabBarPlacement::Right: edge = Edge::Right; break;
        }
        result.append({rect, edge});
    }
    return result;
}
GroupId DockSurface::tabBarAt(QPoint point) const
{
    const auto rects = tabBarRects();
    for (auto it = rects.cbegin(); it != rects.cend(); ++it) if (it.value().contains(point)) return it.key();
    return {};
}
int DockSurface::tabInsertionIndex(GroupId group, QPoint point, TabLayoutInput::Carried carried) const
{
    const auto widget = groups_.value(group); return widget ? widget->insertionIndex(point, carried) : -1;
}
std::optional<bool> DockSurface::tabInsertionRow(GroupId group, QPoint point) const
{
    const auto widget = groups_.value(group); return widget ? widget->insertionRow(point) : std::nullopt;
}
int DockSurface::dragLabelExtent(TabId tab, bool edge) const
{
    const auto* record = manager_->model().tab(tab);
    if (!record) return 0;
    if (edge) {
        const auto appearance=manager_->tabAppearance();
        const auto* group=manager_->model().group(manager_->model().groupForTab(tab));
        int ordinal = 0;
        const auto* window = manager_->model().window(window_);
        if (window && group) for (const auto id : window->edgeGroups) {
            const auto* candidate = manager_->model().group(id);
            if (!candidate || candidate->location != GroupLocation::AutoHide || candidate->edge != group->edge) continue;
            const int index = candidate->tabs.indexOf(tab);
            if (index >= 0) { ordinal += index + 1; break; }
            ordinal += candidate->tabs.size();
        }
        auto& title = dragTitleLayouts_[tab][1];
        title.prepare(record->title, font(), layoutDirection(), false, this);
        return edgeTabExtent(fontMetrics(),title.naturalWidth(),appearance.showSidebarIcons && manager_->tabOptions(tab).showIcon,
            appearance.showSidebarNumbers ? std::max(1, ordinal) : 0, manager_->theme().metrics.edgeThickness,
            appearance.clipSidebarTitles, appearance.maximumSidebarTitleWidth);
    }
    if (const auto group = groups_.value(manager_->model().groupForTab(tab))) {
        const auto* bar = group->tabBar();
        for (int i = 0; i < bar->count(); ++i) {
            if (bar->tabData(i).toUuid() != tab) continue;
            // A sized sticky tab carries its natural width; the target strip
            // sizes a preview among its own sticky tabs.
            const int width = i < bar->stickyCount() && manager_->tabAppearance().stickySizing != StickyTabSizing::Normal
                ? bar->naturalTabWidth(i) : bar->tabExtent(i, i < bar->stickyCount());
            if (manager_->tabAppearance().multiline) {
                const int viewportWidth = group->viewportRectGlobal().width();
                // Single-tab floating windows can hide their strip. A hidden
                // hit region has zero width, but its carried label still has
                // a real extent before entering the target presentation.
                return std::min(width, std::max(1, viewportWidth > 0 ? viewportWidth : group->width()));
            }
            return width;
        }
    }
    // Not in a strip yet: measure it as an unnumbered tab of the target bar.
    const auto appearance = manager_->tabAppearance();
    auto& layout = dragTitleLayouts_[tab][0];
    layout.prepare(record->title, font(), layoutDirection(), true, this);
    const int natural = layout.naturalWidth();
    return tabBarTabWidth({0, appearance.showIcons && manager_->tabOptions(tab).showIcon ? manager_->theme().metrics.iconSize : 0,
                           appearance.clipTitles ? std::min(natural, appearance.maximumTitleWidth) : natural});
}
QHash<Edge, QRect> DockSurface::edgeBarRects() const
{
    QHash<Edge, QRect> result;
    for (auto it = edgeBars_.cbegin(); it != edgeBars_.cend(); ++it)
        if (it.value() && it.value()->isVisible()) result.insert(it.key(), QRect(it.value()->mapToGlobal(QPoint()), it.value()->size()));
    return result;
}
QPixmap DockSurface::dragLabelPixmap(TabId tab) const
{
    if (const auto group = groups_.value(manager_->model().groupForTab(tab))) {
        auto* bar = group->tabBar();
        for (int i = 0; i < bar->count(); ++i)
            if (bar->tabData(i).toUuid() == tab) {
                const auto area=bar->displayTabRect(i);
                return area.isEmpty() ? QPixmap{} : bar->grab(area);
            }
    }
    for (const auto& strip : edgeBars_) if (strip)
        for (auto* button : strip->findChildren<QAbstractButton*>())
            if (button->property("tabId").toUuid() == tab) return button->grab();
    return {};
}
void DockSurface::setLiftedTab(GroupId group, QPoint global, QPoint grab)
{
    if (const auto widget = groups_.value(group)) widget->setLiftedTab(global, grab);
}
void DockSurface::setContentPreview(GroupId group, const QPixmap& picture)
{
    for (auto it = groups_.cbegin(); it != groups_.cend(); ++it)
        if (it.value()) it.value()->setContentPreview(it.key() == group ? picture : QPixmap{});
}
QPixmap DockSurface::activeContentPicture() const
{
    const auto* window = manager_->model().window(window_);
    const auto group = window ? groups_.value(window->activeGroup) : QPointer<GroupTabs>{};
    auto* page = group ? group->page(group->currentIndex()) : nullptr;
    // Content that another process draws cannot be grabbed; then no preview.
    return page && page->isVisible() ? page->grab() : QPixmap{};
}
void DockSurface::clearLiftedTabs()
{
    for (const auto& group : groups_) if (group) group->clearLiftedTab();
}
QPoint DockSurface::tabGrabOffset(TabId tab, QPoint global) const
{
    if (const auto group = groups_.value(manager_->model().groupForTab(tab))) {
        auto* bar = group->tabBar();
        const int index = bar->indexOf(tab);
        if (index >= 0) return bar->mapFromGlobal(global) - bar->tabRect(index).topLeft();
    }
    return {};
}
void DockSurface::setDraggedTab(TabId tab)
{
    for (const auto& group : groups_) if (group) group->setDraggedTab(tab);
    for (const auto& widget : edgeBars_)
        if (auto* strip = dynamic_cast<EdgeStrip*>(widget.data())) strip->setDraggedTab(tab);
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
void DockSurface::clearDragHover(QPoint point)
{
    const auto groups = groups_.values();
    const QPointer<DockSurface> self(this);
    for (const auto& group : groups) {
        if (group) group->clearDragHover(point);
        if (!self) return;
    }
    for (const auto& widget : edgeBars_)
        if (auto* strip = dynamic_cast<EdgeStrip*>(widget.data())) strip->setDragHover(QPoint(-100000, -100000));
}
void DockSurface::setInsertionPreview(GroupId group, int index, const QVector<CarriedLabel>& carried, bool carriedSticky,
                                      TabLayoutInput::Carried kind)
{
    for (auto it = groups_.cbegin(); it != groups_.cend(); ++it)
        if (it.value()) it.value()->setInsertionPreview(it.key() == group ? index : -1, carried, it.key() == group && carriedSticky, kind);
    for (const auto& widget : edgeBars_) if (auto* strip = dynamic_cast<EdgeStrip*>(widget.data())) strip->setInsertionPreview(-1, 0);
}
void DockSurface::setEdgeInsertionPreview(Edge edge, int index, int extent)
{
    for (const auto& widget : groups_) if (widget) widget->setInsertionPreview(-1, {});
    for (auto it = edgeBars_.cbegin(); it != edgeBars_.cend(); ++it)
        if (auto* strip = dynamic_cast<EdgeStrip*>(it.value().data())) strip->setInsertionPreview(it.key() == edge ? index : -1, extent);
}
void DockSurface::clearInsertionPreviews()
{
    for (const auto& widget : groups_) if (widget) widget->setInsertionPreview(-1, {});
    for (const auto& widget : edgeBars_) if (auto* strip = dynamic_cast<EdgeStrip*>(widget.data())) strip->setInsertionPreview(-1, 0);
}
FolderId DockSurface::insertionFolder(GroupId group) const
{
    const auto widget = groups_.value(group);
    return widget ? widget->insertionFolder() : FolderId{};
}
void DockSurface::setDraggedFolder(FolderId folder)
{
    for (const auto& group : groups_) if (group) group->setDraggedFolder(folder);
}
QPoint DockSurface::folderGrabOffset(FolderId folder, QPoint global) const
{
    const auto widget = groups_.value(manager_->model().groupForFolder(folder));
    return widget ? widget->folderGrabOffset(folder, global) : QPoint{};
}
bool DockSurface::insertionSticky(GroupId group) const
{
    const auto widget = groups_.value(group);
    return widget && widget->insertionSticky();
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
TabDragVisual DockSurface::dragLabelVisual() const
{
    struct Piece { QPointer<TabBar> bar; QPoint offset; };
    QVector<Piece> pieces;
    QRect bounds;
    QPoint anchor;
    for (const auto& group : std::as_const(groups_)) if (group && group->isVisible()) {
        auto* bar = group->tabBar();
        const auto rect = bar->carrierBounds();
        if (!bar->isVisible() || rect.isEmpty()) continue;
        const auto offset = bar->mapTo(this, QPoint());
        if (pieces.isEmpty()) anchor = offset + bar->carrierAnchor();
        bounds |= rect.translated(offset);
        pieces.append({bar, offset});
    }
    if (pieces.isEmpty()) return {};
    for (auto& piece : pieces) piece.offset -= bounds.topLeft();
    TabDragVisual visual;
    visual.size = bounds.size(); visual.anchor = anchor - bounds.topLeft();
    for (const auto& piece : pieces) if (piece.bar && piece.bar->currentIndex() >= 0) {
        visual.active = piece.bar->tabRect(piece.bar->currentIndex()).translated(piece.offset);
        visual.identity = piece.bar->tabData(piece.bar->currentIndex()).toUuid();
        break;
    }
    visual.paint = [pieces](QPainter& painter) {
        for (const auto& piece : pieces) if (piece.bar) {
            painter.save(); painter.translate(piece.offset);
            piece.bar->paintCarrier(painter); painter.restore();
        }
    };
    visual.paintSurface = [pieces](QPainter& painter, const SurfaceGeometry& geometry) {
        for (const auto& piece : pieces) if (piece.bar) {
            const auto local = translatedSurfaceGeometry(geometry, -piece.offset);
            painter.save(); painter.translate(piece.offset);
            piece.bar->paintCarrier(painter, &local); painter.restore();
        }
    };
    return visual;
}
void DockSurface::setIncomingVisual(GroupId group, TabDragVisual visual, QPoint global)
{
    for (auto it = groups_.cbegin(); it != groups_.cend(); ++it) if (it.value())
        it.value()->setIncomingVisual(it.key() == group ? visual : TabDragVisual{}, global);
}
bool DockSurface::setDragTabStyle(const DockSurface& target, GroupId group, bool sticky)
{
    const auto* view = target.groups_.value(group).data();
    if (!view) return false;
    bool changed = false;
    for (const auto& carried : std::as_const(groups_)) if (carried)
        changed = carried->setDragTabStyle(*view->tabBar(), sticky) || changed;
    return changed;
}
void DockSurface::setDragPresentation(bool compact, bool edge)
{
    compactPresentation_ = compact;
    update();
    for (auto it = groups_.cbegin(); it != groups_.cend(); ++it) if (it.value()) {
        it.value()->setDragPresentation(compact, edge);
        if (!compact) {
            const auto* group = manager_->model().group(it.key());
            it.value()->setTabBarVisible(group && (group->location != GroupLocation::Central
                || centralTabBarVisible(manager_->tabBarsVisible(window_))));
        }
    }
    refreshContentEdgeToEdge();
}
QRect DockSurface::centralRectGlobal() const
{
    if (hasContentFocusScope())
        if (auto group = groups_.value(focusState_.group))
            return QRect(group->mapToGlobal(QPoint()), group->size());
    return center_ ? QRect(center_->mapToGlobal(QPoint()), center_->size()) : QRect();
}
QSplitter* DockSurface::splitterFor(QUuid node) const { return splitters_.value(node).data(); }
void DockSurface::updateEdgeTabSelection(TabId tab)
{
    for (const auto& strip : edgeBars_) if (strip)
        for (auto* control : strip->findChildren<QAbstractButton*>())
            if (auto* button = dynamic_cast<EdgeTabButton*>(control))
                button->setChecked(button->property("tabId").toUuid() == tab);
}
void DockSurface::selectTab(TabId tab)
{
    updateEdgeTabSelection(tab);
    const auto group = manager_->model().groupForTab(tab);
    const auto* record = manager_->model().group(group);
    if (record && record->location == GroupLocation::AutoHide && !focusedAutoHideGroup_) {
        showEdgeGroup(group, tab); return;
    }
    if (auto widget = groups_.value(group)) {
        if (const QPointer<TabContent> content(manager_->ensureTabView(tab)); content)
            contents_.insert(tab, content);
        const auto index = record->tabs.indexOf(tab);
        widget->setCurrentIndex(index);
    }
}
void DockSurface::showEdgeGroup(GroupId group, TabId tab)
{
    if (!manager_) return;
    const auto* record = manager_->model().group(group);
    if (!record || record->windowId != window_ || record->location != GroupLocation::AutoHide) return;
    if (tab.isNull()) tab = record->activeTab;
    if (!record->tabs.contains(tab)) return;
    if (hasContentFocusScope()) {
        if (group == focusState_.group && (focusState_.mode != ViewMode::PaneFullscreen || tab == focusState_.tab))
            selectTab(tab);
        else
            manager_->activateTab(tab);
        return;
    }
    const QPointer<DockSurface> self(this);
    const QPointer<WindowManager> owner(manager_);
    const auto generation = peekGeneration_ + 1;
    closePeek();
    if (!self || !owner || peekGeneration_ != generation || peek_) return;
    const QPointer<TabContent> content(owner->ensureTabView(tab));
    if (!self || !owner || !content || !owner->model().tab(tab)) return;
    owner->updateActive(window_, group, tab);
    if (!self || !owner || peekGeneration_ != generation) return;
    // Activation replaces the model's hash storage and notifies the host. The
    // host can also move or close this tab, or open a different peek, before
    // that notification returns. Never retain a borrowed record across it.
    record = owner->model().group(group);
    const auto* currentTab = owner->model().tab(tab);
    if (!record || record->windowId != window_ || record->location != GroupLocation::AutoHide
        || !record->tabs.contains(tab) || record->activeTab != tab || !currentTab) return;
    const auto edge = record->edge;
    const auto titleText = currentTab->title;
    updateEdgeTabSelection(tab);
    peekGroup_ = group;
    peekTab_ = tab;
    peek_ = new QFrame(this); peek_->setObjectName("PeekPanel");
    const QPointer<QFrame> panel(peek_);
    const auto currentRequest = [self, owner, panel, generation] {
        return self && owner && panel && self->peek_ == panel && self->peekGeneration_ == generation;
    };
    auto* layout = new QVBoxLayout(peek_); layout->setContentsMargins(1, 1, 1, 1); layout->setSpacing(0);
    auto* header = new PeekHeader(peek_); header->setObjectName("PeekTitleBar");
    header->drag = [this, tab](QPoint point) { if (manager_) manager_->beginTabDrag(tab, point); };
    auto* row = new QHBoxLayout(header); row->setContentsMargins(6, 1, 2, 1);
    row->setSpacing(2);
    auto* title = new QLabel(titleText, header);
    title->setAttribute(Qt::WA_TransparentForMouseEvents); row->addWidget(title); row->addStretch();
    auto* pin = new CompactToolButton(header, peekButtonExtent); pin->setIcon(iconFor("pin", manager_->theme()));
    pin->setToolTip(QCoreApplication::translate("WindowSystem", "Pin and reserve layout space")); pin->setAccessibleName(pin->toolTip()); row->addWidget(pin);
    auto* close = new CompactToolButton(header, peekButtonExtent); close->setIcon(iconFor("close", manager_->theme())); row->addWidget(close);
    close->setObjectName("ClosePeekTabButton");
    close->setToolTip(QCoreApplication::translate("WindowSystem", "Close tab")); close->setAccessibleName(close->toolTip());
    pin->setObjectName("PinPeekTabButton");
    QObject::connect(pin, &QToolButton::clicked, manager_, [this, tab] { manager_->pinTab(tab, true); });
    QObject::connect(close, &QToolButton::clicked, manager_, [this, tab] { manager_->closeTab(tab); });
    layout->addWidget(header);
    if (content && owner->model().tab(tab)) {
        contents_.insert(tab, content);
        layout->addWidget(content, 1);
        if (!currentRequest()) return;
        // Refresh parks tabs explicitly hidden. Showing their new ancestor
        // does not undo that flag, so restore the actual content visibility.
        content->show();
        if (!currentRequest()) return;
    }
    const auto createHandle = [this, edge, tab](Qt::Edges axes, const char* name) {
        auto* handle = new PeekResizeHandle(edge, peek_, axes);
        handle->setObjectName(QLatin1String(name));
        handle->requestedSize = [manager = QPointer<WindowManager>(manager_), tab, edge] {
            return manager ? manager->peekSizePolicy(tab, edge) : PeekSizePolicy{};
        };
        handle->resizeRequested = [manager = QPointer<WindowManager>(manager_), tab, edge](PeekSizePolicy size) {
            if (manager) manager->setPeekSizePolicy(tab, edge, size);
        };
        return QPointer<QWidget>(handle);
    };
    const bool vertical = edge == Edge::Left || edge == Edge::Right;
    const Qt::Edges inner = edge == Edge::Left ? Qt::RightEdge : edge == Edge::Right ? Qt::LeftEdge
        : edge == Edge::Top ? Qt::BottomEdge : Qt::TopEdge;
    const Qt::Edges secondary = vertical ? Qt::BottomEdge : Qt::RightEdge;
    peekResizeHandle_ = createHandle(inner, "PeekResizeHandle");
    peekSecondaryResizeHandle_ = createHandle(secondary, vertical ? "PeekHeightResizeHandle" : "PeekWidthResizeHandle");
    peekCornerResizeHandle_ = createHandle(inner | secondary, "PeekCornerResizeHandle");
    placePeek();
    if (!currentRequest()) return;
    panel->show();
    if (!currentRequest()) return;
    panel->raise();
    for (const auto& handle : {peekResizeHandle_, peekSecondaryResizeHandle_, peekCornerResizeHandle_}) {
        if (!currentRequest() || !handle) return;
        handle->show();
        if (!currentRequest() || !handle) return;
        handle->raise();
    }
    if (!currentRequest()) return;
    layout->activate();
    if (!currentRequest()) return;
    refreshContentEdgeToEdge();
    if (!currentRequest()) return;
    if (manager_) manager_->scheduleContentVisibilityNotification();
}
bool DockSurface::centralTabBarVisible(bool requested) const
{
    if (!requested || !manager_) return false;
    const auto* window = manager_->model().window(window_);
    if (!window) return false;
    const auto behavior = manager_->behavior();
    const bool hideSingle = window->kind == WindowKind::Floating
        ? behavior.hideSingleTabFloating : behavior.hideSingleTabWorkbench;
    return !hideSingle || manager_->model().orderedTabs(window_).size() != 1;
}
void DockSurface::setTabBarsVisible(bool visible)
{
    const QPointer<DockSurface> self(this);
    for (auto it = groups_.cbegin(); it != groups_.cend(); ++it) {
        const auto* record = manager_->model().group(it.key());
        if (it.value() && record)
            it.value()->setTabBarVisible(!focusState_.zen && (record->location != GroupLocation::Central || centralTabBarVisible(visible)));
    }
    refreshContentEdgeToEdge();
    if (!self) return;
    if (root_) root_->layout()->activate();
    placePeek();
}
void DockSurface::setSideBarsVisible(bool visible)
{
    if (!visible) closePeek();
    for (auto bar : edgeBars_) if (bar) bar->setVisible(visible);
    updateRailLayout();
    if (root_) root_->layout()->activate();
    restorePinnedExtents();
    placePeek();
    if (manager_) manager_->scheduleContentVisibilityNotification();
}
bool DockSurface::hasContentFocusScope() const
{
    return focusState_.mode == ViewMode::GroupFullscreen || focusState_.mode == ViewMode::PaneFullscreen;
}
void DockSurface::refreshContentEdgeToEdge()
{
    GroupId soleGroup;
    const auto& bars = focusState_.effective;
    if (manager_ && focusState_.zen && focusState_.mode != ViewMode::Normal && !compactPresentation_
        && !bars.toolbars && !bars.statusBar && !bars.tabBars && !bars.sideBars) {
        const auto& model = manager_->model();
        if (hasContentFocusScope()) {
            const auto* group = model.group(focusState_.group);
            const auto* tab = group ? model.tab(focusState_.mode == ViewMode::PaneFullscreen ? focusState_.tab : group->activeTab) : nullptr;
            if (group && group->windowId == window_ && tab && tab->panes
                && (focusState_.mode == ViewMode::PaneFullscreen || tab->panes->isLeaf()))
                soleGroup = group->id;
        } else if (!peek_) {
            // Only active pages contribute visible panes. Stop at the second
            // pane rather than walking inactive tabs or every pane leaf.
            for (auto it = groups_.cbegin(); it != groups_.cend(); ++it) {
                const auto* group = model.group(it.key());
                if (!it.value() || !group || group->windowId != window_ || group->location == GroupLocation::AutoHide) continue;
                const auto* tab = model.tab(group->activeTab);
                if (!tab || !tab->panes) continue;
                if (!soleGroup.isNull() || !tab->panes->isLeaf()) { soleGroup = {}; break; }
                soleGroup = group->id;
            }
        }
    }
    const QPointer<DockSurface> self(this);
    const auto generation = rebuildGeneration_;
    const auto groups = groups_;
    for (auto it = groups.cbegin(); it != groups.cend(); ++it) if (it.value()) {
        it.value()->setContentEdgeToEdge(it.key() == soleGroup);
        if (!self || rebuildGeneration_ != generation) return;
    }
    if (vertical_) {
        const QMargins margins = soleGroup.isNull()
            ? QMargins(surfaceOuterMargin, surfaceOuterMargin, surfaceOuterMargin, surfaceOuterMargin) : QMargins();
        if (vertical_->contentsMargins() != margins) vertical_->setContentsMargins(margins);
    }
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
    if (!hasContentFocusScope()) { refreshContentEdgeToEdge(); return; }
    const auto* record = manager_->model().group(focusState_.group);
    if (!record || record->windowId != window_) {
        focusState_.mode = ViewMode::Normal;
        refreshContentEdgeToEdge();
        return;
    }
    closePeek();
    auto* target = groups_.value(focusState_.group).data();
    if (record->location == GroupLocation::AutoHide) {
        const auto targetTab = focusState_.mode == ViewMode::PaneFullscreen
            ? focusState_.tab : record->activeTab;
        if (const QPointer<TabContent> content = manager_->ensureTabView(targetTab); content)
            contents_.insert(targetTab, content);
        focusedAutoHideGroup_ = buildGroup(record->id, contents_);
        target = focusedAutoHideGroup_;
        if (!target) return;
        focusedVerticalWasHidden_ = vertical_ && vertical_->isHidden();
        if (vertical_) vertical_->hide();
        auto* outer = root_ ? qobject_cast<QGridLayout*>(root_->layout()) : nullptr;
        if (!outer) return;
        outer->addWidget(target, 1, 1, 3, 3);
        target->show();
    } else {
        focusProjection_.apply(target, root_);
    }
    if (target) target->setTabBarVisible(!focusState_.zen
        && (record->location != GroupLocation::Central || focusState_.effective.tabBars));
    if (focusState_.mode == ViewMode::PaneFullscreen)
        if (auto content = contents_.value(focusState_.tab)) content->setFocusedPane(focusState_.pane);
    const QPointer<DockSurface> self(this);
    refreshContentEdgeToEdge();
    if (!self) return;
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
        if (manager_) manager_->scheduleContentVisibilityNotification();
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
    if (manager_) manager_->scheduleContentVisibilityNotification();
}
void DockSurface::closePeek()
{
    ++peekGeneration_;
    if (!peek_) return;
    const QPointer<DockSurface> self(this);
    const QPointer<QFrame> closing(peek_);
    peek_.clear(); peekResizeHandle_.clear(); peekSecondaryResizeHandle_.clear(); peekCornerResizeHandle_.clear();
    peekGroup_ = GroupId{}; peekTab_ = TabId{};
    const auto contents = contents_.values();
    for (const auto& content : contents) {
        if (!closing) break;
        if (content && closing->isAncestorOf(content)) {
            content->setParent(this);
            if (!self) return;
            if (content && content->parentWidget() == this) content->hide();
        }
    }
    delete closing.data();
    if (!self) return;
    refreshContentEdgeToEdge();
    if (!self) return;
    if (manager_) manager_->scheduleContentVisibilityNotification();
}
void DockSurface::updatePeekSize(TabId tab) { if (tab == peekTab_) placePeek(); }
void DockSurface::placePeek()
{
    if (!manager_ || !peek_) return;
    const auto* group = manager_->model().group(peekGroup_);
    if (!group) return;
    const auto edge = group->edge;
    const QPointer<QFrame> panel(peek_);
    const auto generation = peekGeneration_;
    QRect available = rect();
    if (auto bar = edgeBars_.value(Edge::Left); bar && !bar->isHidden()) available.setLeft(bar->mapTo(this, QPoint(bar->width(), 0)).x());
    if (auto bar = edgeBars_.value(Edge::Right); bar && !bar->isHidden()) available.setRight(bar->mapTo(this, QPoint()).x() - 1);
    if (auto bar = edgeBars_.value(Edge::Top); bar && !bar->isHidden()) available.setTop(bar->mapTo(this, QPoint(0, bar->height())).y());
    if (auto bar = edgeBars_.value(Edge::Bottom); bar && !bar->isHidden()) available.setBottom(bar->mapTo(this, QPoint()).y() - 1);
    const auto requested = manager_->peekSizePolicy(peekTab_, edge);
    const bool vertical = edge == Edge::Left || edge == Edge::Right;
    const auto minimum = peek_->minimumSizeHint();
    const auto h = qMin(available.height(), qMax(minimum.height(), requested.height.value_or(vertical ? available.height() : 170)));
    const auto w = qMin(available.width(), qMax(minimum.width(), requested.width.value_or(vertical ? 250 : available.width())));
    // A shortened panel follows its own trigger label, rather than always
    // opening from the leading corner of the workspace. Keep the full panel
    // inside the usable viewport when the label is near its trailing edge.
    QPoint origin = available.topLeft();
    if (const auto* strip = dynamic_cast<EdgeStrip*>(edgeBars_.value(edge).data()); strip && strip->isVisible()) {
        for (auto* button : strip->findChildren<QAbstractButton*>("EdgeButton")) {
            if (button->property("tabId").toUuid() != peekTab_) continue;
            const QRect trigger(button->mapTo(this, QPoint()), button->size());
            if (vertical) origin.setY(qBound(available.top(), trigger.top(), available.bottom() - h + 1));
            else origin.setX(qBound(available.left(), trigger.left(), available.right() - w + 1));
            break;
        }
    }
    const QPointer<DockSurface> self(this);
    switch (edge) {
    case Edge::Left: peek_->setGeometry(available.x(), origin.y(), w, h); break;
    case Edge::Right: peek_->setGeometry(available.right() - w + 1, origin.y(), w, h); break;
    case Edge::Top: peek_->setGeometry(origin.x(), available.y(), w, h); break;
    case Edge::Bottom: peek_->setGeometry(origin.x(), available.bottom() - h + 1, w, h); break;
    }
    if (!self || !manager_ || !panel || peek_ != panel || peekGeneration_ != generation) return;
    if (QPointer<QWidget> handle = peekResizeHandle_) {
        const int width = peek_->width(), height = peek_->height();
        switch (edge) {
        case Edge::Left: handle->setGeometry(width - 5, 1, 4, height - 2); break;
        case Edge::Right: handle->setGeometry(1, 1, 4, height - 2); break;
        case Edge::Top: handle->setGeometry(1, height - 5, width - 2, 4); break;
        case Edge::Bottom: handle->setGeometry(1, 1, width - 2, 4); break;
        }
        if (!self || !manager_ || !panel || peek_ != panel || peekGeneration_ != generation || !handle) return;
        handle->raise();
    }
    if (!self || !manager_ || !panel || peek_ != panel || peekGeneration_ != generation) return;
    const auto width = panel->width(), height = panel->height();
    if (QPointer<QWidget> handle = peekSecondaryResizeHandle_) {
        handle->setGeometry(vertical ? QRect(1, height - 5, width - 2, 4) : QRect(width - 5, 1, 4, height - 2));
        if (!self || !manager_ || !panel || peek_ != panel || peekGeneration_ != generation || !handle) return;
        handle->raise();
    }
    if (QPointer<QWidget> handle = peekCornerResizeHandle_) {
        const int x = edge == Edge::Right ? 1 : width - 5;
        const int y = edge == Edge::Bottom ? 1 : height - 5;
        handle->setGeometry(x, y, 4, 4);
        if (!self || !manager_ || !panel || peek_ != panel || peekGeneration_ != generation || !handle) return;
        handle->raise();
    }
}
void DockSurface::resizeEvent(QResizeEvent* event)
{
    const QPointer<DockSurface> self(this);
    const QPointer<WindowManager> owner(manager_);
    const auto generation = rebuildGeneration_;
    if (owner) ++owner->d->geometryUpdateDepth;
    const auto releaseGeometry = qScopeGuard([owner] { if (owner) --owner->d->geometryUpdateDepth; });
    QFrame::resizeEvent(event);
    if (!self || !owner || self->rebuildGeneration_ != generation) return;
    restorePinnedExtents();
    if (!self || !owner || self->rebuildGeneration_ != generation || !owner->model().window(window_)) return;
    placePeek();
}
bool DockSurface::eventFilter(QObject* watched, QEvent* event)
{
    if (!manager_) return QFrame::eventFilter(watched, event);
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
