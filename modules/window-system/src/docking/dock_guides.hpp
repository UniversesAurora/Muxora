// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/layout_model.hpp>
#include <QRect>
#include <QVector>
#include <algorithm>

namespace window_system {

struct DockGuide {
    QRect rect;
    Placement placement = Placement::Center;
    bool toEdge = false;
    Edge edge = Edge::Left;
    bool pinned = false;
};

inline QVector<DockGuide> dockGuides(QRect bounds, QRect group, bool splitGroup = true,
                                    const QList<QRect>& strips = {})
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
    constexpr int cell = 28;
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
    for (int index = guides.size() - 8; index < guides.size(); index += 2) {
        const auto edge = guides[index].edge;
        for (qsizetype pass = 0; pass <= strips.size(); ++pass) {
            const auto pair = guides[index].rect.united(guides[index + 1].rect);
            QPoint shift;
            for (const auto& strip : strips) if (pair.intersects(strip)) {
                if (edge == Edge::Left) shift.setX(std::max(shift.x(), strip.right() + 1 - pair.left()));
                if (edge == Edge::Right) shift.setX(std::min(shift.x(), strip.left() - 1 - pair.right()));
                if (edge == Edge::Top) shift.setY(std::max(shift.y(), strip.bottom() + 1 - pair.top()));
                if (edge == Edge::Bottom) shift.setY(std::min(shift.y(), strip.top() - 1 - pair.bottom()));
            }
            if (shift.isNull()) break;
            guides[index].rect.translate(shift); guides[index + 1].rect.translate(shift);
        }
    }
    return guides;
}

} // namespace window_system
