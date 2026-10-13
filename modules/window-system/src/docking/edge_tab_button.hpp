// SPDX-License-Identifier: MIT
#pragma once
#include <QAbstractButton>
#include <functional>

namespace window_system {
// The same label renderer is used by the rail and its transient drag view.
class EdgeTabButton final : public QAbstractButton {
public:
    EdgeTabButton(QString title, bool vertical, int thickness, QWidget* parent);
    [[nodiscard]] QSize sizeHint() const override;
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
};
} // namespace window_system
