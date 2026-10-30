// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/layout_model.hpp>
#include <QVariantMap>
#include <optional>

namespace window_system {

enum class PaneBarSharing { Shared, PerPane };

struct TabOptions {
    QString title;
    QString contentType;
    QVariantMap state;
    bool showIcon = true;
    PaneBarSharing topBarSharing = PaneBarSharing::Shared;
    PaneBarSharing bottomBarSharing = PaneBarSharing::Shared;
    // Opens the tab at the end of this tab folder. Without one, a tab opened
    // beside the active tab joins the active tab's folder (tab strip design 12).
    QUuid folder;
};

struct ContentVisibilityChange {
    PaneId pane;
    bool visible = false;
};

// Window includes its floating descendants; FloatingWindow separates each
// top-level window. Input states are process-local and never serialized.
enum class InputMethodScope { Application, Window, FloatingWindow, TabGroup, Tab, Pane };
[[nodiscard]] QString inputMethodScopeKey(InputMethodScope scope);
[[nodiscard]] std::optional<InputMethodScope> inputMethodScopeFromKey(const QString& key);

} // namespace window_system
