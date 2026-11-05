// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/command_registry.hpp>
#include <window_system/layout_model.hpp>
#include <QStringList>
#include <QVector>

namespace window_system {
class WindowManager;

// One row of the command palette or the window selector, whichever
// presentation lists it. A disabled row stays listed but cannot be chosen.
struct PanelRow {
    QString id;
    QStringList cells;
    QString tooltip;
    bool enabled = true;
    QString iconName;
};

// Every query term must appear in the row's searchable text.
[[nodiscard]] bool matchesPanelSearch(const QString& searchable, const QString& query);
// Commands matching the query, in catalog order; those the context does not
// allow are disabled. Empty when a host query releases the manager.
[[nodiscard]] QVector<PanelRow> commandPaletteRows(WindowManager* manager, const CommandContext& context, const QString& query);
[[nodiscard]] bool canRunFromPalette(WindowManager* manager, const QString& command, const CommandContext& context);
// All workbench and floating windows matching the query, in window order.
[[nodiscard]] QVector<PanelRow> windowSelectorRows(WindowManager* manager, const QString& query);

} // namespace window_system
