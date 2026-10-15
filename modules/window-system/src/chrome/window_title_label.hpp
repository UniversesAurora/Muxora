// SPDX-License-Identifier: MIT
#pragma once

#include <QLabel>
#include <QEvent>
#include <QFontMetrics>
#include <QStylePainter>

namespace window_system {

// The label retains the full caption for accessibility and window persistence.
// Only the paint presentation is shortened to fit its current logical width.
class WindowTitleLabel final : public QLabel {
public:
    explicit WindowTitleLabel(const QString& text, QWidget* parent)
        : QLabel(text, parent)
    {
        setTextFormat(Qt::PlainText);
        setAlignment(Qt::AlignCenter);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setContentsMargins(7, 0, 7, 0);
    }

protected:
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::ToolTip) { event->accept(); return true; }
        return QLabel::event(event);
    }
    void paintEvent(QPaintEvent*) override
    {
        const QRect area = contentsRect();
        if (area.isEmpty()) return;
        const QString visible = fontMetrics().elidedText(text(), Qt::ElideRight, area.width());
        QStylePainter painter(this);
        painter.setClipRect(area);
        painter.drawItemText(area, Qt::AlignCenter | Qt::TextSingleLine,
                             palette(), isEnabled(), visible, foregroundRole());
    }
};

} // namespace window_system
