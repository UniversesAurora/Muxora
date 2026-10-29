// SPDX-License-Identifier: MIT
#include "tab_foreground_item.hpp"

#include "anchor_shadow_item.hpp"
#include "path_item.hpp"
#include "tab_carrier_item.hpp"
#include "tab_folder_item.hpp"
#include "tab_label_item.hpp"
#include <algorithm>
#include <utility>

namespace window_system::quick {

TabForegroundItem::TabForegroundItem(QQuickItem* parent)
    : QQuickItem(parent), incoming_(new TabCarrierItem(this)),
      folder_(new TabFolderItem(this)), folderLine_(new PathItem(this))
{
    incoming_->setZ(1);
    folder_->setZ(2);
    folderLine_->setZ(4);
    folder_->setVisible(false);
    folderLine_->setVisible(false);
}

bool TabForegroundItem::syncLabels(const QVector<TabStripLabelSample>& samples, qreal z)
{
    bool accepted = true;
    for (qsizetype index = 0; index < samples.size(); ++index) {
        const auto& sample = samples[index];
        auto found = labels_.find(sample.id);
        if (found == labels_.end()) found = labels_.insert(sample.id, {new TabLabelItem(this)});
        found->pass = pass_;
        found->item->setZ(z + qreal(index) / (samples.size() + 1));
        accepted = found->item->setLabel(sample.label, *frame_->style) && accepted;
    }
    return accepted;
}

void TabForegroundItem::syncShadows(const TabForegroundFrame* previous)
{
    while (shadows_.size() > frame_->shadows.size()) delete shadows_.takeLast();
    while (shadows_.size() < frame_->shadows.size()) {
        auto* item = new AnchorShadowItem(this);
        item->setTransformOrigin(QQuickItem::TopLeft);
        shadows_.append(item);
    }
    for (qsizetype index = 0; index < shadows_.size(); ++index) {
        auto* item = shadows_[index];
        const auto& sample = frame_->shadows[index];
        item->setZ(qreal(index) / (shadows_.size() + 1));
        item->setPosition(sample.presence.offset);
        item->setScale(sample.presence.scale);
        item->setOpacity(sample.presence.opacity);
        const auto* old = previous && index < previous->shadows.size() ? &previous->shadows[index] : nullptr;
        if (!old || previous->elevation != frame_->elevation || old->geometry != sample.geometry
            || old->body != sample.body || old->anchor != sample.anchor || old->incremental != sample.incremental)
            item->setShadow(sample.geometry, sample.body, sample.anchor, frame_->elevation, sample.incremental);
    }
}

bool TabForegroundItem::syncFolder()
{
    folder_->setVisible(bool(frame_->folder));
    bool accepted = true;
    if (frame_->folder) {
        const auto& sample = *frame_->folder;
        folder_->setPosition(sample.area.topLeft());
        folder_->setSize(sample.area.size());
        accepted = folder_->setFolder(sample.state, *frame_->style, frame_->vertical);
    } else folder_->clearFolder();
    folderLine_->setVisible(bool(frame_->folderLine));
    if (frame_->folderLine) {
        const auto& line = *frame_->folderLine;
        folderLine_->setPosition(line.area.topLeft());
        if (folderLineSize_ != line.area.size()) {
            folderLineSize_ = line.area.size();
            const qreal radius = std::min(line.area.width(), line.area.height()) / 2.0;
            QPainterPath path;
            path.addRoundedRect(QRectF(QPointF(), line.area.size()), radius, radius);
            accepted = folderLine_->setPath(path) && accepted;
        }
        folderLine_->setFill(line.color);
        accepted = folderLine_->geometryError() == PathGeometryError::None && accepted;
    }
    return accepted;
}

bool TabForegroundItem::setFrame(std::shared_ptr<const TabForegroundFrame> frame)
{
    if (!frame || !frame->style) { clearFrame(); return true; }
    const auto previous = std::move(frame_);
    frame_ = std::move(frame);
    ++pass_;
    syncShadows(previous.get());
    bool accepted = true;
    if (frame_->incoming) {
        const auto& sample = *frame_->incoming;
        incoming_->setPosition(sample.origin);
        accepted = incoming_->setVisual(sample.visual, &sample.geometry);
    } else incoming_->clearVisual();
    accepted = syncFolder() && accepted;
    accepted = syncLabels(frame_->carried, 3) && accepted;
    accepted = syncLabels(frame_->settling, 5) && accepted;
    for (auto it = labels_.begin(); it != labels_.end();) {
        if (it->pass == pass_) { ++it; continue; }
        delete it->item;
        it = labels_.erase(it);
    }
    // An unsupported input never leaves a partial old/new foreground visible.
    if (!accepted) clearFrame();
    return accepted;
}

void TabForegroundItem::clearFrame()
{
    for (const auto& entry : std::as_const(labels_)) delete entry.item;
    labels_.clear();
    for (auto* item : std::as_const(shadows_)) delete item;
    shadows_.clear();
    incoming_->clearVisual();
    folder_->clearFolder();
    folder_->setVisible(false);
    folderLine_->setVisible(false);
    frame_.reset();
}

ShadowRenderStats TabForegroundItem::shadowStats() const
{
    ShadowRenderStats result;
    for (const auto* shadow : shadows_) {
        const auto stats = shadow->renderStats();
        result.textureUploads += stats.textureUploads;
        result.imageBytesUploaded += stats.imageBytesUploaded;
        result.geometryUpdates += stats.geometryUpdates;
    }
    return result;
}

} // namespace window_system::quick
