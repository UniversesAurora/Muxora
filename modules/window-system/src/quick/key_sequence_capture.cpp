// SPDX-License-Identifier: MIT
#include "key_sequence_capture.hpp"

#include <QTimerEvent>

namespace window_system::quick {

KeySequenceCapture::KeySequenceCapture(QQuickItem* parent) : QQuickItem(parent)
{
    setActiveFocusOnTab(true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setProperty("windowSystemKeyCapture", true);
}

void KeySequenceCapture::setSequence(const QString& text)
{
    const auto sequence = QKeySequence::fromString(text, QKeySequence::PortableText);
    if (sequence == sequence_) return;
    sequence_ = sequence;
    emit sequenceChanged();
}

bool KeySequenceCapture::event(QEvent* event)
{
    // Every key belongs to the recording, also those the window would take
    // as a shortcut.
    if (event->type() == QEvent::ShortcutOverride) {
        event->accept();
        return true;
    }
    return QQuickItem::event(event);
}

void KeySequenceCapture::keyPressEvent(QKeyEvent* event)
{
    event->accept();
    auto sequence = sequence_;
    const auto result = recording_.press(*event, sequence);
    if (result == KeyRecording::Result::Ignored) return;
    if (result == KeyRecording::Result::Recorded) pause_.start(KeyRecording::pauseMs, this);
    if (sequence != sequence_) {
        sequence_ = sequence;
        emit sequenceChanged();
    }
    emit edited();
}

void KeySequenceCapture::focusInEvent(QFocusEvent* event)
{
    recording_.stop();
    pause_.stop();
    QQuickItem::focusInEvent(event);
}

void KeySequenceCapture::focusOutEvent(QFocusEvent* event)
{
    recording_.stop();
    pause_.stop();
    QQuickItem::focusOutEvent(event);
}

void KeySequenceCapture::mousePressEvent(QMouseEvent* event)
{
    forceActiveFocus(Qt::MouseFocusReason);
    event->accept();
}

void KeySequenceCapture::timerEvent(QTimerEvent* event)
{
    if (event->timerId() != pause_.timerId()) return QQuickItem::timerEvent(event);
    pause_.stop();
    recording_.stop();
}

} // namespace window_system::quick
