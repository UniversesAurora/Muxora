// SPDX-License-Identifier: MIT
#pragma once

class QQmlComponent;

namespace window_system::quick {

enum class SceneComponent { Mask, PathStroke };

// Both retained components share one GUI-thread engine. Components and the
// engine are owned by the application; created items belong to their callers.
[[nodiscard]] QQmlComponent& sceneComponent(SceneComponent kind);

} // namespace window_system::quick
