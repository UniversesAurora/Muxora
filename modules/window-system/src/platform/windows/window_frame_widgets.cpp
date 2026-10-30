// SPDX-License-Identifier: MIT
#include "window_frame.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QEnterEvent>
#include <QEvent>
#include <QMoveEvent>
#include <QPointer>
#include <QScopeGuard>
#include <QVariant>
#include <QWidget>
#include <QWindow>
#include <algorithm>
#include <utility>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <windowsx.h>
#endif

namespace window_system {
namespace {

constexpr auto adapterName = "_windowSystemNativeFrameWidgetAdapter";

class NativeFrameWidgetAdapter final : public QObject {
public:
    explicit NativeFrameWidgetAdapter(QWidget* window) : QObject(window), window_(window)
    {
        setObjectName(QString::fromLatin1(adapterName));
        window->installEventFilter(this);
    }

    QWindow* nativeWindow(bool create)
    {
        const QPointer<NativeFrameWidgetAdapter> self(this);
        const QPointer<QWidget> retainedWindow(window_);
        if (create) window_->winId();
        if (!self || !retainedWindow) return nullptr;
        auto* target = window_->windowHandle();
        if (!target) return nullptr;
        if (target_ != target) {
            target_ = target;
            const QPointer<QWidget> widget(window_);
            NativeFrameClient client;
            client.minimumSize = [widget] { return widget->minimumSize(); };
            client.maximumSize = [widget] { return widget->maximumSize(); };
            client.captionButtonAt = [widget](QPointF nativePosition) -> QObject* {
                const QPoint position(qFloor(nativePosition.x()), qFloor(nativePosition.y()));
                for (auto* child = widget->childAt(position); child && child != widget; child = child->parentWidget()) {
                    if (child->objectName() != QLatin1StringView("CaptionButton")) continue;
                    const auto role = child->property("role").toString();
                    if (role != QLatin1StringView("maximize") && role != QLatin1StringView("restore")) return nullptr;
                    auto* button = qobject_cast<QAbstractButton*>(child);
                    return button && button->isVisible() && button->isEnabled() ? button : nullptr;
                }
                return nullptr;
            };
            client.setCaptionHovered = [widget](QObject* control, bool hovered, QPointF nativePosition) {
                const QPoint position(qFloor(nativePosition.x()), qFloor(nativePosition.y()));
                const QPointer<QAbstractButton> button(qobject_cast<QAbstractButton*>(control));
                if (!button) return; // Qt may already be destroying the old chrome.
                button->setAttribute(Qt::WA_UnderMouse, hovered);
                if (hovered) {
                    const QPoint local = button->mapFrom(widget, position);
                    const QPoint global = widget->mapToGlobal(position);
                    QEnterEvent enter{QPointF(local), QPointF(position), QPointF(global)};
                    QApplication::sendEvent(button, &enter);
                } else {
                    QEvent leave(QEvent::Leave);
                    QApplication::sendEvent(button, &leave);
                }
                if (button) button->update();
            };
            client.setCaptionPressed = [](QObject* control, bool pressed) {
                if (auto* button = qobject_cast<QAbstractButton*>(control)) button->setDown(pressed);
            };
            client.activateCaption = [](QObject* control) {
                if (auto* button = qobject_cast<QAbstractButton*>(control); button && button->isEnabled()) button->click();
            };
            client.frameMarginsChanged = [widget] {
                // QWidget caches its frame offset until a move/resize event.
                // The QWindow core preserves the client rectangle, so report
                // the unchanged position to refresh only that widget cache.
                if (auto* native = widget->windowHandle()) {
                    QMoveEvent moved(native->position(), native->position());
                    QCoreApplication::sendEvent(native, &moved);
                }
                if (widget) widget->update();
            };
            setNativeFrameClient(target, std::move(client));
        }
        if (!self || !retainedWindow) return nullptr;
        setNativeFrameCompact(target, window_->property("windowSystemDragPresentation").toBool());
        return self ? target_.data() : nullptr;
    }

