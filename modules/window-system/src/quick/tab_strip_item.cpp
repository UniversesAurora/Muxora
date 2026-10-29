// SPDX-License-Identifier: MIT
#include "tab_strip_item.hpp"

#include "docking/surface_geometry.hpp"
#include "tab_label_item.hpp"

#include <QSet>
#include <utility>

namespace window_system::quick {
namespace {

TabLabelPresentation localized(TabLabelPresentation label, QPoint origin)
{
    label.parts.tab.translate(-origin);
    label.parts.number.translate(-origin);
    label.parts.icon.translate(-origin);
    label.parts.title.translate(-origin);
    label.parts.action.translate(-origin);
    if (label.surface) label.surface = translatedSurfaceGeometry(*label.surface, -origin);
    return label;
}

} // namespace

TabStripItem::TabStripItem(QQuickItem* parent) : QQuickItem(parent) {}

TabStripItem::~TabStripItem() = default;

void TabStripItem::clearLanes()
{
    for (const auto& lane : std::as_const(lanes_)) {
        delete lane.regular;
        delete lane.frozen;
    }
    lanes_.clear();
}

bool TabStripItem::syncSamples(const QVector<TabStripLabelSample>& samples, QQuickItem* clip,
                               QRect origin, QSet<QUuid>& active)
{
    bool accepted = true;
    for (qsizetype index = 0; index < samples.size(); ++index) {
        const auto& sample = samples[index];
        auto* item = labels_.value(sample.id);
        if (!item) {
            item = new TabLabelItem;
            labels_.insert(sample.id, item);
        }
        item->setParentItem(clip);
        item->setZ(index);
        accepted = item->setLabel(localized(sample.label, origin.topLeft()), *frame_->style) && accepted;
        active.insert(sample.id);
    }
    return accepted;
}

bool TabStripItem::syncDepartures(const QVector<TabDepartureSample>& samples, QSet<QUuid>& active)
{
    bool accepted = true;
    for (qsizetype index = 0; index < samples.size(); ++index) {
        const auto& sample = samples[index];
        auto* item = departures_.value(sample.id);
        if (!item) {
            item = new TabLabelItem(this);
            departures_.insert(sample.id, item);
        }
        item->setParentItem(this);
        item->setZ(10 + index);
        accepted = item->setLabel(sample.label, *frame_->style) && accepted;
        active.insert(sample.id);
    }
    return accepted;
}

bool TabStripItem::setFrame(std::shared_ptr<const TabStripFrame> frame)
{
    if (!frame || !frame->style) {
        clearFrame();
        return true;
    }
    frame_ = std::move(frame);
    while (lanes_.size() > frame_->lanes.size()) {
        auto lane = lanes_.takeLast();
        delete lane.regular;
        delete lane.frozen;
    }
    while (lanes_.size() < frame_->lanes.size()) {
        LaneNode lane;
        lane.regular = new QQuickItem(this);
        lane.regular->setClip(true);
        lane.frozen = new QQuickItem(this);
        lane.frozen->setClip(true);
        lanes_.append(lane);
    }

    QSet<QUuid> live;
    QSet<QUuid> departing;
    bool accepted = true;
    for (qsizetype index = 0; index < frame_->lanes.size(); ++index) {
        const auto& source = frame_->lanes[index];
        auto& lane = lanes_[index];
        lane.regular->setPosition(source.regularClip.topLeft());
        lane.regular->setSize(source.regularClip.size());
        lane.regular->setVisible(!source.regularClip.isEmpty());
        lane.frozen->setPosition(source.viewport.topLeft());
        lane.frozen->setSize(source.viewport.size());
        lane.frozen->setVisible(!source.viewport.isEmpty());
        accepted = syncSamples(source.regular, lane.regular, source.regularClip, live) && accepted;
        accepted = syncSamples(source.frozen, lane.frozen, source.viewport, live) && accepted;
    }
    accepted = syncDepartures(frame_->departures, departing) && accepted;

    for (auto it = labels_.begin(); it != labels_.end();) {
        if (live.contains(it.key())) {
            ++it;
            continue;
        }
        it.value()->setParentItem(nullptr);
        it.value()->deleteLater();
        it = labels_.erase(it);
    }
    for (auto it = departures_.begin(); it != departures_.end();) {
        if (departing.contains(it.key())) {
            ++it;
            continue;
        }
        it.value()->setParentItem(nullptr);
        it.value()->deleteLater();
        it = departures_.erase(it);
    }
    return accepted;
}

void TabStripItem::clearFrame()
{
    for (auto* label : std::as_const(labels_)) {
        label->setParentItem(nullptr);
        label->deleteLater();
    }
    for (auto* label : std::as_const(departures_)) {
        label->setParentItem(nullptr);
        label->deleteLater();
    }
    labels_.clear();
    departures_.clear();
    clearLanes();
    frame_.reset();
}

qsizetype TabStripItem::labelCount() const { return labels_.size() + departures_.size(); }
qsizetype TabStripItem::laneCount() const { return lanes_.size(); }

} // namespace window_system::quick
