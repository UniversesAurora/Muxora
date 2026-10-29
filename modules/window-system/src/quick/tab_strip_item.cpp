// SPDX-License-Identifier: MIT
#include "tab_strip_item.hpp"

#include "docking/surface_geometry.hpp"
#include "masked_item.hpp"
#include "overlay_band_item.hpp"
#include "path_item.hpp"
#include "path_stroke_item.hpp"
#include "tab_folder_item.hpp"
#include "tab_label_item.hpp"

#include <QPainterPath>
#include <QSet>
#include <algorithm>
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
    label.presence = label.presence.translated(-origin);
    if (label.surface) label.surface = translatedSurfaceGeometry(*label.surface, -origin);
    return label;
}

void ensurePaths(QVector<PathItem*>& items, qsizetype count, QQuickItem* parent, int z,
                 PathAntialiasing mode = PathAntialiasing::Coverage)
{
    while (items.size() > count) delete items.takeLast();
    while (items.size() < count) {
        auto* item = new PathItem(parent, mode);
        item->setZ(z);
        items.append(item);
    }
    for (qsizetype index = 0; index < items.size(); ++index)
        items[index]->setVisible(index < count);
}

bool syncPath(PathItem* item, QPainterPath& cached, const QPainterPath& path, const QColor& fill)
{
    if (cached != path) {
        cached = path;
        item->setPath(path);
    }
    item->setFill(fill);
    return item->geometryError() == PathGeometryError::None;
}

} // namespace

TabStripItem::TabStripItem(QQuickItem* parent)
    : QQuickItem(parent),
      background_(new PathItem(this, PathAntialiasing::Multisample)),
      sectionSeparator_(new PathItem(this, PathAntialiasing::Multisample)),
      overlay_(new OverlayBandItem(this))
{
    background_->setZ(-10);
    sectionSeparator_->setZ(6);
    sectionSeparator_->setVisible(false);
    overlay_->setZ(20);
}

TabStripItem::~TabStripItem() = default;

