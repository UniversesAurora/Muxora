// SPDX-License-Identifier: MIT
#include "tab_carrier_item.hpp"

#include "tab_label_item.hpp"
#include "style/tab_carrier_label.hpp"
#include <algorithm>
#include <utility>

namespace window_system::quick {

TabCarrierItem::TabCarrierItem(QQuickItem* parent) : QQuickItem(parent) {}

void TabCarrierItem::resizePieces(qsizetype count)
{
    while (pieces_.size() > size_t(count)) {
        auto* item = pieces_.back().item;
        // Keep surviving labels in this window while their old visual parent
        // disappears. Their QObject owner remains the carrier throughout.
        for (auto* child : item->childItems()) child->setParentItem(this);
        delete item;
        pieces_.pop_back();
    }
    while (pieces_.size() < size_t(count)) {
        Piece piece;
        piece.item = new QQuickItem(this);
        piece.item->setZ(qreal(pieces_.size()));
        pieces_.push_back(std::move(piece));
    }
}

bool TabCarrierItem::syncPiece(Piece& target, const TabCarrierPiece& source,
                              const SurfaceGeometry* geometry)
{
    target.item->setPosition(source.offset);
    if (!source.frame || !source.frame->content) {
        target.content.reset();
        return true;
    }
    const auto& frame = *source.frame;
    const auto& content = *frame.content;
    if (target.content != frame.content) {
        target.content = frame.content;
        target.style.metrics = content.metrics;
        target.style.palette = content.palette;
    }
    const auto localGeometry = geometry ? translatedSurfaceGeometry(*geometry, -source.offset) : SurfaceGeometry{};
    const auto count = std::min(content.labels.size(), frame.samples.size());
    bool accepted = true;
    for (qsizetype index = 0; index < count; ++index) {
        const auto& label = content.labels[index];
        auto& entry = labels_[label.id];
        if (!entry.item) entry.item = new TabLabelItem(this);
        entry.used = pass_;
        entry.item->setParentItem(target.item);
        entry.item->setZ(index);
        accepted = entry.item->setLabel(carrierLabelPresentation(label, frame.samples[index],
            geometry ? &localGeometry : nullptr), target.style) && accepted;
    }
    return accepted;
}

bool TabCarrierItem::setVisual(const TabDragVisual& visual, const SurfaceGeometry* geometry)
{
    if (visual.empty()) {
        clearVisual();
        return true;
    }
    ++pass_;
    resizePieces(visual.pieces.size());
    setSize(visual.size);
    bool accepted = true;
    for (qsizetype index = 0; index < visual.pieces.size(); ++index)
        accepted = syncPiece(pieces_[size_t(index)], visual.pieces[index], geometry) && accepted;
    for (auto it = labels_.begin(); it != labels_.end();) {
        if (it->used == pass_) { ++it; continue; }
        it->item->setParentItem(nullptr);
        it->item->deleteLater();
        it = labels_.erase(it);
    }
    return accepted;
}

void TabCarrierItem::clearVisual()
{
    for (const auto& entry : std::as_const(labels_)) {
        entry.item->setParentItem(nullptr);
        entry.item->deleteLater();
    }
    labels_.clear();
    resizePieces(0);
}

qsizetype TabCarrierItem::labelCount() const { return labels_.size(); }

} // namespace window_system::quick
