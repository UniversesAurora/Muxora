#pragma once
#include <window_system/window_manager.hpp>
#include <QFrame>
#include <QHash>
#include <QPointer>

class QTabWidget;
namespace window_system {
class DockSurface final : public QFrame {
public:
    DockSurface(WindowManager* manager, WindowId window, QWidget* parent);
    void rebuild(const QHash<TabId, QPointer<TabContent>>& contents);
    [[nodiscard]] QHash<GroupId, QRect> groupRects() const;
    [[nodiscard]] GroupId groupAt(QPoint global) const;
    void selectTab(TabId tab);
    void showEdgeGroup(GroupId group, TabId tab = {});
    void closePeek();
    [[nodiscard]] QRect centralRectGlobal() const;
protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    QWidget* buildTree(LayoutTree node, const QHash<TabId, QPointer<TabContent>>& contents);
    QTabWidget* buildGroup(GroupId group, const QHash<TabId, QPointer<TabContent>>& contents);
    QWidget* buildEdge(Edge edge, const QHash<TabId, QPointer<TabContent>>& contents);
    QWidget* buildPinned(Edge edge, const QHash<TabId, QPointer<TabContent>>& contents);
    void placePeek();
    WindowManager* manager_;
    WindowId window_;
    QPointer<QWidget> root_;
    QPointer<QWidget> center_;
    QPointer<QFrame> peek_;
    GroupId peekGroup_;
    QHash<GroupId, QPointer<QTabWidget>> groups_;
    QHash<TabId, QPointer<TabContent>> contents_;
};
} // namespace window_system
