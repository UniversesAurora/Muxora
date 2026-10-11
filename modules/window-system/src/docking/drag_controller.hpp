#pragma once
#include <window_system/window_manager.hpp>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QTimer>

namespace window_system {
class DockOverlay;
struct DropTarget {
    WindowId window;
    GroupId group;
    Placement placement = Placement::Center;
    bool toEdge = false;
    Edge edge = Edge::Left;
    QRect preview;
    QRect groupRect;
    int insertionIndex = -1;
};
class DragController final : public QObject {
public:
    explicit DragController(WindowManager* manager);
    ~DragController() override;
    void beginTab(TabId tab, QPoint global);
    void beginWindow(WindowId window, QPoint global);
    void cancel();
    [[nodiscard]] bool isActive() const;
    void selectTarget();
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    void start(QPoint global);
    void update(QPoint global);
    void finish(QPoint global);
    void stop();
    DropTarget hit(QPoint global);
    WindowId candidateAt(QPoint global) const;
    WindowManager* manager_;
    QPointer<DockOverlay> overlay_;
    QPointer<QWidget> capture_;
    TabId tab_;
    WindowId wholeWindow_;
    WindowId sourceWindow_;
    QRect originalGeometry_;
    bool originalMaximized_ = false;
    QPoint startPosition_;
    QPoint windowOffset_;
    DropTarget target_;
    WindowId revealCandidate_;
    WindowId revealedWindow_;
    WindowId forcedWindow_;
    QTimer revealTimer_;
    bool active_ = false;
    bool choosing_ = false;
};
} // namespace window_system
