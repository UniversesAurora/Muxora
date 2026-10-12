// SPDX-License-Identifier: MIT
#pragma once

#include <QMenu>
#include <QMouseEvent>

namespace window_system {

// A click outside closes the menu without also activating a control beneath it.
// Works for persistent root menus and their nested customization menus.
class DismissOnlyMenu final : public QMenu {
public:
    using QMenu::QMenu;

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (!rect().contains(event->position().toPoint())) {
            // QApplication resets WA_NoMouseReplay before each mouse event.
            // Set it during the actual outside press, before QMenu releases
            // capture. The last parent popup must also prohibit the replay:
            // a queued press can otherwise arrive after the native release
            // and leave Qt's implicit button state pressed, blocking hover.
            for (QMenu* menu = this; menu; menu = qobject_cast<QMenu*>(menu->parentWidget()))
                menu->setAttribute(Qt::WA_NoMouseReplay);
        }
        QMenu::mousePressEvent(event);
    }
};

} // namespace window_system
