// SPDX-License-Identifier: MIT
#include "content_bars.hpp"
#include <QQmlEngine>
#include <QQuickItem>

namespace window_system::quick {

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
        owners_.insert(next, {kind, id});
        connect(next, &QQuickItem::implicitHeightChanged, this, &ContentBars::changed);
        connect(next, &QQuickItem::implicitWidthChanged, this, &ContentBars::changed);
        connect(next, &QObject::destroyed, this, [this, kind, id, item] {
            if (destroying_) return;
            owners_.remove(item);
            // A host's earlier destroyed handler may already install a new bar.
            if (bars_[kind].value(id).isNull()) bars_[kind].remove(id);
            emit changed();
        });
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
