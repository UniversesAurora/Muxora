// SPDX-License-Identifier: MIT
#include "input_method_state.hpp"
#include <QWidget>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <imm.h>
#include <msctf.h>
#include <oleauto.h>
#include <ole2.h>
#endif

namespace window_system {
namespace {
#ifdef Q_OS_WIN
struct NativeState {
    HKL layout = nullptr;

    TF_INPUTPROCESSORPROFILE profile{};
    bool hasProfile = false;

    bool hasOpen = false;
    LONG open = 0;
    bool hasConversion = false;
    LONG conversion = 0;
    bool hasSentence = false;
    LONG sentence = 0;

    bool hasContext = false;
    bool immOpen = false;
    bool immHasModes = false;
    DWORD immConversion = 0;
    DWORD immSentence = 0;
};

template <typename T>
void release(T*& value)
{
    if (value) value->Release();
    value = nullptr;
}

class TsfSession {
public:
    TsfSession()
    {
        if (FAILED(CoCreateInstance(
                CLSID_TF_ThreadMgr, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&manager_)))
            || !manager_) return;
        if (!manager_ || FAILED(manager_->Activate(&client_))) return;
        active_ = true;
        manager_->QueryInterface(IID_PPV_ARGS(&compartments_));
        CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_ALL,
            IID_PPV_ARGS(&profiles_));
    }

    ~TsfSession()
    {
        release(profiles_);
        release(compartments_);
        if (active_) manager_->Deactivate();
        release(manager_);
    }

    [[nodiscard]] bool active() const { return active_; }
    [[nodiscard]] ITfInputProcessorProfileMgr* profiles() const { return profiles_; }
    [[nodiscard]] ITfCompartmentMgr* compartments() const { return compartments_; }
    [[nodiscard]] TfClientId client() const { return client_; }

private:
    ITfThreadMgr* manager_ = nullptr;
    ITfInputProcessorProfileMgr* profiles_ = nullptr;
    ITfCompartmentMgr* compartments_ = nullptr;
    TfClientId client_ = TF_CLIENTID_NULL;
    bool active_ = false;
};

bool readCompartment(TsfSession& session, REFGUID guid, LONG& value)
{
    ITfCompartment* compartment = nullptr;
    if (!session.compartments()
        || FAILED(session.compartments()->GetCompartment(guid, &compartment))) return false;
    VARIANT current;
    VariantInit(&current);
    const auto read = SUCCEEDED(compartment->GetValue(&current)) && current.vt == VT_I4;
    if (read) value = current.lVal;
    VariantClear(&current);
    release(compartment);
    return read;
}

bool writeCompartment(TsfSession& session, REFGUID guid, LONG value)
{
    ITfCompartment* compartment = nullptr;
    if (!session.compartments()
        || FAILED(session.compartments()->GetCompartment(guid, &compartment))) return false;
    VARIANT next;
    VariantInit(&next);
    next.vt = VT_I4;
    next.lVal = value;
    const auto written = SUCCEEDED(compartment->SetValue(session.client(), &next));
    release(compartment);
    return written;
}

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
        auto* tsf = session();
        if (tsf && tsf->profiles()
            && SUCCEEDED(tsf->profiles()->GetActiveProfile(
                GUID_TFCAT_TIP_KEYBOARD, &state.profile))
            && (state.profile.dwFlags & TF_IPP_FLAG_ACTIVE)) {
            state.hasProfile = true;
        }
        if (tsf) {
            state.hasOpen = readCompartment(
                *tsf, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, state.open);
            state.hasConversion = readCompartment(
                *tsf, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, state.conversion);
            state.hasSentence = readCompartment(
                *tsf, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_SENTENCE, state.sentence);
        }
        const auto window = inputWindow(focus);
        if (const auto context = window ? ImmGetContext(window) : nullptr) {
            state.hasContext = true;
            state.immOpen = ImmGetOpenStatus(context) != FALSE;
            state.immHasModes = ImmGetConversionStatus(
                context, &state.immConversion, &state.immSentence) != FALSE;
            ImmReleaseContext(window, context);
        }
        return state;
    }

    void restore(const InputMethodState& value, QWidget* focus) override
    {
        const auto* state = std::any_cast<NativeState>(&value);
        if (!state || !state->layout) return;
        auto* tsf = session();
        bool profileRestored = false;
        if (tsf && state->hasProfile && tsf->profiles()) {
            profileRestored = SUCCEEDED(tsf->profiles()->ActivateProfile(
                state->profile.dwProfileType, state->profile.langid, state->profile.clsid,
                state->profile.guidProfile, state->profile.hkl, TF_IPPMF_FORPROCESS));
        }
        if (tsf) {
            if (state->hasOpen)
                writeCompartment(*tsf, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, state->open);
            if (state->hasConversion) {
                writeCompartment(
                    *tsf, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, state->conversion);
            }
            if (state->hasSentence)
                writeCompartment(
                    *tsf, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_SENTENCE, state->sentence);
        }
        // Do not load layouts, reorder the user's list or affect other threads.
        if (!profileRestored && GetKeyboardLayout(0) != state->layout
            && !ActivateKeyboardLayout(state->layout, 0)) return;
        const auto window = inputWindow(focus);
        if (const auto context = window && state->hasContext ? ImmGetContext(window) : nullptr) {
            if (!state->hasOpen) ImmSetOpenStatus(context, state->immOpen);
            if (!state->hasConversion && state->immHasModes)
                ImmSetConversionStatus(context, state->immConversion, state->immSentence);
            ImmReleaseContext(window, context);
        }
        // Some input methods reject individual mode writes; the profile and
        // any compartments that accepted their values are still restored.
    }

private:
    TsfSession* session()
    {
        if (!tsf_) tsf_ = std::make_unique<TsfSession>();
        return tsf_->active() ? tsf_.get() : nullptr;
    }

    std::unique_ptr<TsfSession> tsf_;
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
