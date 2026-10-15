// SPDX-License-Identifier: MIT
#pragma once
#include <QAbstractButton>
#include <QFontMetrics>
#include <window_system/tab_appearance.hpp>
#include <functional>
#include <optional>

namespace window_system {
inline int edgeTabExtent(const QFontMetrics& metrics, const QString& text, bool icon, int number)
{
    const int extra=(icon ? 20 : 0)+(number>0 ? metrics.horizontalAdvance(QString::number(number))+5 : 0);
    return qBound(64,metrics.horizontalAdvance(text)+24+extra,180);
}
// The same label renderer is used by the rail and its transient drag view.
class EdgeTabButton final : public QAbstractButton {
public:
    EdgeTabButton(QString title, bool vertical, int thickness, QWidget* parent);
    [[nodiscard]] QSize sizeHint() const override;
    void setPreset(std::optional<TabColorPreset> preset);
    void setPresentation(QIcon icon, int number);
    std::function<void(QPoint)> drag;
    std::function<void(QPoint)> menu;
protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
private:
    bool vertical_;
    int thickness_;
    QPoint start_;
    std::optional<TabColorPreset> preset_;
    int number_ = 0;
};
} // namespace window_system
