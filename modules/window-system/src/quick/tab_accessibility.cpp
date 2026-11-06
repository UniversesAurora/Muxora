// SPDX-License-Identifier: MIT
#include "tab_accessibility.hpp"
#include "accessibility.hpp"
#include "tab_bar_item.hpp"
#include <QAccessibleObject>
#include <QCoreApplication>
#include <QQuickWindow>
#include <utility>

namespace window_system::quick {
namespace {

enum class NodeKind { Tab, Folder };
using NodeKey = QPair<NodeKind, QUuid>;

QRect globalRect(const TabBarItem* bar, const QRectF& local)
{
    if (!bar->window() || local.isEmpty()) return {};
    const auto scene = bar->mapRectToScene(local).toAlignedRect();
    return {bar->window()->mapToGlobal(scene.topLeft()), scene.size()};
}

bool shown(const TabBarItem* bar)
{
    return bar->isVisible() && bar->window() && bar->window()->isVisible()
        && !(bar->window()->windowStates() & Qt::WindowMinimized);
}

struct PendingEvent {
    QAccessible::Id id;
    QAccessible::Event type;
    QAccessible::State changed;
};

class TabNode;

class TabList final : public QAccessibleObject, public QAccessibleSelectionInterface, public QAccessibleActionInterface {
public:
    explicit TabList(TabBarItem* bar);
    ~TabList() override;
    QWindow* window() const override { return bar_ ? bar_->window() : nullptr; }
    QAccessibleInterface* parent() const override;
    QAccessibleInterface* child(int index) const override;
    int childCount() const override;
    int indexOfChild(const QAccessibleInterface* child) const override;
    QAccessibleInterface* childAt(int x, int y) const override;
    QAccessibleInterface* focusChild() const override;
    QString text(QAccessible::Text type) const override;
    QRect rect() const override { return bar_ ? globalRect(bar_, bar_->boundingRect()) : QRect{}; }
    QAccessible::Role role() const override { return QAccessible::PageTabList; }
    QAccessible::State state() const override;
    void* interface_cast(QAccessible::InterfaceType type) override;
    int selectedItemCount() const override;
    QList<QAccessibleInterface*> selectedItems() const override;
    bool select(QAccessibleInterface* child) override;
    bool unselect(QAccessibleInterface* child) override;
    bool selectAll() override;
    bool clear() override { return selectedItemCount() == 0; }
    QStringList actionNames() const override { return {setFocusAction()}; }
    void doAction(const QString& name) override { if (bar_ && bar_->isEnabled() && shown(bar_) && name == setFocusAction()) bar_->forceActiveFocus(); }
    QStringList keyBindingsForAction(const QString&) const override { return {}; }
    TabBarItem* bar() const { return bar_; }
    int position(const NodeKey& key) const;

private:
    void ensureIndexes() const;
    void queueUpdate();
    void update();
    QPointer<TabBarItem> bar_;
    // Cancels queued work and connections with this non-QObject interface.
    QObject observer_;
    mutable QHash<NodeKey, int> positions_;
    mutable QVector<NodeKey> order_;
    mutable QHash<NodeKey, QAccessible::Id> children_;
    mutable bool indexesDirty_ = true;
    bool structureChanged_ = false;
    bool contentsChanged_ = false;
    bool selectionChanged_ = false;
    bool focusChanged_ = false;
    bool updateQueued_ = false;
};

class TabNode final : public QAccessibleInterface, public QAccessibleActionInterface {
public:
    TabNode(TabList* owner, NodeKey key) : owner_(owner), key_(std::move(key))
    {
        name_ = text(QAccessible::Name); description_ = text(QAccessible::Description);
        expanded_ = state().expanded;
    }
    void detach() { owner_ = nullptr; }
    const NodeKey& key() const { return key_; }
    bool belongsTo(const TabList* owner) const { return owner_ == owner; }
    int index() const { return owner_ && owner_->bar() ? owner_->position(key_) : -1; }
    bool isValid() const override { return index() >= 0; }
    QObject* object() const override { return nullptr; }
    QWindow* window() const override { return owner_ ? owner_->window() : nullptr; }
    QAccessibleInterface* parent() const override { return owner_; }
    QAccessibleInterface* child(int) const override { return nullptr; }
    int childCount() const override { return 0; }
    int indexOfChild(const QAccessibleInterface*) const override { return -1; }
    QAccessibleInterface* childAt(int, int) const override { return nullptr; }
    QString text(QAccessible::Text type) const override;
    void setText(QAccessible::Text, const QString&) override {}
    QRect rect() const override;
    QAccessible::Role role() const override { return key_.first == NodeKind::Tab ? QAccessible::PageTab : QAccessible::Button; }
    QAccessible::State state() const override;
    void* interface_cast(QAccessible::InterfaceType type) override { return type == QAccessible::ActionInterface ? static_cast<QAccessibleActionInterface*>(this) : nullptr; }
    QStringList actionNames() const override;
    void doAction(const QString& name) override;
    QStringList keyBindingsForAction(const QString&) const override { return {}; }
    void collectChanges(QAccessible::Id id, QVector<PendingEvent>& events);

private:
    TabList* owner_;
    NodeKey key_;
    QString name_, description_;
    bool expanded_ = false;
};

TabList::TabList(TabBarItem* bar) : QAccessibleObject(bar), bar_(bar)
{
    QObject::connect(bar, &TabBarItem::structureChanged, &observer_, [this] {
        indexesDirty_ = true; structureChanged_ = true; contentsChanged_ = true; selectionChanged_ = true;
        queueUpdate();
    });
    QObject::connect(bar, &TabBarItem::contentsChanged, &observer_, [this] { contentsChanged_ = true; queueUpdate(); });
    QObject::connect(bar, &TabBarItem::currentChanged, &observer_, [this] { selectionChanged_ = true; queueUpdate(); });
    QObject::connect(bar, &QQuickItem::activeFocusChanged, &observer_, [this] { focusChanged_ = true; queueUpdate(); });
}

TabList::~TabList()
{
    // QObject destruction reaches the cache after TabBarItem's controller
    // has gone. Detach virtual children before Qt asks if they are valid.
    for (const auto id : std::as_const(children_)) {
        if (auto* child = static_cast<TabNode*>(QAccessible::accessibleInterface(id))) child->detach();
        QAccessible::deleteAccessibleInterface(id);
    }
}

void TabList::ensureIndexes() const
{
    if (!indexesDirty_) return;
    positions_.clear(); order_.clear();
    if (bar_) {
        const auto& controller = bar_->controller();
        order_.reserve(controller.count() + controller.folders().size());
        // Match the existing tree: tabs first, then folder headers.
        for (int index = 0; index < controller.count(); ++index) {
            const NodeKey key{NodeKind::Tab, controller.tabData(index).toUuid()};
            positions_.insert(key, index); order_.append(key);
        }
        for (int index = 0; index < controller.folders().size(); ++index) {
            const NodeKey key{NodeKind::Folder, controller.folders().at(index).id};
            positions_.insert(key, index); order_.append(key);
        }
    }
    indexesDirty_ = false;
}

int TabList::position(const NodeKey& key) const { ensureIndexes(); return positions_.value(key, -1); }
int TabList::childCount() const { ensureIndexes(); return int(order_.size()); }

QAccessibleInterface* TabList::parent() const
{
    if (!bar_) return nullptr;
    auto* parent = bar_->parentItem();
    if (bar_->window() && parent == bar_->window()->contentItem()) return QAccessible::queryAccessibleInterface(bar_->window());
    return QAccessible::queryAccessibleInterface(parent);
}

QAccessibleInterface* TabList::child(int index) const
{
    ensureIndexes();
    if (index < 0 || index >= order_.size()) return nullptr;
    const auto& key = order_.at(index);
    auto& id = children_[key];
    if (!QAccessible::accessibleInterface(id))
        id = QAccessible::registerAccessibleInterface(new TabNode(const_cast<TabList*>(this), key));
    return QAccessible::accessibleInterface(id);
}

int TabList::indexOfChild(const QAccessibleInterface* child) const
{
    const auto* node = dynamic_cast<const TabNode*>(child);
    if (!node || !node->belongsTo(this)) return -1;
    const int index = node->index();
    return index < 0 || node->key().first == NodeKind::Tab ? index : bar_->controller().count() + index;
}

QAccessibleInterface* TabList::childAt(int x, int y) const
{
    if (!bar_ || !shown(bar_)) return nullptr;
    const auto local = bar_->mapFromGlobal(QPointF(x, y)).toPoint();
    const auto& controller = bar_->controller();
    const int tab = controller.tabAt(local);
    if (tab >= 0) return child(tab);
    const int folder = controller.folderAt(local);
    return folder >= 0 ? child(controller.count() + folder) : nullptr;
}

QAccessibleInterface* TabList::focusChild() const
{
    return bar_ && bar_->hasActiveFocus() ? child(bar_->controller().currentIndex()) : nullptr;
}

QString TabList::text(QAccessible::Text type) const
{
    return type == QAccessible::Name ? QCoreApplication::translate("WindowSystem", "Tab bar") : QString{};
}

QAccessible::State TabList::state() const
{
    QAccessible::State result;
    if (!bar_) { result.invalid = true; return result; }
    result.disabled = !bar_->isEnabled(); result.invisible = !shown(bar_);
    result.focusable = bar_->activeFocusOnTab(); result.focused = bar_->hasActiveFocus();
    return result;
}

void* TabList::interface_cast(QAccessible::InterfaceType type)
{
    if (type == QAccessible::SelectionInterface) return static_cast<QAccessibleSelectionInterface*>(this);
    if (type == QAccessible::ActionInterface) return static_cast<QAccessibleActionInterface*>(this);
    return nullptr;
}

int TabList::selectedItemCount() const { return bar_ && bar_->controller().currentIndex() >= 0 ? 1 : 0; }

QList<QAccessibleInterface*> TabList::selectedItems() const
{
    if (selectedItemCount() == 0) return {};
    return {child(bar_->controller().currentIndex())};
}

bool TabList::select(QAccessibleInterface* child)
{
    auto* node = dynamic_cast<TabNode*>(child);
    if (!node || !node->belongsTo(this) || node->key().first != NodeKind::Tab || !node->isValid()
        || !bar_->isEnabled() || !shown(bar_)) return false;
    node->doAction(pressAction());
    return true;
}

bool TabList::unselect(QAccessibleInterface* child)
{
    const auto* node = dynamic_cast<const TabNode*>(child);
    return node && node->belongsTo(this) && node->key().first == NodeKind::Tab && node->isValid()
        && node->index() != bar_->controller().currentIndex();
}

bool TabList::selectAll() { return bar_ && bar_->controller().count() == 1 && select(child(0)); }

void TabList::queueUpdate()
{
    if (updateQueued_) return;
    updateQueued_ = true;
    QTimer::singleShot(0, &observer_, [this] { update(); });
}

void TabList::update()
{
    updateQueued_ = false;
    const bool structure = std::exchange(structureChanged_, false);
    const bool contents = std::exchange(contentsChanged_, false);
    const bool selection = std::exchange(selectionChanged_, false);
    const bool focus = std::exchange(focusChanged_, false);
    ensureIndexes();
    // Rebuilding positions must never delete the node whose isValid() or
    // action queried them. Prune later, outside synchronous client queries.
    if (structure) for (auto it = children_.begin(); it != children_.end();) {
        if (positions_.contains(it.key())) { ++it; continue; }
        const auto id = it.value();
        it = children_.erase(it);
        if (auto* child = static_cast<TabNode*>(QAccessible::accessibleInterface(id))) child->detach();
        QAccessible::deleteAccessibleInterface(id);
    }
    if (!bar_ || !QAccessible::isActive()) return;
    QVector<PendingEvent> events;
    if (structure) events.append({QAccessible::uniqueId(this), QAccessible::ObjectReorder, {}});
    if (contents) for (const auto id : std::as_const(children_))
        if (auto* child = static_cast<TabNode*>(QAccessible::accessibleInterface(id))) child->collectChanges(id, events);
    if (selection || focus) {
        if (auto* selected = child(bar_->controller().currentIndex())) {
            const auto id = QAccessible::uniqueId(selected);
            if (selection) {
                events.append({id, QAccessible::Selection, {}});
                // The Windows UIA bridge maps SelectionAdd to its native
                // ElementSelected event, including for single selection.
                events.append({id, QAccessible::SelectionAdd, {}});
            }
            if (bar_->hasActiveFocus()) events.append({id, QAccessible::Focus, {}});
        }
    }
    const QPointer<TabBarItem> alive(bar_);
    for (const auto& pending : events) {
        if (auto* target = QAccessible::accessibleInterface(pending.id)) {
            if (pending.type == QAccessible::StateChanged) {
                QAccessibleStateChangeEvent event(target, pending.changed);
                QAccessible::updateAccessibility(&event);
            } else {
                QAccessibleEvent event(target, pending.type);
                QAccessible::updateAccessibility(&event);
            }
        }
        if (!alive) return;
    }
}

QString TabNode::text(QAccessible::Text type) const
{
    const int at = index();
    if (at < 0) return {};
    const auto& controller = owner_->bar()->controller();
    if (key_.first == NodeKind::Tab) {
        if (type == QAccessible::Name) return controller.tabText(at);
        if (type == QAccessible::Description) return controller.tabToolTip(at);
    } else if (type == QAccessible::Name) {
        const auto& folder = controller.folders().at(at);
        const auto count = QCoreApplication::translate("WindowSystem", "%n tab(s)", nullptr, folder.count);
        return folder.name.isEmpty() ? QCoreApplication::translate("WindowSystem", "Tab folder, %1").arg(count)
                                    : QCoreApplication::translate("WindowSystem", "Tab folder %1, %2").arg(folder.name, count);
    }
    return {};
}

QRect TabNode::rect() const
{
    const int at = index();
    if (at < 0) return {};
    auto* bar = owner_->bar();
    return globalRect(bar, key_.first == NodeKind::Tab ? bar->controller().tabRect(at) : bar->controller().folderHeaderRect(at));
}

QAccessible::State TabNode::state() const
{
    QAccessible::State result;
    const int at = index();
    if (at < 0) { result.invalid = true; return result; }
    auto* bar = owner_->bar();
    result.disabled = !bar->isEnabled(); result.invisible = !shown(bar) || rect().isEmpty();
    result.offscreen = rect().isEmpty();
    if (key_.first == NodeKind::Tab) {
        result.selectable = true; result.selected = at == bar->controller().currentIndex();
        result.focusable = true; result.focused = result.selected && bar->hasActiveFocus();
    } else {
        result.expandable = true; result.expanded = !bar->controller().folders().at(at).collapsed;
        result.collapsed = !result.expanded;
    }
    return result;
}

QStringList TabNode::actionNames() const
{
    return key_.first == NodeKind::Tab ? QStringList{pressAction(), setFocusAction()} : QStringList{pressAction(), toggleAction()};
}

void TabNode::doAction(const QString& name)
{
    const int at = index();
    if (at < 0) return;
    const QPointer<TabBarItem> bar(owner_->bar());
    if (!bar->isEnabled() || !shown(bar)) return;
    if (key_.first == NodeKind::Folder) {
        if (name == pressAction() || name == toggleAction()) emit bar->actionRequested({TabPointerDecision::Action::ToggleFolder, at});
        return;
    }
    if (name != pressAction() && name != setFocusAction()) return;
    bar->edit([&] { bar->controller().setCurrentIndex(at); bar->controller().ensureVisible(at); });
    if (bar && name == setFocusAction()) bar->forceActiveFocus();
}

void TabNode::collectChanges(QAccessible::Id id, QVector<PendingEvent>& events)
{
    const auto name = text(QAccessible::Name), description = text(QAccessible::Description);
    const bool expanded = state().expanded;
    if (name != name_) events.append({id, QAccessible::NameChanged, {}});
    if (description != description_) events.append({id, QAccessible::DescriptionChanged, {}});
    if (expanded != expanded_) {
        QAccessible::State changed; changed.expanded = true; changed.collapsed = true;
        events.append({id, QAccessible::StateChanged, changed});
    }
    name_ = name; description_ = description; expanded_ = expanded;
}

QAccessibleInterface* tabFactory(const QString& className, QObject* object)
{
    if (className == QLatin1StringView("window_system::quick::TabBarItem")) return new TabList(static_cast<TabBarItem*>(object));
    return nullptr;
}

} // namespace

void installTabAccessibility(TabBarItem* bar)
{
    static const bool installed = [] { QAccessible::installFactory(tabFactory); return true; }();
    Q_UNUSED(installed);
    attachAccessibility(bar, QAccessible::PageTabList);
}

} // namespace window_system::quick
