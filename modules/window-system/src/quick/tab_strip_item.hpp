// SPDX-License-Identifier: MIT
#pragma once

#include "style/tab_strip_frame.hpp"

#include <QHash>
#include <QPainterPath>
#include <QQuickItem>
#include <QSet>
#include <QUuid>
#include <QVector>
#include <memory>

namespace window_system::quick {

class TabLabelItem;
class TabFolderItem;
class OverlayBandItem;
class MaskedItem;
class PathItem;
class PathStrokeItem;

// Retained labels for one published strip frame. Each lane has independent
// rectangular clipping for its regular and frozen runs; label items remain
// keyed by stable ID while frames change.
class TabStripItem final : public QQuickItem {
public:
    explicit TabStripItem(QQuickItem* parent = nullptr);
    ~TabStripItem() override;

    bool setFrame(std::shared_ptr<const TabStripFrame> frame);
    void clearFrame();
    [[nodiscard]] qsizetype labelCount() const;
    [[nodiscard]] qsizetype folderCount() const;
    [[nodiscard]] quint64 overlayTextureUploads() const;
    [[nodiscard]] qsizetype laneCount() const;

private:
    struct LaneNode {
        MaskedItem* root = nullptr;
        QQuickItem* regular = nullptr;
        QQuickItem* frozen = nullptr;
        QQuickItem* decoration = nullptr;
        QVector<PathItem*> lines;
        QVector<QPainterPath> linePaths;
        PathStrokeItem* outline = nullptr;
    };
    void clearLanes();
    bool syncSamples(const QVector<TabStripLabelSample>& samples, QQuickItem* clip,
                     QRect origin, QSet<QUuid>& active);
    bool syncDepartures(const QVector<TabDepartureSample>& samples, QSet<QUuid>& active);
    bool syncFolders(const QVector<TabStripFolderSample>& samples, QQuickItem* clip,
                     QRect origin, QSet<QUuid>& active);
    bool syncDecorations();
    bool syncLaneDecorations(LaneNode& lane, const TabStripLaneFrame& source);
    std::shared_ptr<const TabStripFrame> frame_;
    QVector<LaneNode> lanes_;
    QHash<QUuid, TabLabelItem*> labels_;
    QHash<QUuid, TabFolderItem*> folders_;
    QHash<QUuid, TabLabelItem*> departures_;
    PathItem* background_ = nullptr;
    PathItem* sectionSeparator_ = nullptr;
    QVector<PathItem*> separators_;
    QVector<PathItem*> scrollbars_;
    OverlayBandItem* overlay_ = nullptr;
    QPainterPath backgroundPath_;
    QPainterPath sectionSeparatorPath_;
    QVector<QPainterPath> separatorPaths_;
    QVector<QPainterPath> scrollbarPaths_;
};

} // namespace window_system::quick
