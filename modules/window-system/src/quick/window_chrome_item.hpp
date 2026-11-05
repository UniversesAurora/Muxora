// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/theme.hpp>
#include <window_system/window_manager.hpp>

#include <QQuickItem>
#include <functional>
#include <optional>

namespace window_system::quick {

class ButtonItem;
class TabLabelTextItem;

// A Quick window's header row, as the Widgets WindowHeader: the title in
// the middle and the caption buttons at the outer edge. Menus and toolbars
// join it with 10.4. Pressing the title moves the window; a double click
// maximizes or restores it. A window whose title carries its tabs, as a
// floating window's does, asks for that drag once the pointer has moved
// the drag distance.
class WindowChromeItem final : public QQuickItem {
    Q_OBJECT
public:
    WindowChromeItem(const Theme& theme, QQuickItem* parent = nullptr);
    [[nodiscard]] static int rowHeight();
    void setTitle(const QString& title);
    void setButtonOrder(CaptionButtonOrder order);
    // Maximized or full screen: the middle button restores.
    void setExpanded(bool expanded);
    // The caption button under a point in window coordinates, for the frame.
    [[nodiscard]] ButtonItem* captionButtonAt(QPointF position) const;
    std::function<void()> minimizeRequested;
    std::function<void()> maximizeRequested;
    std::function<void()> closeRequested;
    std::function<bool()> carriesTabs;
    std::function<void(QPoint pressGlobal)> carryRequested;
    std::function<void(QPoint)> menuRequested;

protected:
    bool event(QEvent* event) override;
    void geometryChange(const QRectF& now, const QRectF& previous) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void relayout();
    Theme theme_;
    ButtonItem* minimize_;
    ButtonItem* maximize_;
    ButtonItem* close_;
    TabLabelTextItem* title_;
    QString titleText_;
    // Where a carrying title was pressed, while it waits for the distance.
    std::optional<QPoint> press_;
    CaptionButtonOrder order_ = CaptionButtonOrder::Windows;
    bool expanded_ = false;
};

} // namespace window_system::quick
