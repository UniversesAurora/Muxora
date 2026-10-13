// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>
#include "command_strip.hpp"
#include "workspace_window.hpp"

#include <QJsonArray>

namespace window_system {

bool WindowManager::moveToolbar(WindowId window, const QString& stripId,
                                ToolbarArea area, int row, int index)
{
    if (row < 0 || index < -1 || (area != ToolbarArea::Top && area != ToolbarArea::Bottom)) return false;
    auto* view = static_cast<WorkspaceWindow*>(windowWidget(window));
    if (!view || !view->strips() || !view->strips()->stripWidget(stripId)) return false;
    auto* strips = view->strips();
    const auto rows = strips->saveLayout().value(area == ToolbarArea::Top ? "rows" : "bottomRows").toArray();
    // A new row can only be appended. Reject gaps and untrusted huge indices
    // before the engine creates any row widgets or cancels an active drag.
    if (row > rows.size()) return false;
    const int count = row < rows.size() ? int(rows[row].toArray().size()) : 0;
    if (index > count) return false;
    return strips->moveStrip(stripId, area == ToolbarArea::Top ? CommandStripHost::Area::Top : CommandStripHost::Area::Bottom,
                             row, index < 0 ? count : index);
}

} // namespace window_system
