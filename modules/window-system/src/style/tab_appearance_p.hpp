// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/tab_appearance.hpp>
#include <window_system/layout_model.hpp>
#include <QJsonObject>
#include <QJsonValue>

namespace window_system {
[[nodiscard]] QJsonObject tabAppearanceToJson(const TabAppearance& appearance);
[[nodiscard]] OperationResult parseTabAppearance(const QJsonValue& value, TabAppearance& appearance);
[[nodiscard]] bool validTabAppearance(const TabAppearance& appearance);
// Translate only untouched built-in labels. Host/user preset names are content.
[[nodiscard]] QString tabColorDisplayName(const TabColorPreset& preset);
} // namespace window_system
