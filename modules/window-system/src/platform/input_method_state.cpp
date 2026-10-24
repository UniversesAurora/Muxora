// SPDX-License-Identifier: MIT
#include "input_method_state.hpp"
#include <QWidget>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <imm.h>
#endif

namespace window_system {
namespace {
#ifdef Q_OS_WIN
struct NativeState {
    HKL layout = nullptr;
    bool hasContext = false;
    bool open = false;
    bool hasModes = false;
    DWORD conversion = 0;
    DWORD sentence = 0;
};

// Use the existing native top-level input window. Calling effectiveWinId() on
// an alien editor would create a child HWND and change input and hit testing.
HWND inputWindow(QWidget* widget)
{
    return widget ? reinterpret_cast<HWND>(widget->window()->effectiveWinId()) : GetFocus();
}

class NativeBackend final : public InputMethodBackend {
public:
    InputMethodState capture(QWidget* focus) override
    {
        NativeState state;
        state.layout = GetKeyboardLayout(0);
        const auto window = inputWindow(focus);
        if (const auto context = window ? ImmGetContext(window) : nullptr) {
            state.hasContext = true;
            state.open = ImmGetOpenStatus(context) != FALSE;
            state.hasModes = ImmGetConversionStatus(context, &state.conversion, &state.sentence) != FALSE;
            ImmReleaseContext(window, context);
        }
        return state;
    }

    void restore(const InputMethodState& value, QWidget* focus) override
    {
        const auto* state = std::any_cast<NativeState>(&value);
        if (!state || !state->layout) return;
        // Do not load layouts, reorder the user's list or affect other threads.
        if (GetKeyboardLayout(0) != state->layout
            && !ActivateKeyboardLayout(state->layout, 0)) return;
        const auto window = inputWindow(focus);
        if (const auto context = window && state->hasContext ? ImmGetContext(window) : nullptr) {
            ImmSetOpenStatus(context, state->open);
            if (state->hasModes)
                ImmSetConversionStatus(context, state->conversion, state->sentence);
            ImmReleaseContext(window, context);
        }
        // Some TSF IMEs reject IMM mode changes; the layout is still restored.
    }
};
#else
class NativeBackend final : public InputMethodBackend {
public:
    InputMethodState capture(QWidget*) override { return {}; }
    void restore(const InputMethodState&, QWidget*) override {}
};
#endif
} // namespace

std::unique_ptr<InputMethodBackend> createInputMethodBackend()
{
    return std::make_unique<NativeBackend>();
}

} // namespace window_system
