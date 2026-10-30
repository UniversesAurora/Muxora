// SPDX-License-Identifier: MIT
#include "content_host.hpp"

#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <utility>

namespace window_system::quick {

ContentHost::ContentHost(QObject* parent) : QObject(parent) {}

ContentHost::~ContentHost()
{
    destroying_ = true;
    const auto entries = std::exchange(entries_, {});
    owners_.clear();
    for (const auto& entry : entries) {
        if (!entry.item) continue;
        QObject::disconnect(entry.item, nullptr, this, nullptr);
        delete entry.item.data();
    }
}

void ContentHost::setFactory(Factory factory) { factory_ = std::move(factory); }

bool ContentHost::create(PaneId pane, const TabOptions& options)
{
    if (destroying_ || pane.isNull() || entries_.contains(pane) || !factory_) return false;
    const QPointer<ContentHost> self(this);
    const auto factory = factory_;
    const QPointer<QQuickItem> content = factory(options, pane);
    const bool adopted = self && content && self->adopt(pane, content);
    if (!adopted && content && !content->property("windowSystemManaged").toBool()) delete content.data();
    return adopted;
}

bool ContentHost::current(PaneId pane, const QQuickItem* expected) const
{
    return !destroying_ && expected && entries_.value(pane).item == expected;
}

bool ContentHost::adopt(PaneId pane, QQuickItem* content)
{
    if (destroying_ || pane.isNull() || !content || entries_.contains(pane)
        || owners_.contains(content) || content->property("windowSystemManaged").toBool()) return false;
    const QPointer<ContentHost> self(this);
    const QPointer<QQuickItem> observed(content);
    const auto alive = [&] { return self && observed && self->current(pane, observed); };
    entries_.insert(pane, {content, {}});
    owners_.insert(content, pane);
    order_.append(pane);
    connect(content, &QQuickItem::visibleChanged, this, &ContentHost::scheduleVisibility);
    connect(content, &QQuickItem::windowChanged, this, [this](QQuickWindow* window) {
        if (destroying_) return;
        watchWindow(window);
        scheduleVisibility();
    });
    connect(content, &QObject::destroyed, this, [this, content] {
        if (destroying_) return;
        const auto found = owners_.find(content);
        if (found == owners_.end()) return;
        const auto pane = found.value();
        owners_.erase(found);
        entries_.remove(pane);
        order_.removeAll(pane);
        scheduleVisibility();
    });
    QQmlEngine::setObjectOwnership(content, QQmlEngine::CppOwnership);
    content->setProperty("windowSystemManaged", true);
    if (!alive()) return false;
    content->setProperty("paneId", pane);
    if (!alive()) return false;
    content->setParent(this);
    if (!alive()) return false;
    if (content->focusPolicy() == Qt::NoFocus) content->setFocusPolicy(Qt::StrongFocus);
    if (!alive()) return false;
    content->setVisible(false);
    if (!alive()) return false;
    content->setParentItem(nullptr);
    if (!alive()) return false;
    scheduleVisibility();
    return true;
}

bool ContentHost::mount(PaneId pane, QQuickItem* parent)
{
    const QPointer<QQuickItem> content(item(pane));
    if (!content || !parent || parent == content || content->isAncestorOf(parent)) return false;
    const QPointer<ContentHost> self(this);
    const QPointer<QQuickItem> target(parent);
    const auto alive = [&] { return self && content && self->current(pane, content) && target; };
    if (content->window() && content->window() != parent->window()) {
        const bool wasVisible = visible(pane);
        content->setVisible(false);
        if (!alive()) return false;
        reportHidden(pane, wasVisible);
        if (!alive()) return false;
    }
    content->setParentItem(parent);
    if (!alive() || content->parentItem() != target) return false;
    watchWindow(content->window());
    content->setVisible(true);
    return alive() && content->parentItem() == target;
}

void ContentHost::unmount(PaneId pane)
{
    const QPointer<QQuickItem> content(item(pane));
    if (!content) return;
    const QPointer<ContentHost> self(this);
    const bool wasVisible = visible(pane);
    content->setVisible(false);
    if (!self || !content || !self->current(pane, content)) return;
    reportHidden(pane, wasVisible);
    if (!self || !content || !self->current(pane, content)) return;
    content->setParentItem(nullptr);
}

void ContentHost::remove(PaneId pane)
{
    if (destroying_) return;
    const QPointer<ContentHost> self(this);
    const auto entry = entries_.take(pane);
    order_.removeAll(pane);
    if (entry.item) {
        owners_.remove(entry.item);
        QObject::disconnect(entry.item, nullptr, this, nullptr);
        entry.item->setVisible(false);
        if (entry.item) entry.item->deleteLater();
    }
    if (self) self->scheduleVisibility();
}

void ContentHost::setVisible(PaneId pane, bool visible)
{
    if (auto* content = item(pane)) content->setVisible(visible);
}

QQuickItem* ContentHost::item(PaneId pane) const
{
    return destroying_ ? nullptr : entries_.value(pane).item.data();
}

bool ContentHost::visible(PaneId pane) const
{
    const auto* content = item(pane);
    const auto* window = content ? content->window() : nullptr;
    return content && content->isVisible() && window && window->isVisible()
        && !(window->windowStates() & Qt::WindowMinimized);
}

PaneId ContentHost::paneForItem(QQuickItem* content) const
{
    for (auto* ancestor = content; ancestor; ancestor = ancestor->parentItem()) {
        const auto found = owners_.constFind(ancestor);
        if (found != owners_.cend()) return found.value();
    }
    return {};
}

QQuickItem* ContentHost::focusItem(PaneId pane) const
{
    const auto entry = entries_.value(pane);
    if (!entry.item) return nullptr;
    auto* current = entry.item->window() ? entry.item->window()->activeFocusItem() : nullptr;
    if (current && (current == entry.item || entry.item->isAncestorOf(current))) return current;
    return entry.focus && entry.focus->isVisible() && entry.focus->isEnabled()
        && entry.item->isAncestorOf(entry.focus) ? entry.focus.data() : entry.item.data();
}

bool ContentHost::focus(PaneId pane, Qt::FocusReason reason)
{
    const QPointer<QQuickItem> content(item(pane));
    if (!content || !content->isVisible() || !content->isEnabled() || !content->window()) return false;
    const QPointer<ContentHost> self(this);
    const QPointer<QQuickItem> target(focusItem(pane));
    entries_[pane].focus = target;
    target->forceActiveFocus(reason);
    // An inactive window stores subfocus without activeFocus. That is a
    // successful preparation for returning to it, not a reason to activate it.
    return self && content && self->current(pane, content) && target && target->hasFocus();
}

void ContentHost::watchWindow(QQuickWindow* window)
{
    if (!window || watchedWindows_.contains(window)) return;
    watchedWindows_.insert(window);
    connect(window, &QWindow::visibilityChanged, this, &ContentHost::scheduleVisibility);
    connect(window, &QWindow::windowStateChanged, this, &ContentHost::scheduleVisibility);
    connect(window, &QQuickWindow::activeFocusItemChanged, this, [this, window] { rememberFocus(window); });
    connect(window, &QObject::destroyed, this, [this, window] {
        if (destroying_) return;
        watchedWindows_.remove(window);
        scheduleVisibility();
    });
}

void ContentHost::rememberFocus(QQuickWindow* window)
{
    if (destroying_) return;
    auto* target = window->activeFocusItem();
    const auto pane = paneForItem(target);
    if (pane.isNull()) return;
    entries_[pane].focus = target;
    emit paneFocused(pane, window);
}

void ContentHost::reportHidden(PaneId pane, bool wasVisible)
{
    if (!visibilityHandler_) return;
    const auto previous = reportedVisibility_.constFind(pane);
    if ((previous != reportedVisibility_.cend() && !previous.value())
        || (previous == reportedVisibility_.cend() && !wasVisible)) return;
    reportedVisibility_[pane] = false;
    if (!reportedOrder_.contains(pane)) reportedOrder_.append(pane);
    const auto handler = visibilityHandler_;
    handler({{pane, false}});
}

void ContentHost::setVisibilityHandler(VisibilityHandler handler)
{
    visibilityHandler_ = std::move(handler);
    visibilitySnapshot_ = bool(visibilityHandler_);
    scheduleVisibility();
}

void ContentHost::scheduleVisibility()
{
    if (destroying_ || visibilityPending_ || !visibilityHandler_) return;
    visibilityPending_ = true;
    QTimer::singleShot(0, this, &ContentHost::publishVisibility);
}

void ContentHost::publishVisibility()
{
    visibilityPending_ = false;
    if (destroying_ || !visibilityHandler_) return;
    QHash<PaneId, bool> current;
    QVector<ContentVisibilityChange> changes;
    for (const auto pane : order_) {
        const bool shown = visible(pane);
        current.insert(pane, shown);
        const auto previous = reportedVisibility_.constFind(pane);
        if (visibilitySnapshot_ || previous == reportedVisibility_.cend() || previous.value() != shown)
            changes.append({pane, shown});
    }
    for (const auto pane : reportedOrder_)
        if (!current.contains(pane) && reportedVisibility_.value(pane)) changes.append({pane, false});
    reportedVisibility_ = std::move(current);
    reportedOrder_ = order_;
    visibilitySnapshot_ = false;
    if (changes.isEmpty()) return;
    const auto handler = visibilityHandler_;
    handler(changes);
}

} // namespace window_system::quick
