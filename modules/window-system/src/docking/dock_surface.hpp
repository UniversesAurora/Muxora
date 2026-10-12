#pragma once
#include <window_system/window_manager.hpp>
#include <QFrame>
#include <QHash>
#include <QPointer>

class QSplitter;
namespace window_system {
class GroupTabs;
class DockSurface final : public QFrame {
public:
    DockSurface(WindowManager* manager, WindowId window, QWidget* parent);
    void rebuild(const QHash<TabId, QPointer<TabContent>>& contents);
    [[nodiscard]] QHash<GroupId, QRect> groupRects() const;
    [[nodiscard]] GroupId groupAt(QPoint global) const;
    [[nodiscard]] QHash<GroupId, QRect> tabBarRects() const;
    [[nodiscard]] GroupId tabBarAt(QPoint global) const;
    [[nodiscard]] int tabInsertionIndex(GroupId group, QPoint global) const;
    [[nodiscard]] QHash<Edge, QRect> edgeBarRects() const;
    void setDragHover(QPoint global);
    void selectTab(TabId tab);
    void showEdgeGroup(GroupId group, TabId tab = {});
    void closePeek();
    [[nodiscard]] QRect centralRectGlobal() const;
protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    QWidget* buildTree(LayoutTree node, const QHash<TabId, QPointer<TabContent>>& contents);
    GroupTabs* buildGroup(GroupId group, const QHash<TabId, QPointer<TabContent>>& contents);
    QWidget* buildEdge(Edge edge, const QHash<TabId, QPointer<TabContent>>& contents);
    QWidget* buildPinned(Edge edge, const QHash<TabId, QPointer<TabContent>>& contents);
    void placePeek();
    void savePinnedExtents();
    void restorePinnedExtents();
    WindowManager* manager_;
    WindowId window_;
    QPointer<QWidget> root_;
    QPointer<QWidget> center_;
    QPointer<QFrame> peek_;
    GroupId peekGroup_;
    TabId peekTab_;
    QHash<GroupId, QPointer<GroupTabs>> groups_;
    QHash<Edge, QPointer<QWidget>> edgeBars_;
    QHash<Edge, QPointer<QSplitter>> pinnedRegions_;
    QPointer<QSplitter> horizontal_;
    QPointer<QSplitter> vertical_;
    QHash<TabId, QPointer<TabContent>> contents_;
};
} // namespace window_system
