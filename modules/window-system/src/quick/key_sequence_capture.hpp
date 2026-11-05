// SPDX-License-Identifier: MIT
#pragma once

#include "settings/key_recording.hpp"
#include <QBasicTimer>
#include <QKeySequence>
#include <QQuickItem>

namespace window_system::quick {

// Records a key sequence while focused, as the Widgets shortcut editor does.
// The workspace does not run shortcuts while it has the focus: it is marked
// as a key capture (WindowManager checks the property).
class KeySequenceCapture : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    // Portable text, as bindings are saved.
    Q_PROPERTY(QString sequence READ sequence WRITE setSequence NOTIFY sequenceChanged)
    // The keys as the platform names them.
    Q_PROPERTY(QString displayText READ displayText NOTIFY sequenceChanged)
public:
    explicit KeySequenceCapture(QQuickItem* parent = nullptr);

    QString sequence() const { return sequence_.toString(QKeySequence::PortableText); }
    void setSequence(const QString& text);
    QString displayText() const { return sequence_.toString(QKeySequence::NativeText); }

signals:
    void sequenceChanged();
    // The user recorded or cleared keys.
    void edited();

protected:
    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override { event->accept(); }
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void timerEvent(QTimerEvent* event) override;

private:
    QKeySequence sequence_;
    KeyRecording recording_;
    QBasicTimer pause_;
};

} // namespace window_system::quick
