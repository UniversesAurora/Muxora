// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/layout_model.hpp>

#include <QHash>
#include <QPointer>
#include <QQuickItem>

namespace window_system {
class WindowManager;
}

namespace window_system::quick {

class ContentHost;
class SplitTreeItem;

// One tab's page: its pane tree, each pane's content mounted in a frame
// that the content fills. Pane content belongs to the content host and
// keeps its identity when the tab moves between groups or windows.
class TabContentItem final : public QQuickItem {
    Q_OBJECT
public:
    TabContentItem(WindowManager& manager, ContentHost& host, TabId tab, QQuickItem* parent = nullptr);
    ~TabContentItem() override;
    [[nodiscard]] TabId tab() const { return tab_; }
    // Rebuilds the pane tree from the model and mounts present content.
    void sync();
    [[nodiscard]] QSize minimumSize() const;

private:
    QQuickItem* frame(PaneId pane);

    WindowManager& manager_;
    ContentHost& host_;
    TabId tab_;
    SplitTreeItem* split_;
    QHash<PaneId, QPointer<QQuickItem>> frames_;
};

} // namespace window_system::quick
