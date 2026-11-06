// SPDX-License-Identifier: MIT
#include "keyboard_cues.hpp"

#include <QtGlobal>
#ifdef Q_OS_WIN
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <qt_windows.h>
#endif

namespace window_system {

#ifdef Q_OS_WIN
namespace {
bool readKeyboardCues()
{
    BOOL cues = FALSE;
    return SystemParametersInfoW(SPI_GETKEYBOARDCUES, 0, &cues, 0) && cues;
}

// Read once and again when the setting changes, not on every paint.
class KeyboardCuesSetting final : public QAbstractNativeEventFilter {
public:
    KeyboardCuesSetting() { QCoreApplication::instance()->installNativeEventFilter(this); }
    bool nativeEventFilter(const QByteArray&, void* message, qintptr*) override
    {
        const auto* msg = static_cast<const MSG*>(message);
        if (msg->message == WM_SETTINGCHANGE && msg->wParam == SPI_SETKEYBOARDCUES) shown = readKeyboardCues();
        return false;
    }
    bool shown = readKeyboardCues();
};
} // namespace
#endif

bool keyboardCuesAlwaysShown()
{
#ifdef Q_OS_WIN
    static const auto* setting = new KeyboardCuesSetting;
    return setting->shown;
#else
    return true;
#endif
}

} // namespace window_system
