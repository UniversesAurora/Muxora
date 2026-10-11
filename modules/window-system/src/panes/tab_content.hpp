#pragma once
#include <window_system/window_manager.hpp>
#include <QFrame>
#include <QHash>
#include <QPointer>

class QVBoxLayout;
namespace window_system {
class TabContent final : public QFrame {
public:
    TabContent(WindowManager* manager, TabId tab, QWidget* parent);
    void rebuild(const TabRecord& record, const QHash<PaneId, QPointer<QWidget>>& contents);
    void setTopBar(QWidget* widget);
    void setBottomBar(QWidget* widget);
    QWidget* takeTopBar();
    QWidget* takeBottomBar();
    [[nodiscard]] QHash<PaneId, QWidget*> paneFrames() const;
private:
    QWidget* buildNode(LayoutTree node, const TabRecord& record,
                       const QHash<PaneId, QPointer<QWidget>>& contents);
    WindowManager* manager_;
    TabId tab_;
    QVBoxLayout* layout_;
    QPointer<QWidget> tree_;
    QPointer<QWidget> top_;
    QPointer<QWidget> bottom_;
    QHash<PaneId, QWidget*> frames_;
};
} // namespace window_system
