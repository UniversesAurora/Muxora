// SPDX-License-Identifier: MIT
#pragma once

#include <QAction>
#include <QMenu>

// Hovers a menu item the way a pointer does: the item becomes current and
// QMenu opens its submenu through the same delayed popup path as a hover.
// Synthetic pointer moves are not reliable here: QMenu ignores moves until the
// pointer has travelled the drag distance from where the menu opened (which
// depends on the real cursor), and delays switching while the pointer heads
// towards an open submenu.
inline void hoverMenuItem(QMenu* menu, QAction* action)
{
    menu->setActiveAction(action);
}
