// SPDX-License-Identifier: MIT
#pragma once

#include <QEvent>
#include <QToolButton>
#include <algorithm>

namespace window_system {

// The outer hit target is a layout policy, not a QSS content-box minimum.
// QStyleSheetStyle resets widget minima while polishing; restore the compact
// target after that step without replacing native button input or painting.
class CompactToolButton final : public QToolButton {
public:
    explicit CompactToolButton(QWidget* parent, int extent = 20)
        : QToolButton(parent), extent_(std::max(1, extent))
    {
        setAutoRaise(true); applyMetrics();
    }

protected:
    bool event(QEvent* event) override
    {
        const bool handled = QToolButton::event(event);
        if (event->type() == QEvent::Polish || event->type() == QEvent::StyleChange
            || event->type() == QEvent::PolishRequest) applyMetrics();
        return handled;
    }

private:
    void applyMetrics()
    {
        const QSize target(extent_, extent_);
        if (minimumSize() != target || maximumSize() != target) setFixedSize(target);
        if (iconSize() != QSize(14, 14)) setIconSize(QSize(14, 14));
    }
    const int extent_;
};

} // namespace window_system
