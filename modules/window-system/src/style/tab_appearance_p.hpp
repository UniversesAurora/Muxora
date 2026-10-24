// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/tab_appearance.hpp>
#include <window_system/layout_model.hpp>
#include <QJsonObject>
#include <QJsonValue>
#include <optional>

namespace window_system {
[[nodiscard]] QJsonObject tabAppearanceToJson(const TabAppearance& appearance);
[[nodiscard]] OperationResult parseTabAppearance(const QJsonValue& value, TabAppearance& appearance);
[[nodiscard]] bool validTabAppearance(const TabAppearance& appearance);
// The settings and JSON names of a sizing; empty for an out-of-range value.
[[nodiscard]] QString stickySizingName(StickyTabSizing sizing);
[[nodiscard]] std::optional<StickyTabSizing> stickySizingFromName(const QString& name);
// Translate only untouched built-in labels. Host/user preset names are content.
[[nodiscard]] QString tabColorDisplayName(const TabColorPreset& preset);
} // namespace window_system
