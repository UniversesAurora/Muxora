// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/layout_model.hpp>
#include <QHash>
#include <QRect>
#include <QVector>
#include <algorithm>
#include <limits>

namespace window_system {

struct DockGuide {
    QRect rect;
    Placement placement = Placement::Center;
    bool toEdge = false;
    Edge edge = Edge::Left;
    bool pinned = false;
};

// Keep a side's normal coordinate fixed. Labels which cross its center only
// move the pair along that edge, never across a whole label row into the body.
inline QRect placeEdgeGuidePair(QRect pair, const QRect& available, const QList<QRect>& obstacles, bool vertical)
{
    if (!available.contains(pair)) return {};
    const int length = vertical ? pair.height() : pair.width();
    const int low = vertical ? available.top() : available.left();
    const int high = (vertical ? available.bottom() : available.right()) - length + 1;
    const int preferred = vertical ? pair.top() : pair.left();
    QVector<QPair<int, int>> blocked;
    for (const auto& obstacle : obstacles) {
        const bool crosses = vertical ? obstacle.left() <= pair.right() && obstacle.right() >= pair.left()
                                      : obstacle.top() <= pair.bottom() && obstacle.bottom() >= pair.top();
        if (!crosses) continue;
        const int start = (vertical ? obstacle.top() : obstacle.left()) - length - 3;
        const int end = (vertical ? obstacle.bottom() : obstacle.right()) + 4;
        if (end >= low && start <= high) blocked.append({std::max(low, start), std::min(high, end)});
    }
    std::sort(blocked.begin(), blocked.end());
    int next = low; int best = preferred; int distance = std::numeric_limits<int>::max();
    auto consider = [&](int begin, int end) {
        if (begin > end) return;
        const int candidate = std::clamp(preferred, begin, end);
        const int cost = std::abs(candidate - preferred);
        if (cost < distance || (cost == distance && candidate < best)) { best = candidate; distance = cost; }
    };
    for (const auto& range : blocked) { consider(next, range.first - 1); next = std::max(next, range.second + 1); }
    consider(next, high);
    if (distance == std::numeric_limits<int>::max()) return {};
    if (vertical) pair.moveTop(best); else pair.moveLeft(best);
    return pair;
}

inline QVector<DockGuide> dockGuides(QRect bounds, QRect group, bool splitGroup = true,
                                    const QList<QRect>& strips = {}, const QHash<Edge, QRect>& edgeStrips = {})
{
    QVector<DockGuide> guides;
    // Keep group controls below its real header, including narrow/short groups.
    // Rendering and hit testing consume the same resulting rectangles.
    for (const auto& strip : strips)
        if (strip.intersects(group) && strip.top() <= group.top() + 1)
            group.setTop(std::max(group.top(), strip.bottom() + 1));
    if (!group.isEmpty()) {
        const int cell = std::clamp((std::min(group.width(), group.height()) - 8) / 3, 12, 28);
        const int step = cell + 4;
        for (const auto placement : {Placement::Center, Placement::Left, Placement::Right, Placement::Top, Placement::Bottom}) {
            if (!splitGroup && placement != Placement::Center) continue;
            QPoint offset;
            switch (placement) {
            case Placement::Left: offset = {-step, 0}; break;
            case Placement::Right: offset = {step, 0}; break;
            case Placement::Top: offset = {0, -step}; break;
            case Placement::Bottom: offset = {0, step}; break;
            case Placement::Center: break;
            }
            guides.append({QRect(group.center() + offset - QPoint(cell / 2, cell / 2), QSize(cell, cell)), placement});
        }
    }
    QRect available = bounds;
    for (auto it = edgeStrips.cbegin(); it != edgeStrips.cend(); ++it) {
        if (it.value().isEmpty()) continue;
        switch (it.key()) {
        case Edge::Left: available.setLeft(std::max(available.left(), it.value().right() + 1)); break;
        case Edge::Right: available.setRight(std::min(available.right(), it.value().left() - 1)); break;
        case Edge::Top: available.setTop(std::max(available.top(), it.value().bottom() + 1)); break;
        case Edge::Bottom: available.setBottom(std::min(available.bottom(), it.value().top() - 1)); break;
        }
    }
    const int originalTop = available.top();
    int contentTop = originalTop;
    for (const auto& strip : strips)
        if (strip.intersects(available) && strip.top() <= originalTop + 1) contentTop = std::max(contentTop, strip.bottom() + 1);
    available.setTop(contentTop);
    if (available.isEmpty()) return guides;
    const auto obstacles = strips + edgeStrips.values();
    constexpr int cell = 28;
    for (const auto edge : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom}) {
        QPoint outer, inward;
        switch (edge) {
        case Edge::Left: outer = {available.left() + 6, available.center().y() - cell / 2}; inward = {cell, 0}; break;
        case Edge::Right: outer = {available.right() - cell - 5, available.center().y() - cell / 2}; inward = {-cell, 0}; break;
        case Edge::Top: outer = {available.center().x() - cell / 2, available.top() + 6}; inward = {0, cell}; break;
        case Edge::Bottom: outer = {available.center().x() - cell / 2, available.bottom() - cell - 5}; inward = {0, -cell}; break;
        }
        const QRect initial = QRect(outer, QSize(cell, cell)).united(QRect(outer + inward, QSize(cell, cell)));
        const auto placed = placeEdgeGuidePair(initial, available, obstacles, edge == Edge::Left || edge == Edge::Right);
        if (placed.isEmpty()) continue;
        outer += placed.topLeft() - initial.topLeft();
        guides.append({QRect(outer, QSize(cell, cell)), Placement::Center, true, edge, false});
        guides.append({QRect(outer + inward, QSize(cell, cell)), Placement::Center, true, edge, true});
    }
    return guides;
}

} // namespace window_system
