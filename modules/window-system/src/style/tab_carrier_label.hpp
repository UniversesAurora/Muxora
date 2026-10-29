// SPDX-License-Identifier: MIT
#pragma once

#include "docking/tab_drag_visual.hpp"

namespace window_system {

// Both adapters consume the same source metadata and per-frame sample. The
// supplied attachment geometry is already in this carrier piece's coordinates.
[[nodiscard]] inline TabLabelPresentation carrierLabelPresentation(
    const TabCarrierLabel& source, const TabCarrierSample& sample,
    const SurfaceGeometry* geometry)
{
    auto label = source.label;
    label.parts = sample.parts;
    label.state.titleOpacity = sample.titleOpacity;
    label.state.actionOpacity = sample.actionOpacity;
    label.state.lifted = geometry != nullptr;
    label.state.elevationManaged = true;
    label.surface = label.state.selected && geometry ? std::optional{*geometry} : std::nullopt;
    return label;
}

} // namespace window_system
