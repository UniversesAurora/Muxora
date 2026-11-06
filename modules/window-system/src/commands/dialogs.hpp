// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/window_manager.hpp>

namespace window_system {

// Asks for one line of text over the window without blocking, as
// QInputDialog::getText does: `accepted` receives the text. The dialog closes
// once `applies` returns false. False when no dialog could open.
bool askForText(WindowManager& manager, WindowId window, const QString& name, const QString& title, const QString& label,
                const QString& text, std::function<bool()> applies, std::function<void(const QString&)> accepted);

} // namespace window_system