    void prepare()
    {
        if (preparing_) return;
        const QPointer<NativeFrameWidgetAdapter> self(this);
        preparing_ = true;
        const auto reset = qScopeGuard([self] { if (self) self->preparing_ = false; });
        if (auto* target = nativeWindow(true)) applyNativeFrame(target);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == window_ && event->type() == QEvent::WinIdChange
            && window_->testAttribute(Qt::WA_WState_Created) && window_->effectiveWinId()) prepare();
        return false;
    }

private:
    QPointer<QWidget> window_;
    QPointer<QWindow> target_;
    bool preparing_ = false;
};

NativeFrameWidgetAdapter* adapter(QWidget* window, bool create)
{
    for (auto* child : window->children())
        if (child->objectName() == QLatin1StringView(adapterName))
            if (auto* value = dynamic_cast<NativeFrameWidgetAdapter*>(child)) return value;
    return create ? new NativeFrameWidgetAdapter(window) : nullptr;
}

QWindow* nativeWindow(QWidget* window)
{
    return window ? window->windowHandle() : nullptr;
}

#ifdef Q_OS_WIN
void keepEdgePointerInside(QWidget* window, MSG* message)
{
    if (!IsZoomed(message->hwnd) && !window->isFullScreen()
        && !(window->windowHandle() && window->windowHandle()->windowStates().testFlag(Qt::WindowFullScreen))) return;
    const qreal ratio = std::max<qreal>(1.0, window->devicePixelRatioF());
    int x = GET_X_LPARAM(message->lParam), y = GET_Y_LPARAM(message->lParam);
    const int originalX = x, originalY = y;
    // QWidget::childAt uses integer logical positions. Keep the last physical
    // screen pixel inside that lookup; Quick's fractional input needs no fix.
    while (x > 0 && x / ratio > window->width() - 1) --x;
    while (y > 0 && y / ratio > window->height() - 1) --y;
    if (x != originalX || y != originalY) message->lParam = MAKELPARAM(x, y);
}
#endif

} // namespace

void applyNativeFrame(QWidget* window)
{
    if (window && window->isWindow()) adapter(window, true)->prepare();
}

void setNativeDropCommit(QWidget* window, bool committing)
{
    if (!window || !window->isWindow()) return;
    if (auto* target = adapter(window, true)->nativeWindow(false)) setNativeDropCommit(target, committing);
}

bool handleNativeFrame(QWidget* window, void* message, qintptr* result)
{
    if (!window || !window->isWindow() || !message || !result) return false;
    auto* bridge = adapter(window, false);
    if (!bridge) return false;
    const QPointer<QWidget> retained(window);
    auto* target = bridge->nativeWindow(false);
    if (!retained) { *result = 0; return true; }
    if (!target) return false;
#ifdef Q_OS_WIN
    auto* native = static_cast<MSG*>(message);
    if (native->message >= WM_MOUSEFIRST && native->message <= WM_MOUSELAST
        && native->message != WM_MOUSEWHEEL && native->message != WM_MOUSEHWHEEL) keepEdgePointerInside(window, native);
#endif
    return handleNativeFrame(target, message, result);
}

bool hasNormalNativePlacement(const QWidget* window)
{
    return hasNormalNativePlacement(window ? window->windowHandle() : nullptr);
}

NativeTransitionConfiguration nativeTransitionConfiguration(QWidget* window)
{
    return nativeTransitionConfiguration(nativeWindow(window));
}

NativeFrameConfiguration nativeFrameConfiguration(QWidget* window)
{
    return nativeFrameConfiguration(nativeWindow(window));
}

NativePaintGuardActivity nativePaintGuardActivity(QWidget* window)
{
    return nativePaintGuardActivity(nativeWindow(window));
}

} // namespace window_system
