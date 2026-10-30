// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/theme.hpp>

#include <QIcon>
#include <QQuickItem>

namespace window_system::quick {

class PathItem;
class TabIconItem;
class TabLabelTextItem;

// A compact tool button, drawn as the Widgets theme draws QToolButton:
// transparent at rest; hover fill with an accent border on hover; the
// selection fill with an accent border while pressed or checked. A split
// part instead has a raised fill and rounds only its outer side, without a
// border, and its owner emphasizes both parts together. A caption button is
// flat, as the window's minimize, maximize and close buttons.
class ButtonItem final : public QQuickItem {
    Q_OBJECT
public:
    enum class Shape { Tool, SplitStart, SplitEnd, Caption };

    explicit ButtonItem(const Theme& theme, QQuickItem* parent = nullptr);
    void setTheme(const Theme& theme);
    void setShape(Shape shape);
    void setIcon(const QIcon& icon);
    // Shown instead while hovered or pressed, for a fill that needs it.
    void setActiveIcon(const QIcon& icon);
    // A caption button that closes: hover and press use the danger fill.
    void setDestructive(bool destructive);
    void setText(const QString& text, const QFont& font);
    // Keep the icon at a fixed distance from one edge while text appears.
    void setIconRail(std::optional<int> inset, Qt::Edge edge = Qt::LeftEdge);
    void setCheckable(bool checkable) { checkable_ = checkable; }
    void setChecked(bool checked);
    [[nodiscard]] bool isChecked() const { return checked_; }
    void setDown(bool down);
    void setEmphasized(bool emphasized);
    [[nodiscard]] bool hovered() const { return hovered_; }
    [[nodiscard]] const QString& toolTip() const { return toolTip_; }
    void setToolTip(QString tip) { toolTip_ = std::move(tip); }
    // Native caption input: the frame reports hover, press and activation
    // for caption buttons, whose pixels Windows hit-tests as non-client.
    void setHovered(bool hovered);
    void setPressed(bool pressed);
    void click() { emit clicked(); }

signals:
    void clicked();
    void contextMenuRequested(QPoint global);
    void hoveredChanged(bool hovered);

protected:
    void geometryChange(const QRectF& now, const QRectF& previous) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseUngrabEvent() override;
    void hoverEnterEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;

private:
    void refresh();

    Theme theme_;
    Shape shape_ = Shape::Tool;
    PathItem* fill_;
    PathItem* border_;
    TabIconItem* icon_;
    TabLabelTextItem* text_;
    QIcon iconSource_;
    QIcon activeIcon_;
    QString label_;
    QFont font_;
    QString toolTip_;
    std::optional<int> iconRail_;
    Qt::Edge iconEdge_ = Qt::LeftEdge;
    bool checkable_ = false;
    bool checked_ = false;
    bool hovered_ = false;
    bool pressed_ = false;
    bool down_ = false;
    bool emphasized_ = false;
    bool destructive_ = false;
};

} // namespace window_system::quick
