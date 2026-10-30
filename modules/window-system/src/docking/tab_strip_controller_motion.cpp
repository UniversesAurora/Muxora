// SPDX-License-Identifier: MIT
#include "tab_strip_controller.hpp"
#include "style/tab_appearance_p.hpp"
#include <window_system/interface_appearance.hpp>
#include <algorithm>

namespace window_system {

namespace {
// Match a changing row's empty slots to the other layout. Neighbor edges,
// including both outer ends, are resolved in two linear passes, not per frame.
LabelPositions collapsedSlots(const QVector<QPair<QUuid, QRect>>& row, const LabelPositions& other)
{
    QVector<std::optional<QRect>> right(row.size());
    std::optional<QRect> next;
    for (int index = int(row.size()) - 1; index >= 0; --index) {
        right[index] = next;
        if (const auto found = other.constFind(row.at(index).first); found != other.cend()) next = *found;
    }
    LabelPositions closedRects;
    std::optional<QRect> previous;
    for (int index = 0; index < row.size(); ++index) {
        const auto& [id, rect] = row.at(index);
        if (const auto found = other.constFind(id); found != other.cend()) { previous = *found; continue; }
        const auto after = right.at(index);
        const int x = previous && after ? (previous->right() + 1 + after->left()) / 2
            : previous ? previous->right() + 1 : after ? after->left() : rect.center().x();
        closedRects.insert(id, QRect(x, rect.y(), 0, rect.height()));
    }
    return closedRects;
}


} // namespace

qreal TabStripController::selectionWeight(int index) const
{
    if (index < 0 || index != current_ || index >= count() || !incoming_.visual().empty()) return 0;
    return surfaceMotion_.selectionWeight();
}

std::optional<QUuid> TabStripController::currentLabelId() const
{
    return current_ < 0 ? std::nullopt : std::optional(tabData(current_).toUuid());
}

std::pair<qreal, qreal> TabStripController::attachmentValues() const { return surfaceMotion_.attachmentValues(); }

SurfaceGeometry TabStripController::anchorGeometry(QRectF anchor, QRectF visible, bool compact, bool lifted, bool transitioning) const
{
    if (visible.isEmpty()) return {};
    const auto edge = vertical() ? (placement_ == TabBarPlacement::Left ? SurfaceEdge::Left : SurfaceEdge::Right)
        : placement_ == TabBarPlacement::Top ? SurfaceEdge::Top : SurfaceEdge::Bottom;
    const bool columnOverlay = vertical() && surfaceJoin_ < 1;
    const bool rowOverlay = !vertical() && overlayActive() && !lifted;
    const bool foreground = columnOverlay || rowOverlay;
    auto body = surfaceBody_;
    // A hovering column covers the card instead of narrowing the label back
    // towards the card's original edge. Its small shoulder retracts with the
    // column's own progress, then the whole label casts its own shadow.
    if (columnOverlay) {
        const int gap = metrics_.verticalInset + metrics_.columnGap;
        if (placement_ == TabBarPlacement::Left) body.moveLeft(anchor.right() + gap);
        else body.moveRight(anchor.left() - gap);
    }
    const bool adjacent = vertical() || (placement_ == TabBarPlacement::Top
        ? std::abs(anchor.bottom() - body.top()) <= metrics_.outerMargin + 1
        : std::abs(anchor.top() - body.bottom()) <= metrics_.outerMargin + 1);
    const bool covered = !vertical() && !adjacent;
    auto values = transitioning ? attachmentValues() : std::pair<qreal, qreal>{adjacent && !compact ? 1 : 0, covered ? 1 : 0};
    const qreal panel = transitioning ? (columnOverlay ? surfaceJoin_ : rowOverlay ? 1 - rowsState_.value() : 1) : 1;
    SurfaceGeometryInput input;
    input.body = body; input.bodyRadius = metrics_.cardRadius;
    input.anchor = anchor; input.visibleAnchor = visible;
    input.edge = edge; input.anchorRadius = metrics_.radius; input.shoulderRadius = metrics_.controlRadius;
    input.joinProgress = values.first * panel;
    // A shoulder may change shape, but cannot stretch across another row.
    input.adjacent = adjacent && input.joinProgress > 0;
    input.covered = values.second * panel > 0; input.coveredProgress = values.second * panel;
    input.compact = compact;
    auto& cached = anchorGeometryCache_[transitioning ? 1 : 0];
    if (!cached || !(cached->first == input)) cached = std::pair{input, surfaceGeometry(input)};
    auto geometry = cached->second;
    if (foreground && transitioning) {
        geometry.body = {}; geometry.bodyPath = {}; geometry.mergedPath = geometry.anchorPath;
        geometry.joined = false;
    }
    return geometry;
}

QRect TabStripController::activeDragRect() const
{
    if (!incoming_.visual().empty()) {
        const auto incoming = incomingRect();
        return incoming_.visual().active.isEmpty() ? incoming : incoming_.visual().active.translated(incoming.topLeft());
    }
    const auto lifted = liftedRect();
    if (draggedFolder_ < 0 || draggedFolder_ >= folders_.size()) return lifted;
    const auto& folder = folders_.at(draggedFolder_);
    if (current_ < folder.first || current_ >= folder.first + folder.count || folder.collapsed) return {};
    if (vertical()) return QRect(lifted.left() + metrics_.folderIndent,
        lifted.top() + (current_ - folder.first + 1) * (metrics_.rowHeight + metrics_.rowGap),
        lifted.width() - metrics_.folderIndent, metrics_.rowHeight);
    int left = lifted.left() + folderHeaderExtent(folder);
    for (int index = folder.first; index < current_; ++index) left += tabExtent(index, false);
    return QRect(left, lifted.top(), tabExtent(current_, false), metrics_.rowHeight);
}

void TabStripController::syncAttachment()
{
    QRect anchor;
    bool compact = false;
    if (!incoming_.visual().empty()) {
        anchor = activeDragRect();
        compact = dropSticky_ && compactStickyTiles();
    } else if (current_ >= 0 && current_ < layout_.tabs.size()) {
        anchor = lifted_.active() && (dragged_ == current_ || draggedFolder_ >= 0) ? activeDragRect() : animatedRect(current_);
        compact = shapeOf(current_) == TabShape::Tile;
        if (lifted_.active() && dragged_ == current_) compact = dropSticky_ && compactStickyTiles();
    }
    const auto geometry = anchorGeometry(anchor, anchor.intersected(rect()), compact,
        !incoming_.visual().empty() || lifted_.active(), false);
    const std::pair<qreal, qreal> target{geometry.joinProgress, geometry.coveredProgress};
    const auto owner = !incoming_.visual().empty() ? incoming_.visual().identity : tabData(current_).toUuid();
    const auto timestamp = timestampNs_;
    (void)surfaceMotion_.syncAttachment(owner, target, isVisible() && animationsEnabled(), timestamp);
}

void TabStripController::surfaceFrameChanged()
{
    invalidateCarrier(false);
    requestRepaint();
    changes_.surface = true;
}

TabStripController::SurfaceState TabStripController::captureSurfaceState() const { return surfaceMotion_.capture(); }

void TabStripController::inheritSurface(const SurfaceState& previous, bool animate)
{
    const auto timestamp = timestampNs_;
    (void)surfaceMotion_.inherit(previous, currentLabelId(), animate, timestamp);
    surfaceFrameChanged();
}

void TabStripController::setSurfaceContext(QRectF body, qreal focus, qreal join)
{
    if (surfaceBody_ == body && surfaceFocus_ == focus && surfaceJoin_ == join) return;
    surfaceBody_ = body; surfaceFocus_ = focus; surfaceJoin_ = join;
    syncAttachment(); surfaceFrameChanged();
}

LabelPresencePose TabStripController::labelPresence(int index) const
{
    if (index < 0 || index >= count()) return {};
    return presenceFor(index, animatedRect(index));
}

LabelPresencePose TabStripController::presenceFor(int index, QRect area) const
{
    if (index < 0 || index >= count()) return {};
    return presence_.pose(tabs_.at(index).data.toUuid(), area);
}

QVector<TabSurfaceAnchor> TabStripController::surfaceAnchors(QPointF offset) const
{
    QVector<TabSurfaceAnchor> anchors;
    if (surfaceBody_.isEmpty()) return anchors;
    const auto rest = interpolateElevation(elevationProfile(ElevationLevel::AnchorRest),
                                           elevationProfile(ElevationLevel::AnchorRaised), surfaceFocus_);
    if (!incoming_.visual().empty() || (lifted_.active() && (dragged_ == current_ || draggedFolder_ >= 0))) {
        const auto anchor = activeDragRect();
        const bool compact = (dropIndex_ >= 0 ? dropSticky_ : dragged_ < stickyCount_) && compactStickyTiles();
        const auto geometry = anchorGeometry(anchor, anchor.intersected(rect()), compact, true);
        if (!geometry.anchorPath.isEmpty()) anchors.append({translatedSurfaceGeometry(geometry, offset),
            interpolateElevation(!incoming_.visual().empty() ? rest : interpolateElevation(elevationProfile(ElevationLevel::Flat), rest, selectionWeight(current_)),
                elevationProfile(ElevationLevel::Lifted), surfaceMotion_.liftProgress()), true,
                overlayActive() || vertical() && surfaceJoin_ < 1});
        return anchors;
    }
    const int index = current_;
    if (index >= 0 && index < count()) {
        const auto weight = selectionWeight(index);
        if (weight <= 0 || layout_.lanes.value(index, -1) < 0) return anchors;
        const auto area = animatedRect(index);
        const auto presence = presenceFor(index, area);
        if (presence.opacity <= 0) return anchors;
        const bool settling = positions_.isSettling(tabData(index).toUuid());
        const auto visible = settling ? area
            : area.intersected(layout_.laneList.at(layout_.lanes.at(index)).viewport.translated(tabOrigin())).intersected(rect());
        const bool compact = shapeOf(index) == TabShape::Tile;
        const auto geometry = anchorGeometry(area, visible, compact, false, index == current_);
        if (geometry.anchorPath.isEmpty() || compact) return anchors;
        auto profile = interpolateElevation(elevationProfile(ElevationLevel::Flat), rest, weight);
        if (index == current_ && surfaceMotion_.liftProgress() > 0)
            profile = interpolateElevation(profile, elevationProfile(ElevationLevel::Lifted), surfaceMotion_.liftProgress());
        anchors.append({translatedSurfaceGeometry(geometry, offset), profile, surfaceMotion_.liftProgress() > 0 || settling,
            overlayActive() || vertical() && surfaceJoin_ < 1, presence.translated(offset)});
    }
    return anchors;
}

TabSurfaceExtension TabStripController::surfaceExtension(QPointF offset) const
{
    if (surfaceBody_.isEmpty() || current_ < 0 || current_ >= count()) return {};
    const bool moving = !incoming_.visual().empty() || (lifted_.active() && (dragged_ == current_ || draggedFolder_ >= 0));
    const QRect area = moving ? activeDragRect() : animatedRect(current_);
    const bool compact = moving ? dropSticky_ && compactStickyTiles() : shapeOf(current_) == TabShape::Tile;
    const auto geometry = translatedSurfaceGeometry(anchorGeometry(area, area.intersected(rect()), compact, moving), offset);
    if (!geometry.joined) return {};
    const auto presence = moving ? LabelPresencePose{} : presenceFor(current_, area).translated(offset);
    if (presence.opacity <= 0) return {};
    QPainterPath widget; widget.addRect(QRectF(rect()).translated(offset));
    QTransform transform; transform.translate(presence.offset.x(), presence.offset.y()); transform.scale(presence.scale, presence.scale);
    return {transform.map(geometry.anchorPath).subtracted(widget), theme_.surface, presence.opacity};
}

void TabStripController::setIconMode(bool iconMode)
{
    if (!vertical()) iconMode = false;
    if (!columnPeek_.setIconsOnly(iconMode)) return;
    relayout(true);
    if (columnPeek_.iconsOnly()) changes_.reconcileHover = true;
    changes_.reservedSize = true;
}

void TabStripController::setColumnReveal(qreal reveal, bool modeTransition)
{
    if (columnReveal_ == reveal && columnModeTransition_ == modeTransition) return;
    const bool iconOnlyChanged = (columnReveal_ > 0) != (reveal > 0);
    columnReveal_ = reveal; columnModeTransition_ = modeTransition;
    invalidateCarrier(iconOnlyChanged); requestRepaint();
}

void TabStripController::applyColumnPeekAction(const TabColumnPeekAction& action)
{
    using Timer = TabColumnPeekAction::Timer;
    if (action.timer == Timer::Start) peekDeadlineNs_ = timestampNs_ + qint64(TabColumnPeek::delayMs) * 1'000'000;
    else if (action.timer == Timer::Stop) peekDeadlineNs_.reset();
    if (!action.relayout) return;
    relayout(action.animate);
    changes_.reservedSize = true;
}

void TabStripController::setColumnHovered(bool hovered)
{
    applyColumnPeekAction(columnPeek_.hover(hovered, peekDeadlineNs_.has_value(), scrollbars_.dragging()));
}

void TabStripController::restoreColumnPeek(bool peeked)
{
    const auto action = columnPeek_.restore(peeked);
    applyColumnPeekAction(action);
    if (action.relayout) changes_.trackPopup = true;
}

TabVisualState TabStripController::captureVisualState()
{
    TabVisualState result;
    const auto globalOrigin = mapToGlobal(QPoint());
    for (int index = 0; index < count(); ++index) {
        const auto rect = animatedRect(index);
        if (rect.isEmpty()) continue;
        auto parts = partsOf(index, rect);
        for (auto* part : {&parts.tab, &parts.icon, &parts.number, &parts.title, &parts.action}) part->translate(globalOrigin);
        std::shared_ptr<TabRetiredLabel> retired;
        if (isVisible() && !tabRect(index).isEmpty()) {
            retired = std::make_shared<TabRetiredLabel>();
            retired->label = TabPainter::captureLabel(parts, paintState(index), font(), layoutDirection());
            if (retired->label.state.selected) {
                auto geometry = anchorGeometry(rect, rect.intersected(this->rect()), shapeOf(index) == TabShape::Tile, false);
                geometry.body = {}; geometry.bodyPath = {}; geometry.mergedPath = geometry.anchorPath;
                geometry.joined = false;
                retired->label.surface = translatedSurfaceGeometry(geometry, globalOrigin);
            }
            retired->visibility = presence_.capturedVisibility(tabData(index).toUuid());
        }
        result.insert(tabData(index).toUuid(), {rect.translated(globalOrigin), shapeOf(index),
            index < stickyCount_, index == current_, iconsOnly() || shapeOf(index) == TabShape::Tile, parts, std::move(retired), hasSingleTileRow()});
    }
    // Flat exits belong to this strip's single clock. Capture their current
    // width too, so reversing a pin operation does not restart from full size.
    for (auto it = departing_.cbegin(); it != departing_.cend(); ++it) if (it->collapse && it->retired) {
        auto visible = *it;
        visible.global = planarLabelRect(it->global, *it->collapse, 1 - presence_.progress());
        if (!visible.global.isEmpty()) result.insert(it.key(), std::move(visible));
    }
    return result;
}

void TabStripController::animatePresence(const QSet<QUuid>& ids, bool visible, Motion motion)
{
    (void)presence_.begin(ids, visible, motion, animationsEnabled(), timestampNs_);
    if (!animationsEnabled()) { departing_.clear(); if (visible) for (const auto& id : ids) presence_.remove(id); }
    surfaceFrameChanged();
}

bool TabStripController::hasSingleTileRow() const
{
    return compactStickyTiles() && stickyCount_ > 0 && layout_.tileColumns >= stickyCount_;
}

bool TabStripController::usesPlanarReflow(const TabVisualState& previous) const
{
    if (!hasSingleTileRow()) return false;
    int oldTiles = 0;
    bool reversing = false;
    for (const auto& label : previous) if (label.shape == TabShape::Tile) {
        if (!label.singleTileRow) return false;
        ++oldTiles; reversing |= label.collapse.has_value();
    }
    return oldTiles > 0 && (oldTiles != stickyCount_ || reversing);
}

TabVisualState TabStripController::takeSeparateDepartures(TabVisualState& previous) const
{
    TabVisualState separate;
    const bool planar = usesPlanarReflow(previous);
    QHash<QUuid, int> indexes;
    for (int index = 0; index < count(); ++index) indexes.insert(tabData(index).toUuid(), index);
    for (auto label = previous.begin(); label != previous.end(); ++label) {
        const int index = indexes.value(label.key(), -1);
        if (label->retired && (index < 0 || label->sticky != (index < stickyCount_))
            && !(planar && label->shape == TabShape::Tile)) {
            separate.insert(label.key(), *label); label->retired.reset();
        }
    }
    return separate;
}

void TabStripController::transitionFrom(TabVisualState previous, Motion motion)
{
    if (!animationsEnabled() || !isVisible()) return;
    planarReflow_ = usesPlanarReflow(previous);
    if (planarReflow_) motion = Motion::Presence;
    positions_.setMotion(motion);
    QSet<QUuid> appeared;
    QSet<QUuid> planarEntries;
    LabelPositions entrySlots;
    if (planarReflow_) {
        LabelPositions oldRects, newRects;
        QVector<QPair<QUuid, QRect>> oldRow, newRow;
        for (auto label = previous.cbegin(); label != previous.cend(); ++label) if (label->shape == TabShape::Tile) {
            oldRects.insert(label.key(), label->global); oldRow.append({label.key(), label->global});
        }
        std::sort(oldRow.begin(), oldRow.end(), [](const auto& a, const auto& b) { return a.second.left() < b.second.left(); });
        const auto origin = mapToGlobal(QPoint());
        for (int index = 0; index < stickyCount_; ++index) {
            const auto id = tabData(index).toUuid();
            const auto area = layout_.tabs.at(index).translated(tabOrigin() + origin);
            newRects.insert(id, area); newRow.append({id, area});
        }
        entrySlots = collapsedSlots(newRow, oldRects);
        const auto exitSlots = collapsedSlots(oldRow, newRects);
        for (auto label = previous.begin(); label != previous.end(); ++label)
            if (const auto end = exitSlots.constFind(label.key()); end != exitSlots.cend()) label->collapse = *end;
    }
    QSet<QUuid> currentIds;
    positions_.clear(); departing_.clear();
    for (int index = 0; index < count(); ++index) {
        const auto id = tabData(index).toUuid();
        const auto old = previous.constFind(id);
        if (layout_.visibleRect(index).isEmpty()) {
            if (old != previous.cend() && old->retired) departing_.insert(id, *old);
            continue;
        }
        currentIds.insert(id);
        if (old == previous.cend()) {
            appeared.insert(id);
            if (entrySlots.contains(id)) {
                const auto slot = entrySlots.value(id);
                positions_.slide(id, QRect(mapFromGlobal(slot.topLeft()), slot.size()), true); planarEntries.insert(id);
            }
            continue;
        }
        const QRect from(mapFromGlobal(old->global.topLeft()), old->global.size());
        if (old->sticky != (index < stickyCount_)) {
            departing_.insert(id, *old); appeared.insert(id);
            if (entrySlots.contains(id)) {
                const auto slot = entrySlots.value(id);
                positions_.slide(id, QRect(mapFromGlobal(slot.topLeft()), slot.size()), true); planarEntries.insert(id);
            }
        } else if (!from.isEmpty() && from != layout_.tabs.at(index).translated(tabOrigin()))
            positions_.slide(id, from, old->shape == TabShape::Tile && shapeOf(index) == TabShape::Tile);
    }
    for (auto it = previous.cbegin(); it != previous.cend(); ++it)
        if (it->retired && !currentIds.contains(it.key())) departing_.insert(it.key(), *it);
    if (!positions_.isEmpty()) { positions_.start(timestampNs_); }
    if (planarReflow_ || !appeared.isEmpty() || !departing_.isEmpty()) animatePresence(appeared, true, motion);
    presence_.setPlanar(planarEntries);
    invalidatePresentation();
}

LabelPositions TabStripController::captureDropPositions(std::optional<LabelPositions> carrierPositions) const
{
    LabelPositions positions;
    const auto origin = mapToGlobal(QPoint());
    if (!incoming_.visual().empty()) {
        if (carrierPositions) positions = std::move(*carrierPositions);
        else if (!incoming_.visual().identity.isNull()) positions.insert(incoming_.visual().identity, incoming_.visual().active);
        const auto offset = origin + incomingRect().topLeft();
        for (auto& area : positions) area.translate(offset);
    } else if (lifted_.active()) {
        const auto lifted = liftedRect();
        if (lifted.isEmpty()) return positions;
        if (draggedFolder_ >= 0 && draggedFolder_ < folders_.size()) {
            const auto& folder = folders_.at(draggedFolder_);
            if (folder.collapsed) return positions;
            int x = lifted.left() + folderHeaderExtent(folder);
            int y = lifted.top() + metrics_.rowHeight + metrics_.rowGap;
            for (int index = folder.first; index < folder.first + folder.count && index < count(); ++index) {
                const QRect area = vertical() ? QRect(lifted.left() + metrics_.folderIndent, y,
                    lifted.width() - metrics_.folderIndent, metrics_.rowHeight)
                    : QRect(x, lifted.top(), tabExtent(index, false), metrics_.rowHeight);
                positions.insert(tabData(index).toUuid(), area.translated(origin));
                x = area.right() + 1; y += metrics_.rowHeight + metrics_.rowGap;
            }
        } else if (dragged_ >= 0 && dragged_ < count()) positions.insert(tabData(dragged_).toUuid(), lifted.translated(origin));
    }
    return positions;
}

void TabStripController::settleFrom(const LabelPositions& globalPositions)
{
    if (globalPositions.isEmpty() || !animationsEnabled()) return;
    // Retarget any neighbor still sliding from where it is now, without
    // changing layout, scroll position, or hit testing for the drop.
    for (int index = 0; index < count(); ++index) {
        const auto id = tabData(index).toUuid();
        const auto from = globalPositions.constFind(id);
        if (from != globalPositions.cend()) {
            const auto target = layout_.tabs.at(index).translated(tabOrigin());
            const QRect local(mapFromGlobal(from->topLeft()), from->size());
            if (!target.isEmpty() && !local.isEmpty() && local != target) {
                positions_.settle(id, index, local); presence_.remove(id);
            }
        } else if (positions_.contains(id)) positions_.setFrom(id, animatedRect(index));
    }
    invalidatePresentation();
    if (positions_.settling().isEmpty()) return;
    planarReflow_ = false;
    positions_.setMotion(Motion::SurfaceLower);
    positions_.start(timestampNs_);
    surfaceFrameChanged();
}

void TabStripController::updateRowPresentation(TabVisualState previous, bool moving, bool revealActive)
{
    const bool open = rowsState_.open();
    (void)rowsState_.begin(moving, timestampNs_);
    relayout(false);
    if (open && revealActive) ensureVisible(current_);
    changes_.reservedSize = true;
    if (moving) transitionFrom(std::move(previous), Motion::Reveal);
}

void TabStripController::setExpanded(bool expanded, bool byWheel, bool animate)
{
    if (!collapsible()) expanded = false;
    collapseDeadlineNs_.reset();
    if (rowsState_.expanded() == expanded) {
        rowsState_.setExpanded(expanded, byWheel);
        if (!animate) {

            const auto before = rowsState_.value();
            rowsState_.jumpToTarget();
            if (rowsState_.value() != before) invalidatePresentation();
        }
        return;
    }
    const bool moving = animate && isVisible() && animationsEnabled();
    const bool activeWasVisible = fullyVisible(current_);
    auto before = moving ? captureVisualState() : TabVisualState{};
    rowsState_.setExpanded(expanded, byWheel);
    wheelController_.reset();
    updateRowPresentation(std::move(before), moving, activeWasVisible);
}

void TabStripController::setDragExpanded(bool expanded)
{
    if (!collapsible()) expanded = false;
    if (rowsState_.dragExpanded() == expanded) return;
    const bool moving = isVisible() && animationsEnabled();
    const bool activeWasVisible = fullyVisible(current_);
    auto before = moving ? captureVisualState() : TabVisualState{};
    rowsState_.setDragExpanded(expanded, moving);
    updateRowPresentation(std::move(before), moving, activeWasVisible);
}

void TabStripController::setDraggedTab(TabId tab)
{
    const int index = tab.isNull() ? -1 : indexOf(tab);
    if (index == dragged_) return;
    initialStickyBoundary_ = index >= 0 ? stickyBoundary() : std::nullopt;
    dragged_ = index;
    if (index < 0) {
        lifted_.clear();
        const auto timestamp = timestampNs_;
        (void)surfaceMotion_.setLifted(false, animationsEnabled(), timestamp);
    }
    relayout(true);
}

void TabStripController::setDraggedFolder(QUuid folder)
{
    int index = -1;
    for (int at = 0; at < folders_.size(); ++at) if (folders_.at(at).id == folder) index = at;
    if (index == draggedFolder_) return;
    draggedFolder_ = index;
    if (index < 0) lifted_.clear();
    relayout(true);
}

void TabStripController::setLifted(QPoint local, QPoint grab)
{
    setLiftedWithAnchor(local, {grab, {}});
}

void TabStripController::setLiftedWithAnchor(QPoint local, const DragAnchor& grab)
{
    const auto change = lifted_.update(local.isNull() ? std::nullopt : std::optional(local), grab);
    if (change == TabLiftState::Change::None) return;
    if (change == TabLiftState::Change::Lift || change == TabLiftState::Change::Lower) {
        const bool lifting = change == TabLiftState::Change::Lift;
        const auto timestamp = timestampNs_;
        (void)surfaceMotion_.setLifted(lifting, animationsEnabled(), timestamp);
    }
    syncAttachment(); surfaceFrameChanged();
}

void TabStripController::setIncomingVisual(TabDragVisual visual, QPoint local)
{
    const auto change = incoming_.replace(std::move(visual), local, layout_, dragArea(), incomingConstraint());
    if (change == TabIncomingState::Change::None) return;
    if (change == TabIncomingState::Change::Leave) changes_.incomingLeft = true;
    if (change == TabIncomingState::Change::Enter || change == TabIncomingState::Change::Leave) {
        const bool entering = change == TabIncomingState::Change::Enter;
        const auto timestamp = timestampNs_;
        (void)surfaceMotion_.setIncoming(entering, animationsEnabled(), timestamp);
    }
    syncAttachment(); surfaceFrameChanged();
}

void TabStripController::setDropPreview(int index, bool sticky, QVector<int> block, int folder)
{
    if (index < 0) { block.clear(); sticky = false; index = -1; folder = -1; incoming_.resetReorderOffset(); }
    if (dropIndex_ == index && dropSticky_ == sticky && dropBlock_ == block && dropFolder_ == folder) return;
    dropIndex_ = index; dropSticky_ = sticky; dropBlock_ = std::move(block); dropFolder_ = folder;
    relayout(true);
    changes_.reservedSize = true;
}

qreal TabStripController::liftProgress() const { return surfaceMotion_.liftProgress(); }

QRegion TabStripController::foregroundRegion() const
{
    const bool shown = !positions_.settling().isEmpty() || (lifted_.active() && (dragged_ >= 0 || draggedFolder_ >= 0))
        || !incoming_.visual().empty() || (current_ >= 0 && (surfaceMotion_.liftProgress() > 0
            || overlayActive() || vertical() && surfaceJoin_ < 1));
    if (!shown) return {};
    const auto labelMargins = elevationMargins(elevationProfile(ElevationLevel::Lifted));
    const auto bodyMargins = elevationMargins(elevationProfile(ElevationLevel::BodyRaised));
    const QMarginsF margins(std::max(labelMargins.left(), bodyMargins.left()), std::max(labelMargins.top(), bodyMargins.top()),
        std::max(labelMargins.right(), bodyMargins.right()), std::max(labelMargins.bottom(), bodyMargins.bottom()));
    QRegion region;
    const auto include = [&](QRect area) {
        if (!area.isEmpty()) region |= QRectF(area).marginsAdded(margins).toAlignedRect().adjusted(-1, -1, 1, 1);
    };
    include(liftedRect()); include(incomingRect());
    for (auto it = positions_.settling().cbegin(); it != positions_.settling().cend(); ++it)
        if (it.value() >= 0 && it.value() < count()) include(animatedRect(it.value()));
    for (const auto& anchor : surfaceAnchors()) if (anchor.lifted || anchor.foreground)
        region |= anchor.presence.mapRect(anchor.geometry.anchorPath.boundingRect().marginsAdded(margins))
            .toAlignedRect().adjusted(-1, -1, 1, 1);
    return region & environment_.windowArea;
}

} // namespace window_system
