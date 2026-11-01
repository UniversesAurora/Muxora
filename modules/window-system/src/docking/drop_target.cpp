// SPDX-License-Identifier: MIT
#include "drop_target.hpp"

#include <QCoreApplication>
#include <algorithm>

namespace window_system {
namespace {
template <typename Key>
Key keyAt(const QHash<Key, QRect>& rects, QPoint point)
{
    for (auto it = rects.cbegin(); it != rects.cend(); ++it) if (it.value().contains(point)) return it.key();
    return {};
}
} // namespace

DropTarget dropTargetAt(const DropSurface& surface, const LayoutModel& model, const DropQuery& query)
{
    const auto global = query.global;
    DropTarget result;
    result.windowRect = surface.globalRect();
    if (!result.windowRect.contains(global)) return {};
    const auto groups = surface.groupRects();
    result.group = keyAt(groups, global);
    bool retainedCross = false;
    if (query.shown && groups.contains(query.shown->group)) {
        QRect cross;
        for (const auto& guide : query.shown->guides) if (!guide.toEdge) cross = cross.united(guide.rect);
        // The cross can extend over a neighbouring group after avoiding edge
        // targets. Its arms and the gaps between them belong to the same
        // control until the pointer leaves the complete displayed bounds.
        retainedCross = cross.contains(global);
        if (retainedCross) result.group = query.shown->group;
    }
    if (const auto* group = model.group(result.group)) result.splitGroup = group->location != GroupLocation::AutoHide;
    result.groupRect = groups.value(result.group, surface.centralRectGlobal());
    if (result.groupRect.isEmpty()) result.groupRect = result.windowRect;
    // A displayed side bar is an insertion strip along its full axis, including
    // the area where the outer guide would otherwise overlap that strip.
    const auto edgeBars = surface.edgeBarRects();
    for (auto it = edgeBars.cbegin(); it != edgeBars.cend(); ++it) if (!retainedCross && it.value().contains(global)) {
        result.accepted = true; result.toEdge = true; result.edge = it.key(); result.pinned = false;
        result.toEdgeBar = true;
        result.insertionIndex = surface.edgeTabInsertionIndex(it.key(), global,
            query.carriedEdgeRect ? query.carriedEdgeRect(it.key()) : QRect{});
        break;
    }
    // A visible tab strip is an insertion target. Content bodies and invisible
    // edge margins deliberately have no implicit drop operation.
    const auto bars = surface.tabBarRects();
    const auto barGroup = keyAt(bars, global);
    if (!retainedCross && !result.accepted && !barGroup.isNull()) {
        result.accepted = true; result.group = barGroup; result.toEdge = false;
        result.placement = Placement::Center;
        result.groupRect = groups.value(barGroup);
        result.insertionIndex = surface.tabInsertionIndex(barGroup, global, query.carried);
        result.insertionRow = surface.tabInsertionRow(barGroup, global);
    }
    // The content geometry already excludes strips on any side. Treating a
    // full-height vertical strip as a top header reduces the guide area to
    // the footer and incorrectly shrinks and lowers the group cross.
    auto guideArea = surface.groupContentRect(result.group);
    if (guideArea.isEmpty()) guideArea = result.groupRect;
    result.guides = dockGuides(result.windowRect, guideArea, result.splitGroup, surface.dockStrips(), edgeBars, DockGuideArea::Content);
    for (const auto& guide : result.guides) if (!result.accepted && guide.rect.contains(global)) {
        result.accepted = true; result.toEdge = guide.toEdge; result.pinned = guide.pinned;
        result.edge = guide.edge; result.placement = guide.placement; break;
    }
    if (!result.accepted) return result;
    result.preview = result.groupRect;
    if (result.toEdge) {
        const auto bounds = result.windowRect;
        const int extent = result.pinned ? 160 : query.edgeThickness;
        switch (result.edge) {
        case Edge::Left: result.preview = QRect(bounds.topLeft(), QSize(extent, bounds.height())); break;
        case Edge::Right: result.preview = QRect(bounds.right() - extent + 1, bounds.top(), extent, bounds.height()); break;
        case Edge::Top: result.preview = QRect(bounds.topLeft(), QSize(bounds.width(), extent)); break;
        case Edge::Bottom: result.preview = QRect(bounds.left(), bounds.bottom() - extent + 1, bounds.width(), extent); break;
        }
        if (result.toEdgeBar) result.preview = surface.edgeTabInsertionRect(result.edge, result.insertionIndex);
    } else {
        const auto center = result.preview.center();
        switch (result.placement) {
        case Placement::Left: result.preview.setWidth(result.preview.width() / 2); break;
        case Placement::Right: result.preview.setLeft(center.x()); break;
        case Placement::Top: result.preview.setHeight(result.preview.height() / 2); break;
        case Placement::Bottom: result.preview.setTop(center.y()); break;
        case Placement::Center: break;
        }
        if (result.insertionIndex >= 0) {
            const auto strip = bars.value(result.group);
            result.preview = QRect(global.x() - 2, strip.top(), 4, strip.height());
        }
    }
    return result;
}

bool selectsGuide(const DropTarget& target, const DockGuide& guide)
{
    return target.accepted && target.insertionIndex < 0 && target.toEdge == guide.toEdge
        && (guide.toEdge ? target.edge == guide.edge && target.pinned == guide.pinned : target.placement == guide.placement);
}

QRect guideMark(const DockGuide& guide)
{
    const auto square = guide.rect;
    const int padding = std::clamp(square.width() / 5, 2, 6);
    auto inner = square.adjusted(padding, padding, -padding, -padding);
    if (guide.toEdge) {
        const int thickness = guide.pinned ? 8 : 3;
        if (guide.edge == Edge::Left) inner.setWidth(thickness);
        if (guide.edge == Edge::Right) inner.setLeft(inner.right() - thickness);
        if (guide.edge == Edge::Top) inner.setHeight(thickness);
        if (guide.edge == Edge::Bottom) inner.setTop(inner.bottom() - thickness);
    } else {
        if (guide.placement == Placement::Left) inner.setWidth(inner.width() / 2);
        if (guide.placement == Placement::Right) inner.setLeft(inner.center().x());
        if (guide.placement == Placement::Top) inner.setHeight(inner.height() / 2);
        if (guide.placement == Placement::Bottom) inner.setTop(inner.center().y());
    }
    return inner;
}

QString dropCaption(const DropTarget& target, int count, bool whole)
{
    if (target.toEdgeBar) return QCoreApplication::translate("WindowSystem", "Insert %n sidebar tabs", nullptr, count);
    if (target.toEdge) return target.pinned ? QCoreApplication::translate("WindowSystem", "Pin in sidebar")
                                            : QCoreApplication::translate("WindowSystem", "Auto-hide on sidebar");
    if (target.insertionIndex >= 0) return QCoreApplication::translate("WindowSystem", "Insert %n tabs", nullptr, count);
    if (target.placement == Placement::Center) return QCoreApplication::translate("WindowSystem", "Merge %n tabs", nullptr, count);
    return whole ? QCoreApplication::translate("WindowSystem", "Dock while preserving tab groups")
                 : QCoreApplication::translate("WindowSystem", "Create adjacent tab group");
}

} // namespace window_system
