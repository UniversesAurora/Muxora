// SPDX-License-Identifier: MIT
#pragma once

#include <QColor>

namespace window_system {

// Window activation is independent of keyboard focus, selection and hover.
// Resolve these pairs when painting; derive them only when the theme changes.
struct WindowStateColor {
    QColor active;
    QColor inactive;

    [[nodiscard]] const QColor& resolve(bool windowActive) const
    {
        return windowActive ? active : inactive;
    }

    [[nodiscard]] static WindowStateColor neutralInactive(QColor active)
    {
        const auto inactive = QColor::fromHslF(0, 0, active.lightnessF(), active.alphaF());
        return {active, inactive};
    }
};

} // namespace window_system
