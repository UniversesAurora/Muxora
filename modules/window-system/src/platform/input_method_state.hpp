// SPDX-License-Identifier: MIT
#pragma once

#include <any>
#include <memory>

class QWidget;

namespace window_system {

// Only the backend interprets this process-local value. It is never serialized.
using InputMethodState = std::any;

class InputMethodBackend {
public:
    virtual ~InputMethodBackend() = default;
    virtual InputMethodState capture(QWidget* focus) = 0;
    virtual void restore(const InputMethodState& state, QWidget* focus) = 0;
};

std::unique_ptr<InputMethodBackend> createInputMethodBackend();

} // namespace window_system
