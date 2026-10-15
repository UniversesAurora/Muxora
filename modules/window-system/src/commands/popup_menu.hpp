// SPDX-License-Identifier: MIT
#pragma once

#include <QMenu>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QRect>

namespace window_system {

// Anchor geometry is global and uses the trigger's screen, including monitors
// left of the primary screen. Prefer matching left edges below the trigger;
// fall back to matching right edges above it before clamping to the work area.
[[nodiscard]] QPoint popupMenuPosition(const QRect& anchor, QSize menuSize, const QRect& available);
void setPopupAnchor(QMenu* menu, const QRect& globalTriggerRect);
// Menu and combo-popup clients share the same square native frame policy.
void preparePopupFrame(QWidget* popup);

// A click outside closes the menu without also activating a control beneath it.
// Works for persistent root menus and their nested customization menus.
class DismissOnlyMenu final : public QMenu {
public:
    explicit DismissOnlyMenu(QWidget* parent = nullptr);
    DismissOnlyMenu(const QString& title, QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override
    {
        // QMenu's Home/End support depends on the platform style. Keep these
        // navigation keys available with our own theme on every platform.
        if (event->key() == Qt::Key_Home || event->key() == Qt::Key_End) {
            const auto items = actions();
            const bool last = event->key() == Qt::Key_End;
            for (int offset = 0; offset < items.size(); ++offset) {
                auto* action = items[last ? items.size() - 1 - offset : offset];
                if (action->isVisible() && action->isEnabled() && !action->isSeparator()) {
                    setActiveAction(action);
                    event->accept();
                    return;
                }
            }
        }
        QMenu::keyPressEvent(event);
    }
    bool event(QEvent* event) override
    {
        // Qt clears this attribute before each mouse event. Keep it enabled
        // after every event as well, so keyboard or programmatic dismissal
        // cannot leave a pending replay for a later popup's closing click.
        setAttribute(Qt::WA_NoMouseReplay);
        const bool handled = QMenu::event(event);
        if (event->type() == QEvent::WinIdChange && testAttribute(Qt::WA_WState_Created) && effectiveWinId())
            configureNativePopup();
        return handled;
    }
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
private:
    void configureNativePopup();
};

} // namespace window_system
