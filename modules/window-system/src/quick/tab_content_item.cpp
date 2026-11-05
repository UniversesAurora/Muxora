// SPDX-License-Identifier: MIT
#include "tab_content_item.hpp"
#include "content_host.hpp"
#include "content_bars.hpp"
#include "split_tree_item.hpp"
#include <window_system/window_manager.hpp>
#include <QContextMenuEvent>
#include <QQuickWindow>
#include <QtMath>
#include <algorithm>
#include <array>
#include <functional>

namespace window_system::quick {
// Shared by the whole tab and individual panes. Ownership stays with the
// hosts; this column only mounts and sizes optional bars around a body.
class ContentColumnItem final : public QQuickItem {
public:
    explicit ContentColumnItem(QQuickItem* parent) : QQuickItem(parent) { setClip(true); }
    std::function<void(QPoint, QQuickItem*)> contextMenuRequested;

    void configure(QQuickItem* top, QQuickItem* body, QQuickItem* bottom, QSize bodyMinimum = QSize(0, 0))
    {
        const QPointer<ContentColumnItem> self(this);
        const std::array<QPointer<QQuickItem>, 3> next{top, body, bottom};
        const auto previous = items_;
        items_ = next;
        bodyMinimum_ = bodyMinimum;
        for (const auto& item : previous) {
            if (!item || item->parentItem() != this || std::find(next.begin(), next.end(), item) != next.end()) continue;
            item->setVisible(false);
            if (!self) return;
            if (item) item->setParentItem(nullptr);
            if (!self) return;
        }
        for (const auto& item : next) if (item) {
            if (item->parentItem() != this) item->setParentItem(this);
            if (!self) return;
            if (item) item->setVisible(true);
            if (!self) return;
        }
        arrange();
    }

    QSize minimumSize() const
    {
        auto minimum = bodyMinimum_;
        for (const int index : {0, 2}) if (const auto& item = items_[index]; item && item->parentItem() == this) {
            minimum.setWidth(std::max(minimum.width(), qCeil(item->implicitWidth())));
            minimum.rheight() += qCeil(std::max(qreal(0), item->implicitHeight()));
        }
        return minimum;
    }

protected:
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::ContextMenu) if (const auto callback = contextMenuRequested) {
            const auto* menu = static_cast<QContextMenuEvent*>(event);
            const auto global = menu->reason() == QContextMenuEvent::Keyboard
                ? mapToGlobal(boundingRect().center()).toPoint() : menu->globalPos();
            QQuickItem* source = this;
            if (menu->reason() == QContextMenuEvent::Keyboard) source = window()->activeFocusItem();
            else for (;;) {
                const auto local = source->mapFromGlobal(global);
                auto* child = source->childAt(local.x(), local.y());
                if (!child) break;
                source = child;
            }
            event->accept(); callback(global, source); return true;
        }
        return QQuickItem::event(event);
    }

    void geometryChange(const QRectF& now, const QRectF& previous) override
    {
        const QPointer<ContentColumnItem> self(this);
        QQuickItem::geometryChange(now, previous);
        if (self && now.size() != previous.size()) arrange();
    }

private:
    void arrange()
    {
        if (arranging_) { pending_ = true; return; }
        arranging_ = true;
        const QPointer<ContentColumnItem> self(this);
        do {
            pending_ = false;
            const auto items = items_;
            const auto barHeight = [this](const QPointer<QQuickItem>& item) {
                return item && item->parentItem() == this ? std::max(qreal(0), item->implicitHeight()) : qreal(0);
            };
            const qreal top = barHeight(items[0]), bottom = barHeight(items[2]);
            const qreal body = std::max(qreal(0), height() - top - bottom);
            const std::array<QRectF, 3> areas{QRectF(0, 0, width(), top), QRectF(0, top, width(), body),
                QRectF(0, top + body, width(), bottom)};
            for (int index = 0; index < 3; ++index) if (const auto& item = items[index]; item && item->parentItem() == this) {
                item->setPosition(areas[index].topLeft());
                if (!self) return;
                if (pending_) break;
                if (item) item->setSize(areas[index].size());
                if (!self) return;
                if (pending_) break;
            }
        } while (pending_);
        arranging_ = false;
    }
    std::array<QPointer<QQuickItem>, 3> items_;
    QSize bodyMinimum_{0, 0};
    bool arranging_ = false;
    bool pending_ = false;
};

