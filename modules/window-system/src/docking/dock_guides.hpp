// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/layout_model.hpp>
#include <QRect>
#include <QVector>

namespace window_system {

struct DockGuide {
    QRect rect;
    Placement placement = Placement::Center;
    bool toEdge = false;
    Edge edge = Edge::Left;
    bool pinned = false;
};

inline QVector<DockGuide> dockGuides(QRect bounds, QRect group, bool splitGroup = true)
{
    constexpr int cell = 28;
    constexpr int step = 32;
    QVector<DockGuide> guides;
    if (!group.isEmpty()) {
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
    for (const auto edge : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom}) {
        QPoint outer, inward;
        switch (edge) {
        case Edge::Left: outer = {bounds.left() + 6, bounds.center().y() - cell / 2}; inward = {cell, 0}; break;
        case Edge::Right: outer = {bounds.right() - cell - 5, bounds.center().y() - cell / 2}; inward = {-cell, 0}; break;
        case Edge::Top: outer = {bounds.center().x() - cell / 2, bounds.top() + 6}; inward = {0, cell}; break;
        case Edge::Bottom: outer = {bounds.center().x() - cell / 2, bounds.bottom() - cell - 5}; inward = {0, -cell}; break;
        }
        guides.append({QRect(outer, QSize(cell, cell)), Placement::Center, true, edge, false});
        guides.append({QRect(outer + inward, QSize(cell, cell)), Placement::Center, true, edge, true});
    }
    return guides;
}

} // namespace window_system
