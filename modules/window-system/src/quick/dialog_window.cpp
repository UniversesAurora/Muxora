// SPDX-License-Identifier: MIT
#include "dialog_window.hpp"
#include "qml_scene_component.hpp"
#include <window_system/ui_scale.hpp>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <algorithm>

namespace window_system::quick {

DialogWindow::DialogWindow(QWindow* owner, const Theme& theme, DialogRequest request, std::function<void()> finished, QObject* parent)
    : QObject(parent), request_(std::move(request)), finished_(std::move(finished)), style_(new SettingsStyle(theme, this))
{
    static const char* kinds[] = {"text", "lines", "integer"};
    for (const auto& field : std::as_const(request_.fields)) {
        fields_.append(QVariantMap{{"id", field.id}, {"label", QString(field.label).remove(QLatin1Char('&'))},
            {"kind", kinds[int(field.kind)]}, {"value", field.value}, {"minimum", field.minimum}, {"maximum", field.maximum},
            {"suffix", field.suffix}, {"emptyText", field.emptyText}});
        values_.insert(field.id, field.value);
    }
    auto* window = new QQuickWindow;
    window->QObject::setParent(this); // Owned, not a native child window.
    window_ = window;
    window->setObjectName(request_.name);
    window->setTitle(request_.title);
    window->setFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    window->setTransientParent(owner);
    window->setModality(request_.modal ? Qt::WindowModal : Qt::NonModal);
    window->setColor(theme.surface);
    content_ = qobject_cast<QQuickItem*>(sceneComponent(SceneComponent::Dialog)
        .createWithInitialProperties({{"dialog", QVariant::fromValue(this)}}));
    Q_ASSERT(content_);
    QQmlEngine::setObjectOwnership(content_, QQmlEngine::CppOwnership);
    content_->setParent(window);
    content_->setParentItem(window->contentItem());
    connect(window->contentItem(), &QQuickItem::widthChanged, content_, [this, window] { content_->setWidth(window->width()); });
    connect(window->contentItem(), &QQuickItem::heightChanged, content_, [this, window] { content_->setHeight(window->height()); });
    // As large as the content needs, at least the requested size, centred on
    // the owner within its screen.
    const QSize minimum(uiPixels(std::max(320, request_.minimumSize.width())), uiPixels(request_.minimumSize.height()));
    QSize size(std::max(minimum.width(), qCeil(content_->implicitWidth())), std::max(minimum.height(), qCeil(content_->implicitHeight())));
    const QPoint anchor = owner ? owner->geometry().center() : QPoint();
    auto* screen = owner ? QGuiApplication::screenAt(anchor) : QGuiApplication::primaryScreen();
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect available = screen ? screen->availableGeometry() : QRect(QPoint(), size);
    size = size.boundedTo(available.size());
    QPoint position = (owner ? anchor : available.center()) - QPoint(size.width() / 2, size.height() / 2);
    position.setX(std::clamp(position.x(), available.left(), available.right() - size.width() + 1));
    position.setY(std::clamp(position.y(), available.top(), available.bottom() - size.height() + 1));
    window->setGeometry(QRect(position, size));
    window->setMinimumSize(size.boundedTo(QSize(minimum.width(), qCeil(content_->implicitHeight()))));
    content_->setSize(size);
    connect(window, &QWindow::visibleChanged, this, [this](bool visible) { if (!visible) reject(); });
    if (owner) connect(owner, &QObject::destroyed, this, &DialogWindow::reject);
    if (request_.timeoutMs > 0) QTimer::singleShot(request_.timeoutMs, this, &DialogWindow::reject);
    window->installEventFilter(this);
    window->show();
    window->requestActivate();
}

DialogWindow::~DialogWindow()
{
    // The view goes while the dialog it reads is still whole.
    delete window_.data();
    if (finished_) finished_();
}

bool DialogWindow::acceptable() const { return !request_.acceptable || request_.acceptable(values_); }
QString DialogWindow::acceptText() const
{
    return request_.acceptText.isEmpty() ? QCoreApplication::translate("WindowSystem", "OK") : request_.acceptText;
}
QString DialogWindow::cancelText() const
{
    return request_.cancelText.isEmpty() ? QCoreApplication::translate("WindowSystem", "Cancel") : request_.cancelText;
}
QQuickWindow* DialogWindow::window() const { return window_; }

void DialogWindow::setValue(const QString& id, const QVariant& value)
{
    if (!values_.contains(id) || values_.value(id) == value) return;
    values_.insert(id, value);
    emit valuesChanged();
}

void DialogWindow::accept()
{
    // A number being typed counts once its box gives up the focus.
    if (content_) content_->forceActiveFocus();
    if (acceptable()) finish(true);
}

void DialogWindow::reject() { finish(false); }

void DialogWindow::raise()
{
    if (!window_) return;
    window_->raise();
    window_->requestActivate();
}

void DialogWindow::finish(bool accepted)
{
    if (done_) return;
    done_ = true;
    const QPointer<DialogWindow> self(this);
    if (window_) window_->hide();
    // The window is gone first, so what accepting does (a script that opens
    // or activates windows) is not undone by the owner taking focus back.
    if (accepted && request_.accepted) request_.accepted(values_);
    if (self) deleteLater();
}

bool DialogWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == window_ && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape && key->modifiers() == Qt::NoModifier) {
            if (request_.cancellable) reject(); else accept();
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            // Enter starts a new line in several lines of text; Ctrl+Enter accepts.
            auto* focus = window_->activeFocusItem();
            if (focus && focus->inherits("QQuickTextEdit") && !(key->modifiers() & Qt::ControlModifier)) return false;
            accept();
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

QuickDialogs::QuickDialogs(QObject* owner, std::function<QWindow*(WindowId)> windows, std::function<Theme()> theme)
    : owner_(owner), windows_(std::move(windows)), theme_(std::move(theme))
{
}

QuickDialogs::~QuickDialogs()
{
    const auto dialogs = dialogs_;
    for (const auto& dialog : dialogs) delete dialog.data();
}

void QuickDialogs::open(quint64 id, WindowId window, const DialogRequest& request, std::function<void()> finished)
{
    auto* dialog = new DialogWindow(windows_(window), theme_(), request, [this, id, finished = std::move(finished)] {
        dialogs_.remove(id);
        finished();
    }, owner_);
    dialogs_.insert(id, dialog);
}

void QuickDialogs::close(quint64 id) { if (auto* dialog = dialogs_.value(id).data()) dialog->reject(); }
void QuickDialogs::raise(quint64 id) { if (auto* dialog = dialogs_.value(id).data()) dialog->raise(); }

} // namespace window_system::quick