TabContentItem::TabContentItem(WindowManager& manager, ContentHost& host, ContentBars& bars, TabId tab, QQuickItem* parent)
    : QQuickItem(parent), manager_(manager), host_(host), bars_(bars), tab_(tab),
      column_(new ContentColumnItem(this)), split_(new SplitTreeItem(column_))
{
    const auto theme = manager.interfaceTheme();
    split_->setDividerMetrics(std::max(1, theme.metrics.splitterWidth), 6);
    connect(this, &QQuickItem::widthChanged, column_, [this] { column_->setSize(size()); });
    connect(this, &QQuickItem::heightChanged, column_, [this] { column_->setSize(size()); });
    connect(split_, &SplitTreeItem::ratioRequested, this, [this](QUuid node, double ratio) {
        const auto* record = manager_.model().group(manager_.model().groupForTab(tab_));
        if (record) manager_.execute(QStringLiteral("layout.setRatio"), {{"node", node}, {"ratio", ratio}}, record->windowId);
    });
}

TabContentItem::~TabContentItem()
{
    // Content outlives this page; leave it to the host, unparented.
    for (auto it = frames_.cbegin(); it != frames_.cend(); ++it)
        if (it.value() && host_.item(it.key()) && host_.item(it.key())->parentItem() == it.value()) host_.unmount(it.key());
}

ContentColumnItem* TabContentItem::frame(PaneId pane)
{
    auto& frame = frames_[pane];
    if (!frame) {
        frame = new ContentColumnItem(split_);
        frame->contextMenuRequested = [this, pane](QPoint global, QQuickItem* source) { emit contextMenuRequested(tab_, pane, global, source); };
    }
    return frame;
}

void TabContentItem::sync()
{
    const QPointer<TabContentItem> self(this);
    const auto* record = manager_.model().tab(tab_);
    if (!record) return;
    const auto tree = record->panes;
    const auto* group = manager_.model().group(manager_.model().groupForTab(tab_));
    const auto state = manager_.windowViewState(group->windowId);
    const auto focused = state.mode == ViewMode::PaneFullscreen && state.tab == tab_ ? state.pane : PaneId{};
    const auto active = focused.isNull() ? record->activePane : focused;
    const auto options = manager_.tabOptions(tab_);
    const auto panes = manager_.model().panes(tab_);
    QHash<QUuid, QPointer<QQuickItem>> leaves;
    QHash<QUuid, QSize> minima;
    for (const auto pane : panes) {
        const QPointer<ContentColumnItem> target(frame(pane));
        leaves.insert(pane, target);
        if (host_.item(pane) && host_.item(pane)->parentItem() != target) host_.mount(pane, target);
        if (!self || !target) return;
        target->configure(!state.zen && options.topBarSharing == PaneBarSharing::PerPane ? bars_.pane(pane, true) : nullptr,
            host_.item(pane), !state.zen && options.bottomBarSharing == PaneBarSharing::PerPane ? bars_.pane(pane, false) : nullptr);
        if (!self || !target) return;
        minima.insert(pane, target->minimumSize());
    }
    for (auto it = frames_.begin(); it != frames_.end();) {
        if (panes.contains(it.key())) { ++it; continue; }
        if (it.value()) it.value()->deleteLater();
        it = frames_.erase(it);
    }
    split_->setTree(tree);
    if (!self) return;
    split_->setMinimumSizes(minima);
    if (!self) return;
    split_->setLeafItems(leaves);
    if (!self) return;
    split_->setFocusedItem(focused);
    if (!self) return;
    const auto shared = [&](bool top) -> QQuickItem* {
        if (state.zen) return nullptr;
        const auto sharing = top ? options.topBarSharing : options.bottomBarSharing;
        auto* paneBar = sharing == PaneBarSharing::Shared ? bars_.pane(active, top) : nullptr;
        return paneBar ? paneBar : bars_.tab(tab_, top);
    };
    column_->configure(shared(true), split_, shared(false), split_->minimumSize());
    if (self) column_->setSize(size());
}

QSize TabContentItem::minimumSize() const { return column_->minimumSize(); }

} // namespace window_system::quick
