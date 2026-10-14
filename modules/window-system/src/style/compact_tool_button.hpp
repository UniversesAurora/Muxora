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
        : CompactToolButton(parent, QSize(std::max(1, extent), std::max(1, extent)))
    {
    }

    CompactToolButton(QWidget* parent, QSize target)
        : QToolButton(parent), target_(target.expandedTo(QSize(1, 1)))
    {
        setAutoRaise(true); applyMetrics();
    }

    void setTargetSize(QSize target)
    {
        target_ = target.expandedTo(QSize(1, 1));
        applyMetrics();
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
        if (minimumSize() != target_ || maximumSize() != target_) setFixedSize(target_);
        const QSize icon(std::min(14, std::max(1, target_.width() - 2)),
                         std::min(14, std::max(1, target_.height() - 2)));
        if (iconSize() != icon) setIconSize(icon);
    }
    QSize target_;
};

} // namespace window_system
