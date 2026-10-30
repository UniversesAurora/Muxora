// SPDX-License-Identifier: MIT
#pragma once

#include <QtGlobal>
#include <QPointF>
#include <QSize>
#include "window_menu.hpp"
#include <functional>

class QObject;
class QWindow;
class QWidget;

namespace window_system {

// A window adapter supplies its real maximize/restore control and bridges
// native hover/press/click to the same accessible control used by client input.
// An empty callback uses QWindow's own size constraints. Platform code never
// inspects a widget or a scene-graph item tree.
struct NativeFrameClient {
    std::function<QSize()> minimumSize;
    std::function<QSize()> maximumSize;
    std::function<QObject*(QPointF)> captionButtonAt;
    std::function<void(QObject*, bool, QPointF)> setCaptionHovered;
    std::function<void(QObject*, bool)> setCaptionPressed;
    std::function<void(QObject*)> activateCaption;
    std::function<void()> frameMarginsChanged;
};

void setNativeFrameClient(QWindow* window, NativeFrameClient client);
// Compact carriers supply their client shape through QWindow::setMask(). The
// same HWND keeps input/capture; only the native frame presentation changes.
void setNativeFrameCompact(QWindow* window, bool compact);
void applyNativeFrame(QWindow* window);
void setNativeDropCommit(QWindow* window, bool committing);
bool handleNativeFrame(QWindow* window, void* message, qintptr* result);
[[nodiscard]] bool hasNormalNativePlacement(const QWindow* window);

// Call once while the completed top-level widget is still hidden. Preparing its
// HWND before the first show lets DWM own native window transitions; subsequent
// calls are idempotent and do not start an animation. Qt continues to own window
// visibility and state. Other platforms keep their ordinary Qt behavior.
void applyNativeFrame(QWidget* window);
// A compact carrier disappears into a tab group, rather than closing as a
// separate window. Limit transition suppression to this commit; restore it
// if the drop fails and the carrier survives.
void setNativeDropCommit(QWidget* window, bool committing);
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
[[nodiscard]] NativeTransitionConfiguration nativeTransitionConfiguration(QWindow* window);

// Diagnostics describe the attributes actually written for this HWND. A cached
// repeated preparation has the same revision; composition changes refresh it.
struct NativeFrameConfiguration {
    bool attempted = false;
    bool compact = false;
    bool nonClientEnabled = false;
    qint32 nonClientResult = 0;
    qint32 marginsResult = 0;
    quint64 revision = 0;
    bool expanded = false;
    bool fullscreen = false;
    quint32 cornerPreference = 2;
    qint32 cornerResult = 0;
    quint32 borderColor = 0xffffffff;
    qint32 borderResult = 0;
};
[[nodiscard]] NativeFrameConfiguration nativeFrameConfiguration(QWidget* window);
[[nodiscard]] NativeFrameConfiguration nativeFrameConfiguration(QWindow* window);

// Counts the custom-frame paint guards reached by real native messages. The
// requested DWM policy does not imply NC composition for a shaped HWND.
struct NativePaintGuardActivity {
    quint64 paintsSuppressed = 0;
    quint64 activationsSuppressed = 0;
    quint64 themedPaintsSuppressed = 0;
    quint64 clientRecalculations = 0;
};
[[nodiscard]] NativePaintGuardActivity nativePaintGuardActivity(QWidget* window);
[[nodiscard]] NativePaintGuardActivity nativePaintGuardActivity(QWindow* window);

} // namespace window_system
