// SPDX-License-Identifier: MIT
#pragma once

#include <QQuickItem>
#include <functional>
#include <optional>

namespace window_system::quick {

// Shared input for the title and unused toolbar space. Ordinary windows
// enter the system move; floating windows can carry their tabs instead.
class WindowDragAreaItem : public QQuickItem {
public:
    explicit WindowDragAreaItem(QQuickItem* parent = nullptr);
    std::function<void()> maximizeRequested;
    std::function<bool()> carriesTabs;
    std::function<void(QPoint pressGlobal)> carryRequested;
    std::function<void(QPoint)> menuRequested;

protected:
    bool event(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    // The press anchor stays fixed while a tab-carrying drag starts.
    std::optional<QPoint> press_;
};

} // namespace window_system::quick
