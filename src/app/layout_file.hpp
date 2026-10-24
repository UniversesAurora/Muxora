// SPDX-License-Identifier: MIT
#pragma once

#include <QString>

namespace window_system { class WindowManager; }

namespace muxora {

struct LayoutFileRestore {
    bool restored = false; // The windows and tabs came back from the file.
    QString message;       // Tells the user what was lost; empty when nothing was.
};

// Restores the workspace from the saved layout file. When only the settings
// stored with the layout fail validation, those sections fall back to their
// defaults and the windows and tabs are still restored. Whenever anything is
// dropped, the file is first copied next to itself, so the following save
// cannot destroy the only copy.
[[nodiscard]] LayoutFileRestore restoreLayoutFile(window_system::WindowManager& windows, const QString& path);

} // namespace muxora
