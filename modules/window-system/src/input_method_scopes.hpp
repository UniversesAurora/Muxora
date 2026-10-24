// SPDX-License-Identifier: MIT
#pragma once

#include "platform/input_method_state.hpp"
#include <window_system/window_manager.hpp>
#include <QHash>
#include <QPointer>
#include <QSet>

namespace window_system {

// Tracks stable model identities, not ephemeral view widgets. The backend is
// injected so lifecycle and focus semantics can be tested without changing IMEs.
class InputMethodScopes {
public:
    explicit InputMethodScopes(std::unique_ptr<InputMethodBackend> backend = createInputMethodBackend());
    void setScope(InputMethodScope scope);
    void reset();
    void synchronize(const LayoutModel& model);
    void focus(const LayoutModel& model, QWidget* widget);
    [[nodiscard]] QUuid key(const LayoutModel& model, PaneId pane) const;
    [[nodiscard]] int count() const { return int(states_.size()); }

private:
    QUuid widgetKey(const LayoutModel& model, QWidget* widget) const;
    std::unique_ptr<InputMethodBackend> backend_;
    InputMethodScope scope_ = InputMethodScope::Tab;
    QHash<QUuid, InputMethodState> states_;
    QUuid active_;
    QPointer<QWidget> focus_;
    quint64 modelState_ = 0;
};

} // namespace window_system
