// SPDX-License-Identifier: MIT
#pragma once

#include "window_drag_area_item.hpp"
#include <window_system/theme.hpp>
#include <window_system/window_manager.hpp>

#include <QQuickItem>
#include <QFont>
#include <functional>

namespace window_system::quick {

class ButtonItem;
class TabLabelTextItem;

// The title content of a window strip. Caption content is owned here too,
// but its visual parent belongs to a separate strip in the same registry.
// Pressing the title moves the window; a floating title carries its tabs
// after the drag distance. A double click maximizes or restores the window.
class WindowChromeItem final : public WindowDragAreaItem {
    Q_OBJECT
public:
    WindowChromeItem(const Theme& theme, QQuickItem* parent = nullptr);
    [[nodiscard]] static int rowHeight();
    void setTitle(const QString& title);
    void setTheme(const Theme& theme);
    // Only change the visual parent: QObject ownership stays with this item.
    [[nodiscard]] QQuickItem* captionContent() const { return caption_; }
    void setButtonOrder(CaptionButtonOrder order);
    // Maximized or full screen: the middle button restores.
    void setExpanded(bool expanded);
    // The caption button under a point in window coordinates, for the frame.
    [[nodiscard]] ButtonItem* captionButtonAt(QPointF position) const;
    std::function<void()> minimizeRequested;
    std::function<void()> closeRequested;

protected:
    bool event(QEvent* event) override;
    void geometryChange(const QRectF& now, const QRectF& previous) override;

private:
    void requestLayout();
    void relayout();
    void layoutTitle();
    void layoutCaption();
    void measureTitle();
    Theme theme_;
    QQuickItem* caption_;
    ButtonItem* minimize_;
    ButtonItem* maximize_;
    ButtonItem* close_;
    TabLabelTextItem* title_;
    QString titleText_;
    QFont titleFont_;
    int titleWidth_ = 0;
    int titleHeight_ = 0;
    CaptionButtonOrder order_ = CaptionButtonOrder::Windows;
    bool expanded_ = false;
    bool layoutPending_ = false;
    QObject* accessibility_;
};

} // namespace window_system::quick
