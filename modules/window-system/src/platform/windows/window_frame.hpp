// SPDX-License-Identifier: MIT
#pragma once

#include <QtGlobal>
#include "window_menu.hpp"

class QWidget;

namespace window_system {

// Call once while the completed top-level widget is still hidden. Preparing its
// HWND before the first show lets DWM own native window transitions; subsequent
// calls are idempotent and do not start an animation. Qt continues to own window
// visibility and state. Other platforms keep their ordinary Qt behavior.
void applyNativeFrame(QWidget* window);
bool handleNativeFrame(QWidget* window, void* message, qintptr* result);
// Native state can change before Qt delivers its queued state/geometry events.
[[nodiscard]] bool hasNormalNativePlacement(const QWidget* window);

// Internal diagnostic for the write-only DWM transition attribute. This records
// the actual native call for the current HWND, not an inferred animation state.
struct NativeTransitionConfiguration {
    bool attempted = false;
    qint32 result = 0;
    bool forcedDisabled = false;
};
[[nodiscard]] NativeTransitionConfiguration nativeTransitionConfiguration(QWidget* window);

// Diagnostics describe the attributes actually written for this HWND. A cached
// repeated preparation has the same revision; composition changes refresh it.
struct NativeFrameConfiguration {
    bool attempted = false;
    bool compact = false;
    bool nonClientEnabled = false;
    qint32 nonClientResult = 0;
    qint32 marginsResult = 0;
    quint64 revision = 0;
};
[[nodiscard]] NativeFrameConfiguration nativeFrameConfiguration(QWidget* window);

// Counts the custom-frame paint guards reached by real native messages. The
// requested DWM policy does not imply NC composition for a shaped HWND.
struct NativePaintGuardActivity {
    quint64 paintsSuppressed = 0;
    quint64 activationsSuppressed = 0;
    quint64 themedPaintsSuppressed = 0;
    quint64 clientRecalculations = 0;
};
[[nodiscard]] NativePaintGuardActivity nativePaintGuardActivity(QWidget* window);

} // namespace window_system
