// SPDX-License-Identifier: MIT
#include "tab_strip_controller.hpp"
#include "style/tab_appearance_p.hpp"
#include <window_system/interface_appearance.hpp>
#include <algorithm>

namespace window_system {

namespace {
TabShape horizontalShape(TabBarPlacement placement) { return placement == TabBarPlacement::Bottom ? TabShape::Bottom : TabShape::Top; }
} // namespace

bool TabStripController::isCompactSticky(int index) const
{
    return index >= 0 && index < stickyCount_ && appearance_.stickySizing == StickyTabSizing::Compact;
}

bool TabStripController::rowShape() const { return vertical(); }

TabLabelParts TabStripController::labelParts(int index, bool sticky) const
{
    const auto& tab = tabs_.at(index);
    TabLabelParts parts;
    parts.number = labelContent(index)->numberExtent;
    parts.icon = appearance_.showIcons && !tab.icon.isNull() ? metrics_.iconSize : 0;
    const int natural = std::max(0, tab.titleWidth);
    if (vertical()) parts.title = natural;
    else if (sticky && appearance_.stickySizing == StickyTabSizing::Shrink) parts.title = std::min(natural, uiPixels(appearance_.shrinkStickyTitleWidth));
    else parts.title = appearance_.clipTitles ? std::min(natural, uiPixels(appearance_.maximumTitleWidth)) : natural;
    return parts;
}

int TabStripController::tabExtent(int index, bool sticky) const
{
    if (index < 0 || index >= tabs_.size()) return 0;
    if (sticky && appearance_.stickySizing == StickyTabSizing::Compact) return uiPixels(compactStickyTabWidth);
    // A sticky tab's action always shows and keeps its room.
    return tabBarTabWidth(labelParts(index, sticky), sticky);
}

int TabStripController::carriedExtent(const CarriedLabel& label, bool sticky) const
{
    if (label.folderHeader) return folderHeaderExtent({{}, label.title, {}, label.collapsed, 0, label.count});
    if (sticky && appearance_.stickySizing == StickyTabSizing::Compact) return uiPixels(compactStickyTabWidth);
    TabTitleLayout title;
    title.prepare(label.title, font(), layoutDirection(), true);
    TabLabelParts parts;
    parts.icon = appearance_.showIcons && label.icon ? metrics_.iconSize : 0;
    const int natural = title.naturalWidth();
    if (sticky && appearance_.stickySizing == StickyTabSizing::Shrink) parts.title = std::min(natural, uiPixels(appearance_.shrinkStickyTitleWidth));
    else parts.title = appearance_.clipTitles ? std::min(natural, uiPixels(appearance_.maximumTitleWidth)) : natural;
    // The number it gets is unknown until it lands; one digit approximates it.
    parts.number = appearance_.showNumbers ? fontMetrics().horizontalAdvance(QStringLiteral("0")) : 0;
    return tabBarTabWidth(parts, sticky);
}

int TabStripController::naturalTabWidth(int index) const { return index >= 0 && index < tabs_.size() ? tabBarTabWidth(labelParts(index, false)) : 0; }

void TabStripController::updateTitleWidths()
{
    labelStyle_.reset();
    tabs_.measureTitles(font(), layoutDirection(), nullptr, true);
}

bool TabStripController::wrappedRows() const
{
    return !vertical() && rowPolicy().wrapped();
}

bool TabStripController::collapsible() const
{
    return !vertical() && rowPolicy().collapsible();
}

bool TabStripController::collapsedNow() const { return !vertical() && rowPolicy().collapsed(); }

bool TabStripController::overlayActive() const
{
    if (vertical()) return columnPeek_.iconsOnly() && columnPeek_.expanded();
    return rowPolicy().overlay();
}

TabStripRows TabStripController::rowPolicy() const
{
    return {appearance_.multiline, appearance_.showAllRows, appearance_.stickyMultiline,
        appearance_.stickyShowAllRows, appearance_.stickyTabsOnSeparateRow, stickyCount_ > 0,
        rowsState_.expanded(), rowsState_.dragExpanded(), rowsState_.temporaryOverlay(), appearance_.rowExpansion, rowsState_.value()};
}

TabStripGeometry TabStripController::stripGeometry() const
{
    return {placement_, rowPolicy(), metrics_.outerMargin, metrics_.shadowBlur, metrics_.rowHeight,
        columnPeek_.iconsOnly(), metrics_.verticalIconWidth, minimumColumnWidth(), maximumVerticalTabWidth};
}

TabStripControls TabStripController::stripControls() const
{
    auto result = environment_.controls;
    result.countMinimumWidth = metrics_.countMinimumWidth;
    result.countPadding = uiPixels(14);
    result.ownerVisible = isVisible();
    return result;
}

TabStripArrangementInput TabStripController::arrangementInput() const
{
    TabStripArrangementInput source;
    source.geometry = stripGeometry(); source.group = available_; source.controls = stripControls(); source.font = font();
    auto& input = source.labels;
    input.rowGap = vertical() ? metrics_.rowGap : 0;
    input.inset = metrics_.verticalInset; input.tile = metrics_.tile; input.minimumTileWidth = metrics_.tile.width();
    input.tileColumns = carriedTileColumns_; input.stickyCount = stickyCount_;
    input.extents.reserve(tabs_.size());
    for (int index = 0; index < tabs_.size(); ++index)
        input.extents.append(vertical() ? 0 : tabExtent(index, index < stickyCount_));
    input.stickyTiles = compactStickyTiles();
    input.freezeSticky = !vertical() && !appearance_.multiline && !appearance_.stickyTabsOnSeparateRow && appearance_.stickySizing != StickyTabSizing::Normal;
    input.stickyOffset = stickyOffset_; input.regularOffset = regularOffset_;
    if (!vertical()) {
        input.rowsOffset = rowsOffset_;
        input.collapsedStickyRow = collapsedStickyRow_; input.collapsedRegularRow = collapsedRegularRow_;
    }
    input.current = current_; input.removed = dragged_;
    input.insertion = dropIndex_; input.insertionSticky = dropSticky_; input.block = dropBlock_;
    input.folderIndent = metrics_.folderIndent;
    for (const auto& folder : folders_)
        input.folders.append({folder.first, folder.count, folderHeaderExtent(folder), folder.collapsed});
    input.removedFolder = draggedFolder_; input.insertionFolder = dropFolder_;
    return source;
}

QPoint TabStripController::tabOrigin() const
{
    // Keep this hot query independent of label measurement and control hints.
    return tabStripOrigin(placement_, metrics_.outerMargin, vertical() ? 0 : rowShadowExtent());
}

int TabStripController::minimumColumnWidth() const
{
    // Keep the icon clear of the action even when no title fits. Ordinals
    // and titles can disappear before either of these hit targets does.
    return std::max(uiPixels(minimumVerticalTabWidth), 2 * metrics_.verticalInset + uiPixels(tabBarLeading)
        + metrics_.iconSize + uiPixels(tabActionGap) + uiPixels(tabActionSize) + uiPixels(tabBarTrailing));
}

int TabStripController::rowShadowExtent() const
{
    return overlayActive() ? qRound(metrics_.shadowBlur * rowsState_.value()) : 0;
}

const TabStripRowMeasure& TabStripController::measureRows(QSize available) const
{
    if (!rowMeasure_ || rowMeasure_->available != available) rowMeasure_ = measureTabStripRows(arrangementInput(), available);
    return *rowMeasure_;
}

QSize TabStripController::reservedSize(QSize available) const
{
    const auto geometry = stripGeometry();
    return tabStripReservedSize(geometry, available, vertical() ? TabStripRowMeasure{} : measureRows(available));
}

QSize TabStripController::displaySize(QSize available) const
{
    const auto geometry = stripGeometry();
    if (vertical()) return tabStripReservedSize(geometry, available);
    std::optional<TabLayoutInput> preview;
    if (!geometry.rows.overlay() && dropIndex_ >= 0)
        preview = tabStripLayoutInput(arrangementInput(), QSize(available.width(), 0), collapsedNow());
    return tabStripDisplaySize(geometry, available, measureRows(available), preview ? &*preview : nullptr);
}

void TabStripController::setAvailableSize(QSize available)
{
    if (available_ == available) return;
    available_ = available;
    relayout(false);
}

void TabStripController::relayout(bool animate, bool measureChanged)
{
    invalidateCarrier(true);
    if (measureChanged) { scrollMotion_.cancel(); smoothLane_ = -1; }
    if (measureChanged) rowMeasure_.reset();
    // Where each tab is drawn now, to slide from.
    QHash<QUuid, QRect> before;
    QSet<QUuid> resizing;
    const int previousTileColumns = layout_.tileColumns;
    const bool sliding = animate && animationsEnabled() && isVisible();
    if (sliding) for (int index = 0; index < tabs_.size() && index < layout_.tabs.size(); ++index)
        if (!layout_.tabs.at(index).isNull()) {
            const auto id = tabs_.at(index).data.toUuid();
            before.insert(id, animatedRect(index));
            if (positions_.resizesWidth(id)) resizing.insert(id);
        }
    tabs_.measureTitles(font(), layoutDirection(), nullptr);
    auto arranged = arrangeTabStrip(arrangementInput(), size());
    arrangedInput_ = std::move(arranged.input); layout_ = std::move(arranged.layout);
    environment_.controls.countShown = arranged.controls.countShown;
    environment_.controls.countSize = arranged.controls.countSize;
    controls_.countShown = arranged.controls.countShown;
    controls_.countSize = arranged.controls.countSize;
    controls_.countMeasured = arranged.countMeasured;
    if (arranged.countMeasured) controls_.countText = std::move(arranged.countText);
    changes_.controls = true;
    stickyOffset_ = arranged.stickyOffset; regularOffset_ = arranged.regularOffset; rowsOffset_ = arranged.rowsOffset;
    collapsedStickyRow_ = arranged.collapsedStickyRow; collapsedRegularRow_ = arranged.collapsedRegularRow;
    applyControlRects(arranged.controlRects);
    carrierBounds_.reset();

    if (sliding) {
        // Restart from wherever each tab is drawn now.
        positions_.clear(); planarReflow_ = false;
        positions_.setMotion(Motion::Relocate);
        for (int index = 0; index < tabs_.size(); ++index) {
            const auto id = tabs_.at(index).data.toUuid();
            const auto from = before.value(id);
            const auto to = layout_.tabs.at(index).translated(tabOrigin());
            if (!from.isNull() && !layout_.tabs.at(index).isNull() && from != to)
                positions_.slide(id, from, shapeOf(index) == TabShape::Tile
                    && (previousTileColumns != layout_.tileColumns || resizing.contains(id)));
        }
        if (!positions_.isEmpty()) { positions_.start(timestampNs_); }
    }
    syncAttachment();
    surfaceFrameChanged();
    changes_.view = true;

}

void TabStripController::relayoutTrailing()
{
    if (vertical()) { environment_.controls.countShown = false; controls_.countShown = false; }
    applyControlRects(tabStripControlRects(stripGeometry(), layout_, stripControls()));
}

void TabStripController::applyControlRects(const TabStripControlRects& controls)
{
    controls_.rects = controls;
    changes_.controls = true;
}

QRect TabStripController::tabRect(int index) const
{
    if (index < 0 || index >= layout_.tabs.size()) return {};
    const auto visible = layout_.visibleRect(index).intersected(rect().translated(-tabOrigin()));
    return visible.isEmpty() ? QRect{} : visible.translated(tabOrigin());
}

QRegion TabStripController::tabRegion(int index) const
{
    if (index < 0 || index >= layout_.tabs.size() || layout_.lanes.at(index) < 0) return {};
    const auto contour = painter_.outline(QRectF(layout_.tabs.at(index).translated(tabOrigin())), shapeOf(index), carriedDisplay_ || index == current_, carriedDisplay_);
    return QRegion(contour.toFillPolygon().toPolygon()).intersected(tabRect(index));
}

int TabStripController::tabAt(QPoint local) const { return layout_.tabAt(local - tabOrigin()); }

TabShape TabStripController::shapeOf(int index) const
{
    if (!vertical()) return horizontalShape(placement_);
    const bool tiles = compactStickyTiles();
    return index < stickyCount_ && tiles ? TabShape::Tile : TabShape::Row;
}

bool TabStripController::hasLabelAction(int index) const
{
    if (index < 0 || index >= tabs_.size()) return false;
    if (!vertical()) return !isCompactSticky(index);
    // Do not expose a destructive action until the expanded column has
    // actually finished opening. Painting, hit testing and tips share this.
    if (iconsOnly() || columnReveal_ < 1 || shapeOf(index) == TabShape::Tile) return false;
    return index >= stickyCount_ || appearance_.stickySizing != StickyTabSizing::Compact;
}

TabParts TabStripController::partsOf(int index, const QRect& rect) const
{
    const bool sticky = index < stickyCount_;
    const bool iconOnly = (vertical() && iconsOnly() && columnReveal_ <= 0) || (!vertical() && isCompactSticky(index));
    const bool action = hasLabelAction(index);
    return labelGeometry(rect, shapeOf(index), labelParts(index, sticky), iconOnly, action, sticky);
}

TabParts TabStripController::labelGeometry(const QRect& rect, TabShape shape, TabLabelParts label,
                              bool iconOnly, bool action, bool sticky) const
{
    if (vertical() && shape == TabShape::Row)
        return layoutColumnTabParts(rect, label, iconOnly, action, sticky, placement_ == TabBarPlacement::Right,
            {width(), metrics_.verticalIconWidth, metrics_.iconSize, metrics_.rowHeight});
    return layoutTabParts(rect, shape, label, iconOnly, action, sticky, layoutDirection());
}

QRect TabStripController::titleRect(int index) const
{
    const auto rect = tabRect(index);
    return rect.isEmpty() ? QRect{} : partsOf(index, layout_.tabs.at(index).translated(tabOrigin())).title.intersected(rect);
}

QRect TabStripController::actionRect(int index) const
{
    const auto rect = tabRect(index);
    return rect.isEmpty() ? QRect{} : partsOf(index, layout_.tabs.at(index).translated(tabOrigin())).action.intersected(rect);
}

QRect TabStripController::animatedRect(int index) const
{
    const auto target = layout_.tabs.at(index).translated(tabOrigin());
    if (positions_.isEmpty()) return target;
    const bool rearrangingTiles = columnModeTransition_ && stickyCount_ > 1 && index < stickyCount_
        && appearance_.stickySizing == StickyTabSizing::Compact;
    return positions_.rect(tabs_.at(index).data.toUuid(), target,
        {vertical() && columnManaged_, columnModeTransition_, rearrangingTiles},
        planarReflow_ ? std::optional(presence_.progress()) : std::nullopt);
}

int TabStripController::scrollOffset() const
{
    return regularOffset_;
}

int TabStripController::maximumScrollOffset() const
{
    for (const auto& lane : layout_.laneList) if (!lane.sticky) return lane.maximumOffset();
    return 0;
}

QRect TabStripController::scrollViewportAt(QPoint local) const
{
    const int index = laneAt(local);
    return index < 0 ? QRect{} : layout_.laneList.at(index).viewport.translated(tabOrigin());
}

bool TabStripController::scrollsHorizontallyAt(QPoint local) const
{
    const int index = laneAt(local);
    return index >= 0 && !layout_.laneList.at(index).vertical;
}

QRect TabStripController::scrollViewport() const
{
    for (const auto& lane : layout_.laneList) if (!lane.sticky) return lane.viewport.translated(tabOrigin());
    return {};
}

int TabStripController::frozenExtent() const
{
    for (const auto& lane : layout_.laneList) if (!lane.sticky) return lane.frozen;
    return 0;
}

QRect TabStripController::contentTabRect(int index) const
{
    if (index < 0 || index >= layout_.tabs.size() || layout_.lanes.at(index) < 0) return {};
    const auto& lane = layout_.laneList.at(layout_.lanes.at(index));
    auto rect = layout_.tabs.at(index).translated(tabOrigin());
    // Frozen sticky tabs do not scroll; the others are shifted back.
    if (lane.frozen && index < stickyCount_) return rect.translated(lane.offset, 0);
    return lane.vertical ? rect.translated(0, lane.offset) : rect.translated(lane.offset, 0);
}

bool TabStripController::scrollsHorizontally() const { return !vertical() && !wrappedRows(); }

bool TabStripController::isOverflowing() const
{
    for (const auto& lane : layout_.laneList) if (lane.maximumOffset() > 0) return true;
    return false;
}

int TabStripController::laneAt(QPoint local) const
{
    return TabWheelController::laneAt(layout_, local - tabOrigin(), vertical());
}

TabScrollbarMetrics TabStripController::scrollbarMetrics() const
{
    return {tabOrigin(), metrics_.scrollbar, placement_ == TabBarPlacement::Right};
}

QRect TabStripController::scrollbarHitRect() const
{
    return scrollbars_.hitRect(layout_, scrollbarMetrics(), rect(), isVisible());
}

TabDragArea TabStripController::dragArea() const
{
    return {size(), tabOrigin(), metrics_.rowHeight, metrics_.verticalInset};
}

QRect TabStripController::incomingRect() const
{
    return incoming_.rect(layout_, dragArea(), incomingConstraint());
}

QRect TabStripController::incomingRectAt(QPoint local) const
{
    return incoming_.rectAt(local, layout_, dragArea(), incomingConstraint());
}

TabDragConstraint TabStripController::incomingConstraint() const
{
    return vertical() && dropSticky_ && compactStickyTiles() ? TabDragConstraint::CompactTile
        : vertical() ? TabDragConstraint::VerticalRow : TabDragConstraint::HorizontalRow;
}

QRect TabStripController::liftedRect() const
{
    const auto target = liftTarget();
    return target ? lifted_.rect(layout_, dragArea(), *target) : QRect{};
}

QRect TabStripController::liftedRectAt(QPoint local) const
{
    const auto target = liftTarget();
    return target ? lifted_.rectAt(local, layout_, dragArea(), *target) : QRect{};
}

std::optional<TabLiftTarget> TabStripController::liftTarget() const
{
    if (!lifted_.active()) return {};
    if (draggedFolder_ >= 0 && draggedFolder_ < folders_.size()) {
        const auto& folder = folders_.at(draggedFolder_);
        const int tabs = folder.collapsed ? 0 : folder.count;
        if (vertical()) {
            const int rows = 1 + tabs;
            const int height = rows * metrics_.rowHeight + (rows - 1) * metrics_.rowGap;
            const int width = std::max(0, this->width() - 2 * metrics_.verticalInset);
            return TabLiftTarget{{width, height}, {width, metrics_.rowHeight}, TabDragConstraint::VerticalRow};
        }
        const int header = folderHeaderExtent(folder);
        int extent = header;
        for (int index = folder.first; index < folder.first + tabs && index < tabs_.size(); ++index) extent += tabExtent(index, false);
        return TabLiftTarget{{extent, metrics_.rowHeight}, {header, metrics_.rowHeight}, TabDragConstraint::HorizontalRow};
    }
    if (dragged_ < 0 || dragged_ >= tabs_.size()) return {};
    const bool sticky = dropIndex_ >= 0 ? dropSticky_ : dragged_ < stickyCount_;
    if (vertical()) {
        if (sticky && compactStickyTiles()) {
            // A gap supplies target dimensions, never a replacement pointer.
            const auto size = !layout_.gap.isEmpty() ? layout_.gap.first().size() : layout_.tabs.value(dragged_).size();
            return TabLiftTarget{size, size, TabDragConstraint::CompactTile, true};
        }
        const QSize size(std::max(0, width() - 2 * metrics_.verticalInset), metrics_.rowHeight);
        return TabLiftTarget{size, size, TabDragConstraint::VerticalRow};
    }
    const bool compact = sticky && appearance_.stickySizing == StickyTabSizing::Compact;
    const QSize size(compact ? uiPixels(compactStickyTabWidth) : tabExtent(dragged_, sticky), metrics_.rowHeight);
    return TabLiftTarget{size, size, TabDragConstraint::HorizontalRow, compact};
}

std::optional<int> TabStripController::stickyBoundary() const
{
    if (stickyCount_ <= 0) return {};
    if (vertical() && !layout_.separator.isNull()) return layout_.separator.top() + tabOrigin().y();
    if (!vertical() && appearance_.stickyTabsOnSeparateRow)
        return layout_.sectionBoundary.value_or(metrics_.rowHeight) + tabOrigin().y();
    return {};
}

TabInsertion TabStripController::insertionAt(QPoint local, int stickyReach, TabLayoutInput::Carried carried) const
{
    auto input = arrangedInput_;
    input.carried = carried;
    const auto boundary = dragged_ >= 0 && dropIndex_ < 0 ? initialStickyBoundary_ : stickyBoundary();
    const std::optional<bool> section = boundary ? std::optional<bool>(local.y() < *boundary) : std::nullopt;
    const auto held = !incoming_.visual().empty() ? incomingRectAt(local) : lifted_.active() ? liftedRectAt(local) : QRect{};
    if (!held.isEmpty() && draggedFolder_ >= 0) {
        // A local folder keeps its existing header-based insertion probe;
        // only individual tabs adopt the edge-over-center reorder rule.
        if (vertical()) local.setY(held.top() + metrics_.rowHeight / 2);
        else local.setX(held.left() + metrics_.rowHeight / 2);
    }
    if (dropIndex_ >= 0 && !held.isEmpty() && !layout_.gap.isEmpty())
        return tabReorderAt(input, layout_, held.translated(-tabOrigin()), local - tabOrigin(), stickyReach, section,
            !incoming_.visual().empty() ? incoming_.reorderOffset() : std::nullopt);
    // A bottom strip grows upward for a temporary gap, while the stable
    // no-gap layout used to pick the slot stays next to the content.
    if (placement_ == TabBarPlacement::Bottom && !overlayActive())
        local.ry() -= std::max(0, height() - reservedSize(available_).height());
    return tabInsertionAt(input, local - tabOrigin(), stickyReach, section);
}

QRect TabStripController::dropPreviewRect() const
{
    QRect result;
    for (const auto& cell : layout_.gap) if (!cell.isNull()) result |= cell.translated(tabOrigin());
    if (layout_.gapLane >= 0) result &= layout_.laneList.at(layout_.gapLane).viewport.translated(tabOrigin());
    return result;
}

QRect TabStripController::scrollThumb(int laneIndex) const
{
    return tabScrollbarGeometry(layout_.laneList.at(laneIndex), scrollbarMetrics()).thumb;
}

} // namespace window_system
