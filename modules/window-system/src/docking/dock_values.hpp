// SPDX-License-Identifier: MIT
#pragma once

#include "tab_strip_controller.hpp"
#include <window_system/layout_model.hpp>

namespace window_system {

// With each group's own card margin, cards stay 6 from the window's edges.
inline constexpr int surfaceOuterMargin = 4;

// A strip's folders: runs of adjacent tabs of one folder, in strip order.
[[nodiscard]] inline QVector<TabStripFolder> stripFolders(const LayoutModel& model, const GroupRecord& group)
{
    QVector<TabStripFolder> result;
    for (int index = 0; index < group.tabs.size(); ++index) {
        const auto* tab = model.tab(group.tabs.at(index));
        if (!tab || tab->folder.isNull()) continue;
        if (!result.isEmpty() && result.last().id == tab->folder) { ++result.last().count; continue; }
        const auto* folder = model.folder(tab->folder);
        if (!folder) continue;
        result.append({folder->id, folder->name, folder->colorId, folder->collapsed, index, 1});
    }
    return result;
}

} // namespace window_system
