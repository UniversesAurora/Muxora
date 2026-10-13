// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/view_state.hpp>

namespace window_system {

[[nodiscard]] bool validViewMode(ViewMode mode);
[[nodiscard]] QJsonObject windowViewStateToJson(const WindowViewState& state);
OperationResult parseWindowViewState(const QJsonValue& value, const LayoutModel& model,
                                    WindowId window, WindowViewState& state);

} // namespace window_system
