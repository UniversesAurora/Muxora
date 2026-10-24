#pragma once
#include <window_system/window_manager.hpp>
#include "dock_guides.hpp"
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QSet>
#include <QTimer>

namespace window_system {
class DockOverlay;
class DragLabelPreview;
struct DropTarget {
    WindowId window;
    GroupId group;
    Placement placement = Placement::Center;
    bool toEdge = false;
    bool toEdgeBar = false;
    bool pinned = false;
    bool accepted = false;
    bool splitGroup = true;
    Edge edge = Edge::Left;
    QRect preview;
    QRect groupRect;
    QRect windowRect;
    int insertionIndex = -1;
    // Whether the strip showed the insertion among its sticky tabs.
    bool insertionSticky = false;
    // Whether the pointer is over the strip's own row of sticky tabs.
    std::optional<bool> insertionRow;
    QVector<DockGuide> guides;
};
class DragController final : public QObject {
public:
    explicit DragController(WindowManager* manager);
    ~DragController() override;
    void beginTab(TabId tab, QPoint global);
    void beginWindow(WindowId window, QPoint global);
    void cancel();
    void releaseForMutation();
    [[nodiscard]] bool isActive() const;
    [[nodiscard]] bool hasStableLayout() const { return preparing_ || active_ || finalizing_; }
    void selectTarget();
    void refreshHover();
    [[nodiscard]] QJsonObject stableLayout() const { return originalLayout_; }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    void start(QPoint global);
    bool detach(QPoint global);
    bool updateSourceStrip(QPoint global);
    void update(QPoint global);
    void finish(QPoint global);
    void stop();
    void completeLayoutTransaction();
    [[nodiscard]] bool belongsToDropWindow(QObject* object) const;
    DropTarget hit(QPoint global);
    WindowId candidateAt(QPoint global) const;
    WindowManager* manager_;
    QPointer<DockOverlay> overlay_;
    QPointer<QWidget> capture_;
    QPointer<DragLabelPreview> labelPreview_;
    TabId tab_;
    WindowId wholeWindow_;
    WindowId sourceWindow_;
    WindowId movingWindow_;
    GroupId sourceGroup_;
    Edge sourceEdge_ = Edge::Left;
    bool sourceIsEdge_ = false;
    bool labelOnly_ = false;
    bool transferringCapture_ = false;
    LayoutModel originalModel_;
    QHash<WindowId, WindowOptions> originalOptions_;
    QHash<WindowId, QHash<WindowKind, BarVisibility>> originalWindowVisibility_;
    QHash<WindowId, WindowKind> originalVisibilityKinds_;
    QJsonObject originalSourceStrips_;
    QJsonObject originalSourceInactiveStrips_;
    QJsonObject originalLayout_;
    QHash<WindowId, QRect> originalWindows_;
    QSet<WindowId> originalMaximizedWindows_;
    bool detached_ = false;
    QRect originalGeometry_;
    bool originalMaximized_ = false;
    QPoint startPosition_;
    QPoint windowOffset_;
    QPoint normalOffset_;
    QPoint lastPosition_;
    DropTarget target_;
    WindowId revealCandidate_;
    WindowId revealedWindow_;
    WindowId forcedWindow_;
    QTimer revealTimer_;
    bool active_ = false;
    bool preparing_ = false;
    bool finalizing_ = false;
    bool choosing_ = false;
    bool hoverUpdateQueued_ = false;
    quint64 hoverGeneration_ = 0;
};
} // namespace window_system