void TabStripItem::clearLanes()
{
    for (const auto& lane : std::as_const(lanes_)) {
        delete lane.root;
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
            item = new TabLabelItem(this);
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

bool TabStripItem::syncFolders(const QVector<TabStripFolderSample>& samples, QQuickItem* clip,
                               QRect origin, QSet<QUuid>& active)
{
    bool accepted = true;
    for (const auto& sample : samples) {
        auto* item = folders_.value(sample.id);
        if (!item) {
            item = new TabFolderItem(this);
            item->setZ(-1);
            folders_.insert(sample.id, item);
        }
        item->setParentItem(clip);
        item->setPosition(sample.area.topLeft() - origin.topLeft());
        item->setSize(sample.area.size());
        accepted = item->setFolder(sample.state, *frame_->style, frame_->vertical) && accepted;
        active.insert(sample.id);
    }
    return accepted;
}

bool TabStripItem::syncDecorations()
{
    bool accepted = true;
    QPainterPath background;
    if (!frame_->background.isEmpty()) background.addRect(frame_->background);
    accepted = syncPath(background_, backgroundPath_, background, frame_->style->palette.strip) && accepted;
    background_->setVisible(!frame_->background.isEmpty());

    ensurePaths(separators_, frame_->separators.size(), this, 0, PathAntialiasing::Multisample);
    separatorPaths_.resize(separators_.size());
    for (qsizetype index = 0; index < frame_->separators.size(); ++index) {
        QPainterPath path;
        path.addRect(frame_->separators[index]);
        accepted = syncPath(separators_[index], separatorPaths_[index], path, frame_->style->palette.separator) && accepted;
    }

    const bool showSection = !frame_->sectionSeparator.isEmpty();
    sectionSeparator_->setVisible(showSection);
    if (showSection) {
        QPainterPath path;
        path.addRect(frame_->sectionSeparator);
        accepted = syncPath(sectionSeparator_, sectionSeparatorPath_, path,
                 frame_->vertical ? frame_->style->palette.sectionSeparator.resolve(frame_->windowActive)
                                  : frame_->style->palette.separator) && accepted;
    }

    ensurePaths(scrollbars_, frame_->scrollbars.size(), this, 5);
    scrollbarPaths_.resize(scrollbars_.size());
    for (qsizetype index = 0; index < frame_->scrollbars.size(); ++index) {
        const auto& source = frame_->scrollbars[index];
        const qreal radius = std::min(source.thumb.width(), source.thumb.height()) / 2.0;
        QPainterPath path;
        path.addRoundedRect(QRectF(source.thumb), radius, radius);
        auto color = frame_->style->palette.text;
        color.setAlphaF(color.alphaF() * (source.emphasized ? 0.38 : 0.22));
        accepted = syncPath(scrollbars_[index], scrollbarPaths_[index], path, color) && accepted;
        scrollbars_[index]->setOpacity(source.opacity);
    }

    if (frame_->overlayShadow.isEmpty() || frame_->shadowOpacity <= 0)
        overlay_->clearBand();
    else
        overlay_->setBand(frame_->overlayShadow, frame_->shadowAbove,
                          frame_->style->palette.elevation, frame_->shadowOpacity);

    return accepted;
}

bool TabStripItem::syncLaneDecorations(LaneNode& lane, const TabStripLaneFrame& source)
{
    bool accepted = true;
    const auto origin = source.regularClip.topLeft();
    ensurePaths(lane.lines, source.folderLines.size(), lane.decoration, 0);
    lane.linePaths.resize(lane.lines.size());
    for (qsizetype index = 0; index < source.folderLines.size(); ++index) {
        const auto& line = source.folderLines[index];
        const qreal radius = std::min(line.area.width(), line.area.height()) / 2.0;
        QPainterPath path;
        path.addRoundedRect(QRectF(line.area.translated(-origin)), radius, radius);
        accepted = syncPath(lane.lines[index], lane.linePaths[index], path, line.color) && accepted;
    }
    lane.outline->setVisible(bool(source.folderOutline));
    if (source.folderOutline) {
        const auto& outline = *source.folderOutline;
        const TabPainter painter(frame_->style->metrics, frame_->style->palette);
        lane.outline->setSize(source.regularClip.size());
        accepted = lane.outline->setStroke(
            painter.folderOutlinePath(outline.area.translated(-origin), outline.shape),
            outline.color, frame_->style->metrics.folderOutline);
    }
    auto fades = source.fades;
    for (auto& fade : fades) {
        fade.area.translate(-source.viewport.topLeft());
        if (!fade.opacityRamp.isNull()) fade.opacityRamp.translate(-source.viewport.topLeft());
    }
    return lane.root->setFades(fades) && accepted;
}

bool TabStripItem::setFrame(std::shared_ptr<const TabStripFrame> frame)
{
    if (!frame || !frame->style) {
        clearFrame();
        return true;
    }
    frame_ = std::move(frame);
    bool accepted = syncDecorations();
    while (lanes_.size() > frame_->lanes.size()) {
        auto lane = lanes_.takeLast();
        // Preserve the same-window scene resources while the clipping parent
        // disappears. QObject ownership belongs to the strip, not the lane.
        for (auto* parent : {lane.regular, lane.frozen})
            for (auto* child : parent->childItems()) child->setParentItem(this);
        delete lane.root;
    }
    while (lanes_.size() < frame_->lanes.size()) {
        LaneNode lane;
        lane.root = new MaskedItem(this);
        lane.root->setZ(1);
        lane.root->setClip(true);
        lane.regular = new QQuickItem(lane.root->contentItem());
        lane.regular->setClip(true);
        lane.frozen = new QQuickItem(lane.root->contentItem());
        lane.frozen->setZ(1);
        lane.frozen->setClip(true);
        lane.decoration = new QQuickItem(lane.root->contentItem());
        lane.decoration->setZ(2);
        lane.decoration->setClip(true);
        lane.outline = new PathStrokeItem(lane.decoration);
        lane.outline->setZ(1);
        lanes_.append(lane);
    }

    QSet<QUuid> live;
    QSet<QUuid> folderLive;
    QSet<QUuid> departing;
    for (qsizetype index = 0; index < frame_->lanes.size(); ++index) {
        const auto& source = frame_->lanes[index];
        auto& lane = lanes_[index];
        lane.root->setPosition(source.viewport.topLeft());
        lane.root->setSize(source.viewport.size());
        lane.regular->setPosition(source.regularClip.topLeft() - source.viewport.topLeft());
        lane.regular->setSize(source.regularClip.size());
        lane.regular->setVisible(!source.regularClip.isEmpty());
        lane.frozen->setPosition({});
        lane.frozen->setSize(source.viewport.size());
        lane.frozen->setVisible(!source.viewport.isEmpty());
        lane.decoration->setPosition(lane.regular->position());
        lane.decoration->setSize(lane.regular->size());
        accepted = syncLaneDecorations(lane, source) && accepted;
        accepted = syncSamples(source.regular, lane.regular, source.regularClip, live) && accepted;
        accepted = syncSamples(source.frozen, lane.frozen, source.viewport, live) && accepted;
        accepted = syncFolders(source.folders, lane.regular, source.regularClip, folderLive) && accepted;
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
    for (auto it = folders_.begin(); it != folders_.end();) {
        if (folderLive.contains(it.key())) {
            ++it;
            continue;
        }
        it.value()->setParentItem(nullptr);
        it.value()->deleteLater();
        it = folders_.erase(it);
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
    for (auto* folder : std::as_const(folders_)) {
        folder->setParentItem(nullptr);
        folder->deleteLater();
    }
    labels_.clear();
    folders_.clear();
    departures_.clear();
    clearLanes();
    frame_.reset();
    background_->setVisible(false);
    sectionSeparator_->setVisible(false);
    for (auto* item : std::as_const(separators_)) item->setVisible(false);
    for (auto* item : std::as_const(scrollbars_)) item->setVisible(false);
    overlay_->clearBand();
}

qsizetype TabStripItem::labelCount() const { return labels_.size() + departures_.size(); }
qsizetype TabStripItem::folderCount() const { return folders_.size(); }
quint64 TabStripItem::overlayTextureUploads() const { return overlay_->textureUploads(); }
qsizetype TabStripItem::laneCount() const { return lanes_.size(); }

} // namespace window_system::quick
