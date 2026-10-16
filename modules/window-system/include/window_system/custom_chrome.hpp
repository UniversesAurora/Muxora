// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/toolbar_layout.hpp>
#include <QString>
#include <QStringList>

namespace window_system {

// Manager-owned definitions are recreated for every window kind. Per-window
// visibility, sizing and position remain part of the ordinary layout snapshot.
// Buttons invoke registered commands; definitions never retain QWidget pointers.
struct CommandToolbar {
    QString id;
    QString title;
    QStringList commands;
    ToolbarArea area = ToolbarArea::Top;
    int row = 1;
    bool operator==(const CommandToolbar &) const = default;
};

struct ToolbarState {
    ToolbarArea area = ToolbarArea::Top;
    int row = 0;
    int index = 0;
    int width = -1; // Requested logical width; -1 means the natural width.
    bool visible = true;
    bool elastic = false;
    bool locked = true;
    QStringList hiddenItems;
};

struct StatusItem {
    QString id;
    QString text;
    QString tooltip;
    bool operator==(const StatusItem &) const = default;
};

} // namespace window_system
