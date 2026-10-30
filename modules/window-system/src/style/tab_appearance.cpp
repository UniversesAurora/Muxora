// SPDX-License-Identifier: MIT
#include "tab_appearance_p.hpp"
#include "../window_manager_p.hpp"
#include "../chrome/workspace_window.hpp"
#include "../docking/dock_surface.hpp"
#include <algorithm>

namespace window_system {

TabAppearance WindowManager::tabAppearance() const { return d->appearance; }

bool WindowManager::setTabAppearance(const TabAppearance& appearance)
{
    if (!validTabAppearance(appearance)) return false;
    if (d->appearance == appearance) return true;
    d->appearance = appearance;
    // Relabel the built strips in place when they still match the model; a
    // full refresh would also close peeks and revisit every tab's view.
    bool inPlace = !d->refreshing && !d->destroying;
    for (auto window : std::as_const(d->windows))
        if (inPlace && window) inPlace = window->surface()->updateTabAppearance();
    if (!inPlace) { refresh(); return true; }
    emit layoutChanged();
    return true;
}

std::optional<TabColorPreset> WindowManager::tabColorPreset(const QString& id) const
{
    if (id.isEmpty()) return {};
    const auto it = std::find_if(d->appearance.colors.cbegin(), d->appearance.colors.cend(),
        [&id](const TabColorPreset& preset) { return preset.id == id; });
    return it == d->appearance.colors.cend() ? std::optional<TabColorPreset>{} : *it;
}

} // namespace window_system
