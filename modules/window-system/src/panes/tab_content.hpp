#pragma once
#include <window_system/window_manager.hpp>
#include <QFrame>
#include <QHash>
#include <QPointer>
#include "focus_projection.hpp"

class QSplitter;
class QVBoxLayout;
namespace window_system {
class TabContent final : public QFrame {
public:
    TabContent(WindowManager* manager, TabId tab, QWidget* parent);
    void rebuild(const TabRecord& record, const QHash<PaneId, QPointer<QWidget>>& contents,
                 const QHash<PaneId, QPointer<QWidget>>& paneTopBars = {},
                 const QHash<PaneId, QPointer<QWidget>>& paneBottomBars = {});
    // True when rebuild() would recreate the same splitters and frames: the
    // same pane tree, the same content and pane bars per pane, and the same
    // splitter metric. Such a view keeps its widgets and mounted content.
    [[nodiscard]] bool canReuse(const TabRecord& record, const QHash<PaneId, QPointer<QWidget>>& contents,
                                const QHash<PaneId, QPointer<QWidget>>& paneTopBars,
                                const QHash<PaneId, QPointer<QWidget>>& paneBottomBars) const;
    void setActivePane(PaneId pane);
    void setFocusedPane(PaneId pane);
    void setBarsVisible(bool visible);
    void setBarSharing(PaneBarSharing top, PaneBarSharing bottom);
    void setTopBar(QWidget* widget);
    void setBottomBar(QWidget* widget);
    QWidget* takeTopBar(QWidget* owner = nullptr);
    QWidget* takeBottomBar(QWidget* owner = nullptr);
    [[nodiscard]] QHash<PaneId, QWidget*> paneFrames() const;
    // The splitter built for a pane-tree split node, or null.
    [[nodiscard]] QSplitter* splitterFor(QUuid node) const;
private:
    QWidget* buildNode(LayoutTree node, const TabRecord& record,
                       const QHash<PaneId, QPointer<QWidget>>& contents);
    void parkBuiltWidgets();
    void updateBars();
    void replaceVisibleBar(QPointer<QWidget>& slot, QWidget* next, bool top);
    void placePaneBars(const QHash<PaneId, QPointer<QWidget>>& bars,
                       PaneBarSharing sharing, QWidget* shared, bool top);
    WindowManager* manager_;
    TabId tab_;
    QVBoxLayout* layout_;
    QPointer<QWidget> tree_;
    QPointer<QWidget> top_;
    QPointer<QWidget> bottom_;
    QPointer<QWidget> visibleTop_;
    QPointer<QWidget> visibleBottom_;
    QHash<PaneId, QPointer<QWidget>> paneTopBars_;
    QHash<PaneId, QPointer<QWidget>> paneBottomBars_;
    PaneId activePane_;
    PaneBarSharing topSharing_ = PaneBarSharing::Shared;
    PaneBarSharing bottomSharing_ = PaneBarSharing::Shared;
    QHash<PaneId, QWidget*> frames_;
    QHash<QUuid, QPointer<QSplitter>> splitters_;
    // Inputs of the last full build, in leaf order.
    struct BuiltPane {
        PaneId pane;
        QPointer<QWidget> content;
        QPointer<QWidget> topBar;
        QPointer<QWidget> bottomBar;
    };
    LayoutTree builtTree_;
    QVector<BuiltPane> builtPanes_;
    int builtSplitterWidth_ = -1;
    PaneId focusedPane_;
    FocusProjection focusProjection_;
    bool barsVisible_ = true;
};
} // namespace window_system
