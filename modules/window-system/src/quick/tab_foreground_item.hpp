// SPDX-License-Identifier: MIT
#pragma once

#include "docking/tab_foreground_frame.hpp"
#include "shadow_stats.hpp"
#include <QHash>
#include <QQuickItem>

namespace window_system::quick {

class AnchorShadowItem;
class PathItem;
class TabCarrierItem;
class TabFolderItem;
class TabLabelItem;

// The window-level plane consumes owned strip coordinates. There is no strip
// viewport clip here: lifted shadows and labels can extend over neighbouring
// surfaces, exactly as in the raster foreground.
class TabForegroundItem final : public QQuickItem {
public:
    explicit TabForegroundItem(QQuickItem* parent = nullptr);
    bool setFrame(std::shared_ptr<const TabForegroundFrame> frame);
    void clearFrame();
    [[nodiscard]] ShadowRenderStats shadowStats() const;

private:
    struct LabelEntry { TabLabelItem* item; quint64 pass = 0; };
    bool syncLabels(const QVector<TabStripLabelSample>& samples, qreal z);
    void syncShadows(const TabForegroundFrame* previous);
    bool syncFolder();
    std::shared_ptr<const TabForegroundFrame> frame_;
    QHash<QUuid, LabelEntry> labels_;
    quint64 pass_ = 0;
    QVector<AnchorShadowItem*> shadows_;
    TabCarrierItem* incoming_ = nullptr;
    TabFolderItem* folder_ = nullptr;
    PathItem* folderLine_ = nullptr;
    QSize folderLineSize_;
};

} // namespace window_system::quick
