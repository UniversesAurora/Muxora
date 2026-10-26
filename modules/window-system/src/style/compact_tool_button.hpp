// SPDX-License-Identifier: MIT
#pragma once

#include <QEvent>
#include <QToolButton>
#include <QStyleOptionToolButton>
#include <QStylePainter>
#include <algorithm>
#include <optional>

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
    // An expanding button can keep its icon under the pointer instead of
    // recentering the icon/text pair whenever its label becomes visible.
    void setIconRail(std::optional<int> inset = {}, Qt::Edge edge = Qt::LeftEdge)
    {
        if (iconRail_ == inset && iconEdge_ == edge) return;
        iconRail_ = inset; iconEdge_ = edge; update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        if (!iconRail_) { QToolButton::paintEvent(event); return; }
        QStyleOptionToolButton option; initStyleOption(&option);
        const auto image = option.icon; const auto label = option.text;
        option.icon = {}; option.text.clear();
        QStylePainter painter(this); painter.drawComplexControl(QStyle::CC_ToolButton, option);
        const bool right = iconEdge_ == Qt::RightEdge;
        const int center = right ? width() - *iconRail_ : *iconRail_;
        const QRect glyph(center - iconSize().width() / 2, (height() - iconSize().height()) / 2, iconSize().width(), iconSize().height());
        image.paint(&painter, glyph, Qt::AlignCenter, isEnabled() ? QIcon::Normal : QIcon::Disabled, isChecked() ? QIcon::On : QIcon::Off);
        if (toolButtonStyle() != Qt::ToolButtonIconOnly) {
            const QRect textArea = right ? QRect(2, 0, std::max(0, glyph.left() - 8), height())
                : QRect(glyph.right() + 7, 0, std::max(0, width() - glyph.right() - 9), height());
            painter.setPen(option.palette.color(isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::ButtonText));
            painter.drawText(textArea, Qt::AlignVCenter | Qt::TextSingleLine | (right ? Qt::AlignRight : Qt::AlignLeft), label);
        }
    }
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
    std::optional<int> iconRail_;
    Qt::Edge iconEdge_ = Qt::LeftEdge;
};

} // namespace window_system
