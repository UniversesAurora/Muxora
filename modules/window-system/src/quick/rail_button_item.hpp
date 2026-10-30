// SPDX-License-Identifier: MIT
#pragma once

#include "docking/drag_start.hpp"
#include "style/tab_title_layout.hpp"
#include <window_system/layout_model.hpp>
#include <window_system/tab_appearance.hpp>
#include <window_system/theme.hpp>

#include <QIcon>
#include <QQuickItem>
#include <functional>
#include <optional>

namespace window_system::quick {

class PathItem;
class TabIconItem;
class TabLabelTextItem;

// One auto-hide tab on a sidebar rail, as EdgeTabButton draws it: the rail
// color or the tab's preset, a two-pixel line on the inner side, and the
// ordinal, icon and title along the rail's text direction.
class RailButtonItem final : public QQuickItem {
    Q_OBJECT
public:
    RailButtonItem(const Theme& theme, TabId tab, QString title, bool vertical, int thickness, QQuickItem* parent = nullptr);
    [[nodiscard]] TabId tab() const { return tab_; }
    [[nodiscard]] const QString& title() const { return title_; }
    void setTheme(const Theme& theme);
    void setThickness(int thickness);
    void setPreset(std::optional<TabColorPreset> preset);
    void setPresentation(QIcon icon, int number);
    void setTitleClipping(bool enabled, int maximumWidth);
    void setChecked(bool checked);
    [[nodiscard]] bool isChecked() const { return checked_; }
    // Along the rail and across it; the rail sizes its buttons from this.
    [[nodiscard]] QSize preferredSize() const;

    std::function<void()> clicked;
    std::function<void(DragStart)> drag;
    std::function<void(QPoint)> menu;
    // A middle click released on the button closes its tab.
    std::function<void()> close;

protected:
    void geometryChange(const QRectF& now, const QRectF& previous) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void hoverEnterEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;

private:
    void refresh();

    Theme theme_;
    TabId tab_;
    QString title_;
    bool vertical_;
    int thickness_;
    std::optional<TabColorPreset> preset_;
    QIcon icon_;
    int number_ = 0;
    bool clipTitles_ = false;
    int maximumTitleWidth_ = defaultTabTitleWidth;
    bool checked_ = false;
    bool hovered_ = false;
    bool pressed_ = false;
    bool middlePressed_ = false;
    DragStart start_;
    mutable TabTitleLayout titleLayout_;
    PathItem* fill_;
    PathItem* line_;
    QQuickItem* content_;
    TabLabelTextItem* numberText_;
    TabIconItem* iconItem_;
    TabLabelTextItem* titleText_;
};

} // namespace window_system::quick
