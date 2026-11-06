// SPDX-License-Identifier: MIT
#pragma once

#include "settings_style.hpp"
#include <window_system/dialog_request.hpp>
#include <QHash>
#include <QPointer>
#include <QVariantList>
#include <qqmlintegration.h>

class QQuickItem;
class QQuickWindow;
class QWindow;

namespace window_system::quick {

// One dialog in its own window over its owner, as a QDialog with a native
// frame: the text, the fields with their labels, and the buttons. Enter
// accepts (Ctrl+Enter in several lines of text), Esc and the frame's close
// button cancel. It deletes itself once closed.
class DialogWindow final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the Quick workspace")
    Q_PROPERTY(QString text READ text CONSTANT)
    // {id, label, kind ("text", "lines", "integer"), value, minimum, maximum,
    // suffix, emptyText}
    Q_PROPERTY(QVariantList fields READ fields CONSTANT)
    Q_PROPERTY(bool acceptable READ acceptable NOTIFY valuesChanged)
    Q_PROPERTY(QString acceptText READ acceptText CONSTANT)
    Q_PROPERTY(QString cancelText READ cancelText CONSTANT)
    Q_PROPERTY(bool cancellable READ cancellable CONSTANT)
    Q_PROPERTY(window_system::quick::SettingsStyle* style READ style CONSTANT)
public:
    DialogWindow(QWindow* owner, const Theme& theme, DialogRequest request, std::function<void()> finished, QObject* parent);
    ~DialogWindow() override;

    QString text() const { return request_.text; }
    QVariantList fields() const { return fields_; }
    bool acceptable() const;
    QString acceptText() const;
    QString cancelText() const;
    bool cancellable() const { return request_.cancellable; }
    SettingsStyle* style() const { return style_; }
    QQuickWindow* window() const;

    Q_INVOKABLE void setValue(const QString& id, const QVariant& value);
    Q_INVOKABLE void accept();
    Q_INVOKABLE void reject();
    void raise();

signals:
    void valuesChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void finish(bool accepted);
    DialogRequest request_;
    std::function<void()> finished_;
    QVariantList fields_;
    QVariantMap values_;
    SettingsStyle* style_;
    QPointer<QQuickWindow> window_;
    QQuickItem* content_ = nullptr;
    bool done_ = false;
};

// The Quick workspace's dialogs, over its windows.
class QuickDialogs final : public DialogPresenter {
public:
    QuickDialogs(QObject* owner, std::function<QWindow*(WindowId)> windows, std::function<Theme()> theme);
    ~QuickDialogs() override;
    void open(quint64 id, WindowId window, const DialogRequest& request, std::function<void()> finished) override;
    void close(quint64 id) override;
    void raise(quint64 id) override;

private:
    QObject* owner_;
    std::function<QWindow*(WindowId)> windows_;
    std::function<Theme()> theme_;
    QHash<quint64, QPointer<DialogWindow>> dialogs_;
};

} // namespace window_system::quick
