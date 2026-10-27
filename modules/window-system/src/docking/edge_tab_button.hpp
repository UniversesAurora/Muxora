// SPDX-License-Identifier: MIT
#pragma once
#include <QAbstractButton>
#include <QFontMetrics>
#include <window_system/tab_appearance.hpp>
#include "style/tab_label_geometry.hpp"
#include "style/tab_title_layout.hpp"
#include "drag_start.hpp"
#include <functional>
#include <optional>
#include <algorithm>

namespace window_system {
inline constexpr int edgeTabIconSize = 16;
// The rail label: ordinal, icon and title along the rail's text direction.
inline TabLabelParts edgeTabParts(const QFontMetrics& metrics, int natural, bool icon, int number,
                                  bool clipTitles = false, int maximumTitleWidth = defaultTabTitleWidth)
{
    return {number > 0 ? metrics.horizontalAdvance(QString::number(number)) : 0, icon ? uiPixels(edgeTabIconSize) : 0,
            clipTitles ? std::min(natural, uiPixels(maximumTitleWidth)) : natural};
}
inline int edgeTabExtent(const QFontMetrics& metrics, int natural, bool icon, int number, int thickness,
                         bool clipTitles = false, int maximumTitleWidth = defaultTabTitleWidth)
{
    return edgeTabExtent(edgeTabParts(metrics, natural, icon, number, clipTitles, maximumTitleWidth), thickness);
}
// The same label renderer is used by the rail and its transient drag view.
class EdgeTabButton final : public QAbstractButton {
public:
    EdgeTabButton(QString title, bool vertical, int thickness, QWidget* parent);
    [[nodiscard]] QSize sizeHint() const override;
    void setPreset(std::optional<TabColorPreset> preset);
    void setPresentation(QIcon icon, int number);
    void setTitleClipping(bool enabled, int maximumWidth);
    void setThickness(int thickness) { thickness_ = thickness; updateGeometry(); update(); }
    std::function<void(DragStart)> drag;
    std::function<void(QPoint)> menu;
    // A middle click released on the button closes its tab.
    std::function<void()> close;
protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
private:
    bool vertical_;
    int thickness_;
    DragStart start_;
    bool middlePressed_ = false;
    std::optional<TabColorPreset> preset_;
    int number_ = 0;
    bool clipTitles_ = false;
    int maximumTitleWidth_ = defaultTabTitleWidth;
    mutable TabTitleLayout titleLayout_;
};
} // namespace window_system
