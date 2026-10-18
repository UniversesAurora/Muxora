// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/view_state.hpp>

namespace window_system {

[[nodiscard]] bool validViewMode(ViewMode mode);
[[nodiscard]] bool validZenFullscreenMode(ViewMode mode);
OperationResult validateSavedWindowViewState(const QJsonValue& value, const LayoutModel& model,
                                            WindowId window);

} // namespace window_system
