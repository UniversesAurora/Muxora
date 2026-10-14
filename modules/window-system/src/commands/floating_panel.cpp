// SPDX-License-Identifier: MIT
#include <window_system/floating_panel.hpp>

#include <QApplication>
#include <QHideEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QShowEvent>

namespace window_system {

FloatingPanel::FloatingPanel(QWidget* parent, const FloatingPanelOptions& options)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint)
{
    setOptions(options);
}

FloatingPanel::~FloatingPanel() { observeApplication(false); }

void FloatingPanel::setOptions(const FloatingPanelOptions& options)
{
    const FloatingPanelOptions requested = options;
    const QPointer<FloatingPanel> retained(this);
    const bool reshow = isVisible() && options_.modal != requested.modal;
    if (reshow) hide();
    if (!retained) return;
    options_ = requested;
    setWindowModality(requested.modal ? Qt::WindowModal : Qt::NonModal);
    if (!retained) return;
    if (reshow) show();
}

FloatingPanelOptions FloatingPanel::options() const { return options_; }

bool FloatingPanel::ownsWidget(const QWidget* widget) const
{
    // Parent chains include menus/popups opened by controls inside the panel.
    for (auto* current = widget; current; current = current->parentWidget())
        if (current == this) return true;
    return false;
}

void FloatingPanel::observeApplication(bool observe)
{
    if (observing_ == observe || !qApp) return;
    observing_ = observe;
    if (observe) qApp->installEventFilter(this);
    else qApp->removeEventFilter(this);
}

bool FloatingPanel::eventFilter(QObject* watched, QEvent* event)
{
    const QPointer<FloatingPanel> retained(this);
    if (isVisible() && options_.dismissOnOutsidePress) {
        if (event->type() == QEvent::ApplicationDeactivate) reject();
        else if (event->type() == QEvent::MouseButtonPress) {
            const auto* widget = qobject_cast<QWidget*>(watched);
            const auto* mouse = static_cast<QMouseEvent*>(event);
            if (widget && !ownsWidget(widget)
                && !frameGeometry().contains(mouse->globalPosition().toPoint())) reject();
        }
    }
    if (!retained) return false;
    return QDialog::eventFilter(watched, event);
}

void FloatingPanel::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    observeApplication(true);
}

void FloatingPanel::hideEvent(QHideEvent* event)
{
    observeApplication(false);
    QDialog::hideEvent(event);
}

} // namespace window_system
