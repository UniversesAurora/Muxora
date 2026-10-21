// SPDX-License-Identifier: MIT
#pragma once

#include <QApplication>
#include <QDebug>
#include <QEvent>
#include <QPointer>

namespace window_system::test {

// Component drag fixtures provide their own complete QMouseEvent stream. An
// unrelated desktop pointer must not add a second stream during native capture.
// This affects only spontaneous Qt MouseMove events in this test process;
// authored events, native frame messages, keyboard input and timers still run.
class AuthoredMouseInput final : public QObject {
public:
    explicit AuthoredMouseInput(QObject* parent) : QObject(parent) {}

    void takePriority()
    {
        qApp->removeEventFilter(this);
        qApp->installEventFilter(this);
    }

    void finish()
    {
        qApp->removeEventFilter(this);
        if (isolatedMoves_) qInfo() << "Authored mouse fixture isolated spontaneous Qt moves:" << isolatedMoves_;
        isolatedMoves_ = 0;
    }

protected:
    bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() != QEvent::MouseMove || !event->spontaneous()) return false;
        ++isolatedMoves_;
        return true;
    }

private:
    quint64 isolatedMoves_ = 0;
};

inline AuthoredMouseInput& authoredMouseInput()
{
    static QPointer<AuthoredMouseInput> input;
    if (!input) input = new AuthoredMouseInput(qApp);
    return *input;
}

} // namespace window_system::test
