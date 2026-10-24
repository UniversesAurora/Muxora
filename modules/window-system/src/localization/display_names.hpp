// SPDX-License-Identifier: MIT
#pragma once

#include <QString>

namespace window_system {

// Built-in category/menu identities are stable host API values, not source
// text. Unrecognized names belong to the host or user and are returned as-is.
[[nodiscard]] QString commandCategoryDisplayName(const QString& identity);

} // namespace window_system
