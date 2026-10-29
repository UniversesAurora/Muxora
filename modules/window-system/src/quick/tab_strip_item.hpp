// SPDX-License-Identifier: MIT
#pragma once

#include "style/tab_strip_frame.hpp"

#include <QHash>
#include <QQuickItem>
#include <QSet>
#include <QUuid>
#include <QVector>
#include <memory>

namespace window_system::quick {

class TabLabelItem;

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
    [[nodiscard]] qsizetype laneCount() const;

private:
    struct LaneNode {
        QQuickItem* regular = nullptr;
        QQuickItem* frozen = nullptr;
    };
    void clearLanes();
    bool syncSamples(const QVector<TabStripLabelSample>& samples, QQuickItem* clip,
                     QRect origin, QSet<QUuid>& active);
    bool syncDepartures(const QVector<TabDepartureSample>& samples, QSet<QUuid>& active);
    std::shared_ptr<const TabStripFrame> frame_;
    QVector<LaneNode> lanes_;
    QHash<QUuid, TabLabelItem*> labels_;
    QHash<QUuid, TabLabelItem*> departures_;
};

} // namespace window_system::quick
