// SPDX-License-Identifier: MIT
#pragma once

#include <any>
#include <memory>

class QWidget;
class QWindow;

namespace window_system {

// Only the backend interprets this process-local value. It is never serialized.
using InputMethodState = std::any;

class InputMethodBackend {
public:
    virtual ~InputMethodBackend() = default;
    virtual InputMethodState capture(QWindow* window) = 0;
    virtual void restore(const InputMethodState& state, QWindow* window) = 0;
    InputMethodState capture(QWidget* focus);
    void restore(const InputMethodState& state, QWidget* focus);
};

std::unique_ptr<InputMethodBackend> createInputMethodBackend();

} // namespace window_system
