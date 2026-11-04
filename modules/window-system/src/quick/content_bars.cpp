// SPDX-License-Identifier: MIT
#include "content_bars.hpp"
#include <QQmlEngine>
#include <QGuiApplication>
#include <QQuickItem>
#include <algorithm>
#include <utility>

namespace window_system::quick {
namespace {
// Hierarchy conversion preserves both existing bars, in their existing order.
// This adds no surface or styling; its implicit size is the stacked content.
class BarStackItem final : public QQuickItem {
public:
    void setBars(QQuickItem* first, QQuickItem* second)
    {
        const QPointer<BarStackItem> self(this);
        bars_ = {first, second};
        for (const auto& bar : bars_) if (bar) {
            connect(bar, &QQuickItem::implicitHeightChanged, this, &BarStackItem::arrange);
            connect(bar, &QQuickItem::implicitWidthChanged, this, &BarStackItem::arrange);
            connect(bar, &QObject::destroyed, this, &BarStackItem::arrange);
            bar->setParent(this);
            if (!self) return;
            if (bar) bar->setParentItem(this);
            if (!self) return;
            if (bar) bar->setVisible(true);
            if (!self) return;
        }
        arrange();
    }
protected:
    void geometryChange(const QRectF& now, const QRectF& previous) override
    {
        const QPointer<BarStackItem> self(this);
        QQuickItem::geometryChange(now, previous);
        if (self && now.width() != previous.width()) arrange();
    }
private:
    void arrange()
    {
        if (arranging_) { pending_ = true; return; }
        arranging_ = true;
        const QPointer<BarStackItem> self(this);
        do {
            pending_ = false;
            qreal naturalWidth = 0, y = 0;
            for (const auto& bar : bars_) if (bar) {
                const qreal height = std::max(qreal(0), bar->implicitHeight());
                naturalWidth = std::max(naturalWidth, bar->implicitWidth());
                bar->setPosition(QPointF(0, y));
                if (!self) return;
                if (bar) bar->setSize(QSizeF(width(), height));
                if (!self) return;
                y += height;
            }
            setImplicitSize(naturalWidth, y);
            if (!self) return;
        } while (pending_);
        arranging_ = false;
    }
    std::array<QPointer<QQuickItem>, 2> bars_;
    bool arranging_ = false, pending_ = false;
};
}

ContentBars::ContentBars(QObject* parent) : QObject(parent) {}
ContentBars::~ContentBars() { destroying_ = true; }

bool ContentBars::setTab(TabId id, QQuickItem* item, bool top) { return set(top ? TabTop : TabBottom, id, item); }
bool ContentBars::setPane(PaneId id, QQuickItem* item, bool top) { return set(top ? PaneTop : PaneBottom, id, item); }
QQuickItem* ContentBars::tab(TabId id, bool top) const { return bars_[top ? TabTop : TabBottom].value(id); }
QQuickItem* ContentBars::pane(PaneId id, bool top) const { return bars_[top ? PaneTop : PaneBottom].value(id); }

bool ContentBars::set(Kind kind, QUuid id, QQuickItem* item)
{
    const QPointer<ContentBars> self(this);
    const QPointer<QQuickItem> next(item), previous(bars_[kind].value(id));
    if (next == previous) return true;
    if (next && next->property("windowSystemManaged").toBool()) return false;
    if (next) bars_[kind].insert(id, next); else bars_[kind].remove(id);
    const auto current = [&] { return self && (!next || bars_[kind].value(id) == next); };
    if (previous) {
        owners_.remove(previous);
        disconnect(previous, nullptr, this, nullptr);
        previous->setVisible(false);
        if (previous) previous->deleteLater();
        if (!current()) return false;
        if (previous) previous->setParentItem(nullptr);
        if (!current()) return false;
    }
    if (next) {
        watch(kind, id, next);
        QQmlEngine::setObjectOwnership(next, QQmlEngine::CppOwnership);
        next->setParent(this);
        if (!current() || !next) return false;
        next->setProperty("windowSystemManaged", true);
        if (!current() || !next) return false;
        next->setVisible(false);
        if (!current() || !next) return false;
        next->setParentItem(nullptr);
        if (!current() || !next) return false;
    }
    emit changed();
    return bool(self);
}

void ContentBars::watch(Kind kind, QUuid id, QQuickItem* item)
{
    owners_.insert(item, {kind, id});
    connect(item, &QQuickItem::implicitHeightChanged, this, &ContentBars::changed);
    connect(item, &QQuickItem::implicitWidthChanged, this, &ContentBars::changed);
    connect(item, &QObject::destroyed, this, [this, kind, id, item] {
        if (destroying_) return;
        owners_.remove(item);
        // A host's earlier destroyed handler may already install a new bar.
        if (bars_[kind].value(id).isNull()) bars_[kind].remove(id);
        emit changed();
    });
}

QQuickItem* ContentBars::take(Kind kind, QUuid id)
{
    auto item = bars_[kind].take(id);
    if (item) { owners_.remove(item); disconnect(item, nullptr, this, nullptr); }
    return item;
}

QQuickItem* ContentBars::takeTransferredFocus() { return std::exchange(transferredFocus_, {}).data(); }

void ContentBars::transferToPanes(const QVector<QPair<TabId, PaneId>>& sources)
{
    const QPointer<ContentBars> self(this);
    const QPointer<QQuickItem> focus(qobject_cast<QQuickItem*>(QGuiApplication::focusObject()));
    for (const auto& [tabId, paneId] : sources) for (const bool top : {true, false}) {
        const QPointer<QQuickItem> outer(take(top ? TabTop : TabBottom, tabId));
        if (!outer) continue;
        const auto kind = top ? PaneTop : PaneBottom;
        const QPointer<QQuickItem> previous(take(kind, paneId));
        for (const auto& bar : {outer, previous})
            if (bar && focus && (bar == focus || bar->isAncestorOf(focus))) transferredFocus_ = focus;
        if (!previous) {
            bars_[kind].insert(paneId, outer);
            watch(kind, paneId, outer);
        } else {
            const QPointer<BarStackItem> stack(new BarStackItem);
            set(kind, paneId, stack);
            if (!self) return;
            if (stack) stack->setBars(top ? outer.data() : previous.data(), top ? previous.data() : outer.data());
            if (!self) return;
        }
    }
    emit changed();
}

PaneId ContentBars::paneForItem(const LayoutModel& model, QQuickItem* item) const
{
    for (auto* ancestor = item; ancestor; ancestor = ancestor->parentItem()) {
        const auto found = owners_.constFind(ancestor);
        if (found == owners_.cend()) continue;
        if (found->kind == PaneTop || found->kind == PaneBottom) return found->id;
        const auto* tab = model.tab(found->id);
        return tab ? tab->activePane : PaneId{};
    }
    return {};
}

void ContentBars::prune(const LayoutModel& model)
{
    const QPointer<ContentBars> self(this);
    for (int kind = TabTop; kind <= PaneBottom; ++kind) {
        const auto ids = bars_[kind].keys();
        for (const auto id : ids) {
            if (kind <= TabBottom ? bool(model.tab(id)) : !model.tabForPane(id).isNull()) continue;
            set(Kind(kind), id, nullptr);
            if (!self) return;
        }
    }
}

} // namespace window_system::quick
