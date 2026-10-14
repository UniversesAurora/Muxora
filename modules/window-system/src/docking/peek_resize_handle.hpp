// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/layout_model.hpp>
#include <QMouseEvent>
#include <QWidget>
#include <functional>

namespace window_system {

class PeekResizeHandle final : public QWidget {
public:
    PeekResizeHandle(Edge edge, QWidget* parent) : QWidget(parent), edge_(edge)
    {
        setObjectName("PeekResizeHandle");
        setProperty("edge", int(edge));
        setCursor(edge == Edge::Left || edge == Edge::Right ? Qt::SizeHorCursor : Qt::SizeVerCursor);
        setAccessibleName(QStringLiteral("调整临时展开面板大小"));
    }
    std::function<QSize()> requestedSize;
    std::function<void(QSize)> resizeRequested;
protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) return;
        start_ = event->globalPosition().toPoint();
        initial_ = requestedSize ? requestedSize() : QSize(250, 170);
        if (edge_ == Edge::Left || edge_ == Edge::Right) initial_.setWidth(parentWidget()->width());
        else initial_.setHeight(parentWidget()->height());
        pressed_ = true; event->accept();
    }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!pressed_ || !event->buttons().testFlag(Qt::LeftButton)) return;
        const auto delta = event->globalPosition().toPoint() - start_;
        auto size = initial_;
        if (edge_ == Edge::Left || edge_ == Edge::Right)
            size.setWidth(qBound(140, initial_.width() + (edge_ == Edge::Left ? delta.x() : -delta.x()), QWIDGETSIZE_MAX));
        else
            size.setHeight(qBound(100, initial_.height() + (edge_ == Edge::Top ? delta.y() : -delta.y()), QWIDGETSIZE_MAX));
        const auto resize = resizeRequested;
        event->accept();
        // Host save callbacks may synchronously destroy this panel.
        if (resize) resize(size);
    }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) { pressed_ = false; event->accept(); }
    }
private:
    Edge edge_;
    QPoint start_;
    QSize initial_;
    bool pressed_ = false;
};

} // namespace window_system
