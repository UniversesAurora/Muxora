// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/layout_model.hpp>
#include "dock_guides.hpp"
#include "tab_layout.hpp"

#include <QHash>
#include <QRect>
#include <QVector>
#include <functional>
#include <optional>

namespace window_system {

struct DropTarget {
    WindowId window;
    GroupId group;
    Placement placement = Placement::Center;
    bool toEdge = false;
    bool toEdgeBar = false;
    bool pinned = false;
    bool accepted = false;
    bool splitGroup = true;
    Edge edge = Edge::Left;
    QRect preview;
    QRect groupRect;
    QRect windowRect;
    int insertionIndex = -1;
    // Whether the strip showed the insertion among its sticky tabs.
    bool insertionSticky = false;
    // Whether the pointer is over the strip's own row of sticky tabs.
    std::optional<bool> insertionRow;
    // The tab folder a dropped tab joins; null for none.
    FolderId insertionFolder;
    QVector<DockGuide> guides;
};

// What a window's docking area tells a drag about itself, in global
// coordinates. The Widgets and Quick surfaces both answer these, so drop
// targets follow one rule set.
class DropSurface {
public:
    virtual ~DropSurface() = default;
    [[nodiscard]] virtual QRect globalRect() const = 0;
    // Visible groups only.
    [[nodiscard]] virtual QHash<GroupId, QRect> groupRects() const = 0;
    [[nodiscard]] virtual QRect groupContentRect(GroupId group) const = 0;
    // The central area, or the focused group when focus mode shows only it.
    [[nodiscard]] virtual QRect centralRectGlobal() const = 0;
    [[nodiscard]] virtual QHash<GroupId, QRect> tabBarRects() const = 0;
    [[nodiscard]] virtual QVector<DockStrip> dockStrips() const = 0;
    [[nodiscard]] virtual int tabInsertionIndex(GroupId group, QPoint global, TabLayoutInput::Carried carried) const = 0;
    [[nodiscard]] virtual std::optional<bool> tabInsertionRow(GroupId group, QPoint global) const = 0;
    // Visible side bars.
    [[nodiscard]] virtual QHash<Edge, QRect> edgeBarRects() const = 0;
    [[nodiscard]] virtual int edgeTabInsertionIndex(Edge edge, QPoint global, QRect carriedGlobal) const = 0;
    [[nodiscard]] virtual QRect edgeTabInsertionRect(Edge edge, int index) const = 0;
};

struct DropQuery {
    QPoint global;
    TabLayoutInput::Carried carried = TabLayoutInput::Carried::Tab;
    // The carried label's rectangle when it is shown as a side bar button.
    std::function<QRect(Edge)> carriedEdgeRect;
    // The target whose guides are on screen, if any: its cross keeps the
    // pointer until it leaves the cross's bounds.
    const DropTarget* shown = nullptr;
    int edgeThickness = 0;
};

// The drop target under a point of one window's surface; `window` stays
// for the caller to fill. Not accepted over content without a guide.
[[nodiscard]] DropTarget dropTargetAt(const DropSurface& surface, const LayoutModel& model, const DropQuery& query);

} // namespace window_system
