// SPDX-License-Identifier: MIT
#pragma once

#include <QCoreApplication>
#include <window_system/layout_model.hpp>
#include <QMouseEvent>
#include <QWidget>
#include <functional>

namespace window_system {

class PeekResizeHandle final : public QWidget {
public:
    PeekResizeHandle(Edge edge, QWidget* parent, Qt::Edges resizeEdges = {}) : QWidget(parent)
    {
        if (!resizeEdges) {
            switch (edge) {
            case Edge::Left: resizeEdges = Qt::RightEdge; break;
            case Edge::Right: resizeEdges = Qt::LeftEdge; break;
            case Edge::Top: resizeEdges = Qt::BottomEdge; break;
            case Edge::Bottom: resizeEdges = Qt::TopEdge; break;
            }
        }
        resizeEdges_ = resizeEdges;
        setObjectName("PeekResizeHandle");
        setProperty("edge", int(edge));
        setProperty("resizeEdges", int(resizeEdges));
        const bool width = resizeEdges.testFlag(Qt::LeftEdge) || resizeEdges.testFlag(Qt::RightEdge);
        const bool height = resizeEdges.testFlag(Qt::TopEdge) || resizeEdges.testFlag(Qt::BottomEdge);
        setCursor(width && height ? (resizeEdges.testFlag(Qt::LeftEdge) == resizeEdges.testFlag(Qt::TopEdge)
            ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor) : width ? Qt::SizeHorCursor : Qt::SizeVerCursor);
        setAccessibleName(QCoreApplication::translate("WindowSystem", "调整临时展开面板大小"));
    }
    std::function<PeekSizePolicy()> requestedSize;
    std::function<void(PeekSizePolicy)> resizeRequested;
protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) return;
        start_ = event->globalPosition().toPoint();
        initial_ = parentWidget()->size();
        initialPolicy_ = requestedSize ? requestedSize() : PeekSizePolicy{};
        pressed_ = true; event->accept();
    }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!pressed_ || !event->buttons().testFlag(Qt::LeftButton)) return;
        const auto delta = event->globalPosition().toPoint() - start_;
        auto size = initialPolicy_;
        if (resizeEdges_.testFlag(Qt::LeftEdge) || resizeEdges_.testFlag(Qt::RightEdge))
            size.width = qBound(140, initial_.width() + (resizeEdges_.testFlag(Qt::RightEdge) ? delta.x() : -delta.x()), QWIDGETSIZE_MAX);
        if (resizeEdges_.testFlag(Qt::TopEdge) || resizeEdges_.testFlag(Qt::BottomEdge))
            size.height = qBound(100, initial_.height() + (resizeEdges_.testFlag(Qt::BottomEdge) ? delta.y() : -delta.y()), QWIDGETSIZE_MAX);
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
    Qt::Edges resizeEdges_;
    QPoint start_;
    QSize initial_;
    PeekSizePolicy initialPolicy_;
    bool pressed_ = false;
};

} // namespace window_system
