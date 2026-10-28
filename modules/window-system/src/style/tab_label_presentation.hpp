// SPDX-License-Identifier: MIT
#pragma once

#include "elevation.hpp"
#include "label_presence.hpp"
#include "tab_label_geometry.hpp"
#include <window_system/tab_appearance.hpp>
#include <QFont>
#include <QIcon>
#include <QRect>
#include <QString>
#include <optional>

namespace window_system {

struct TabLabelState {
    TabShape shape = TabShape::Top;
    bool selected = false;
    bool hovered = false;
    bool pressed = false;
    bool groupFocused = false;
    bool windowActive = true;
    qreal titleOpacity = 1.0;
    bool sticky = false;
    bool compactSticky = false;
    // Only the icon, or the title's initial without one, centered.
    bool iconOnly = false;
    bool lifted = false;
    bool elevationManaged = false;
    // 0..1 while a lifted tab's shadow fades in.
    qreal shadowOpacity = 1.0;
    // A row of a strip right of its content: the outer side is the right.
    bool outerRight = false;
    bool enabled = true;
    bool actionShown = false;
    bool actionHovered = false;
    bool actionPressed = false;
    // 0..1 while a covering action fades in or out.
    qreal actionOpacity = 1.0;
    // The action has room of its own and covers nothing.
    bool actionReserved = false;
    QString number;
    QIcon icon;
    QString title;
    QIcon actionIcon;
};

// Owns every descriptor needed after the source label or window is destroyed.
// Icons remain GUI-thread resource descriptions: a Quick adapter must prepare
// their textures on the GUI thread, never invoke an icon engine while rendering.
// Text-layout caches belong to each renderer and do not enter this snapshot.
struct TabLabelPresentation {
    TabParts parts;
    TabLabelState state;
    QFont font;
    Qt::LayoutDirection direction = Qt::LeftToRight;
    LabelPresencePose presence;
    std::optional<SurfaceGeometry> surface;
    std::optional<ElevationProfile> bodyElevation;
    std::optional<TabColorPreset> preset;
};

} // namespace window_system
