// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/theme.hpp>
#include <window_system/window_manager.hpp>
#include <QFont>
#include <QQuickItem>

namespace window_system::quick {

class TabLabelTextItem;

// A manager's text bar in a Quick page, as the Widgets presentation shows it
// with a plain-text QLabel: the interface font and text color, 6 pixels on
// either side, as tall as a line, its tooltip on hover.
class TextBarItem final : public QQuickItem {
    Q_OBJECT
public:
    TextBarItem(TextBar bar, const Theme& theme, QQuickItem* parent = nullptr);
    void setTheme(const Theme& theme);
    [[nodiscard]] const TextBar& bar() const { return bar_; }

protected:
    void geometryChange(const QRectF& now, const QRectF& previous) override;
    void hoverEnterEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;

private:
    void refresh();
    TextBar bar_;
    Theme theme_;
    QFont font_;
    TabLabelTextItem* text_;
};

} // namespace window_system::quick
