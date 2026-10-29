// SPDX-License-Identifier: MIT
#pragma once

#include "tab_label_presentation.hpp"
#include "tab_visual_style.hpp"

#include <algorithm>
#include <cmath>

namespace window_system {

// Text color is shared by raster and scene-graph adapters. Preset colors own
// the selected/inactive split; otherwise emphasis follows interaction state.
[[nodiscard]] inline QColor tabLabelTextColor(const TabLabelPresentation& label,
                                               const TabPalette& palette)
{
    const auto& state = label.state;
    if (label.preset)
        return (state.selected ? label.preset->groupActive : label.preset->groupInactive).text;
    return state.selected || state.hovered || state.lifted ? palette.text : palette.mutedText;
}

// The title keeps its complete shaped source and is clipped horizontally.
// Opacity is split into the static cut fade and the action-cover fade so a
// retained renderer can animate the latter without reshaping the text.
struct TabTitleFade {
    qreal left = 0;
    qreal right = 0;
    bool cut = false;
    int fadeLength = 0;
    bool covering = false;
    bool reverse = false;
    qreal actionEnd = 0;
    qreal actionFade = 1;
    qreal actionOpacity = 0;

    [[nodiscard]] bool active() const
    {
        return (cut && fadeLength > 0) || covering;
    }

    [[nodiscard]] qreal cutOpacityAt(qreal x) const
    {
        if (!cut || fadeLength <= 0 || right <= left) return 1;
        return std::clamp((right - x) / fadeLength, 0.0, 1.0);
    }

    [[nodiscard]] qreal coverOpacityAt(qreal x) const
    {
        if (!covering || actionFade <= 0) return 1;
        const qreal cover = reverse ? (actionEnd + actionFade - x) / actionFade
                                    : (x - actionEnd + actionFade) / actionFade;
        return 1 - std::clamp(actionOpacity, 0.0, 1.0) * std::clamp(cover, 0.0, 1.0);
    }

    [[nodiscard]] qreal opacityAt(qreal x) const
    {
        return cutOpacityAt(x) * coverOpacityAt(x);
    }
};

[[nodiscard]] inline TabTitleFade tabLabelTitleFade(const TabLabelPresentation& label,
                                                     const TabMetrics& metrics)
{
    const auto& state = label.state;
    const auto& title = label.parts.title;
    if (state.iconOnly || state.shape == TabShape::Tile || title.isEmpty()) return {};
    TabTitleFade fade;
    fade.left = title.left();
    fade.right = title.right() + 1.0;
    fade.cut = label.naturalWidth > title.width();
    fade.fadeLength = state.shape == TabShape::Row
        ? std::min(metrics.titleFade,
                   std::max(0, title.width() - (metrics.rowHeight - uiPixels(10))))
        : metrics.titleFade;
    fade.covering = state.actionShown && !state.actionReserved && !label.parts.action.isEmpty()
        && state.actionOpacity > 0;
    fade.reverse = label.parts.action.left() < label.parts.tab.center().x();
    fade.actionEnd = fade.reverse ? label.parts.action.right() + 1.0 : label.parts.action.left();
    fade.actionFade = std::max(1, metrics.edgeFade);
    fade.actionOpacity = state.actionOpacity;
    return fade;
}

} // namespace window_system
