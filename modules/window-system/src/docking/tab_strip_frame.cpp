// SPDX-License-Identifier: MIT
#include "tab_bar.hpp"
#include <algorithm>

namespace window_system {

std::optional<TabStripLabelSample> TabBar::labelSample(int index, QRect area, const QFont& font,
    Qt::LayoutDirection direction, bool settling)
{
    if (area.isEmpty() || (!settling && settling_.contains(tabData(index).toUuid()))) return {};
    auto state = paintState(index);
    const int lane = layout_.lanes.value(index, -1);
    const auto visible = settling ? area : lane < 0 ? QRect{}
        : area.intersected(layout_.laneList.at(lane).viewport.translated(tabOrigin())).intersected(rect());
    std::optional<SurfaceGeometry> geometry;
    if (state.selected && !visible.isEmpty()) geometry = anchorGeometry(area, visible, state.shape == TabShape::Tile, false);
    state.surface = geometry ? &*geometry : nullptr;
    state.elevationManaged = !surfaceBody_.isEmpty();
    auto label = TabPainter::captureLabel(partsOf(index, area), state, font, direction);
    label.presence = presenceFor(index, area);
    return TabStripLabelSample{tabData(index).toUuid(), index, labelContent(index), std::move(label)};
}

void TabBar::captureLaneFrame(TabStripLaneFrame& frame, int laneIndex, const QFont& font, Qt::LayoutDirection direction)
{
    const auto& lane = layout_.laneList.at(laneIndex);
    const auto origin = tabOrigin();
    frame.viewport = lane.viewport.translated(origin);
    frame.regularClip = frame.viewport.adjusted(lane.frozen, 0, 0, 0);
    frame.fades.clear(); frame.regular.clear(); frame.frozen.clear();
    const auto appendFades = [&](const TabLayoutLane& segment) {
        const auto viewport = segment.viewport.translated(origin);
        const int fade = segment.fadeExtent(metrics_.edgeFade);
        if (segment.offset > 0) frame.fades.append({segment.vertical
            ? QRect(viewport.left(), viewport.top(), viewport.width(), fade)
            : QRect(viewport.left() + segment.frozen, viewport.top(), fade, viewport.height()),
            segment.vertical ? Qt::TopEdge : Qt::LeftEdge});
        if (segment.offset < segment.maximumOffset()) frame.fades.append({segment.vertical
            ? QRect(viewport.left(), viewport.bottom() - fade + 1, viewport.width(), fade)
            : QRect(viewport.right() - fade + 1, viewport.top(), fade, viewport.height()),
            segment.vertical ? Qt::BottomEdge : Qt::RightEdge});
    };
    appendFades(lane);
    if (layout_.rowScrollLane >= 0 && layout_.rowScrollLane != laneIndex)
        appendFades(layout_.laneList.at(layout_.rowScrollLane));
    if (!frame.fades.isEmpty() && layout_.lanes.value(current_, -1) == laneIndex
        && incoming_.visual().empty() && !liftedPointer_ && liftElevation_.value() <= 0) {
        const auto area = animatedRect(current_);
        const auto visible = area.intersected(frame.viewport).intersected(rect());
        const auto geometry = anchorGeometry(area, visible, shapeOf(current_) == TabShape::Tile, false);
        if (geometry.joined) {
            const auto joint = vertical() ? (placement_ == TabBarPlacement::Left ? Qt::RightEdge : Qt::LeftEdge)
                : placement_ == TabBarPlacement::Top ? Qt::BottomEdge : Qt::TopEdge;
            auto surface = QRectF(area);
            if (joint == Qt::RightEdge) surface.setRight(width());
            else if (joint == Qt::LeftEdge) surface.setLeft(0);
            else if (joint == Qt::BottomEdge) surface.setBottom(height());
            else surface.setTop(0);
            for (auto& fade : frame.fades) if (fade.area.intersects(visible)) fade.convergeTo(joint, surface);
        }
    }
    const auto captureRange = [&](int first, int last, QVector<TabStripLabelSample>& labels, QRect clip) {
        const auto append = [&](int index) {
            if (layout_.lanes.at(index) != laneIndex) return;
            const auto area = animatedRect(index);
            if (!clip.intersects(area.adjusted(-metrics_.controlRadius, 0, metrics_.controlRadius, 0))) return;
            if (auto label = labelSample(index, area, font, direction)) labels.append(std::move(*label));
        };
        for (int index = first; index < last; ++index) if (index != current_) append(index);
        if (current_ >= first && current_ < last) append(current_);
    };
    if (lane.frozen) {
        captureRange(stickyCount_, count(), frame.regular, frame.regularClip);
        captureRange(0, stickyCount_, frame.frozen, frame.viewport);
    } else captureRange(0, count(), frame.regular, frame.viewport);
    captureFolderFrame(frame, laneIndex);
}

void TabBar::captureFolderFrame(TabStripLaneFrame& frame, int laneIndex)
{
    const auto origin = tabOrigin();
    const bool bottom = placement_ == TabBarPlacement::Bottom;
    frame.folders.clear(); frame.folderLines.clear(); frame.folderOutline.reset();
    const auto addLine = [&](QRect area, QColor color) {
        if (area.intersects(frame.regularClip)) frame.folderLines.append({area, color});
    };
    for (int folder = 0; folder < folders_.size(); ++folder) {
        if (folder == draggedFolder_) continue;
        const auto state = folderPaintState(folder);
        const auto& record = folders_.at(folder);
        const bool headerHere = layout_.headerLanes.value(folder, -1) == laneIndex;
        if (headerHere && frame.regularClip.intersects(layout_.headers.at(folder).translated(origin)))
            frame.folders.append({layout_.headers.at(folder).translated(origin), state, record.id});
        QVector<QRect> rows;
        if (headerHere) rows.append(layout_.headers.at(folder));
        for (int index = record.first; index < record.first + record.count && index < tabs_.size(); ++index) {
            if (index == dragged_ || layout_.lanes.value(index, -1) != laneIndex) continue;
            const auto area = layout_.tabs.at(index);
            auto row = std::find_if(rows.begin(), rows.end(), [&](const QRect& r) { return vertical() || r.top() == area.top(); });
            if (row == rows.end()) rows.append(area); else *row |= area;
        }
        if (record.collapsed && rows.size() <= 1 && (rows.isEmpty() || (headerHere && rows.first() == layout_.headers.at(folder)))) continue;
        for (auto row : rows) {
            row.translate(origin);
            if (vertical()) {
                auto span = row;
                if (headerHere) span.setTop(layout_.headers.at(folder).translated(origin).bottom() + 1 + metrics_.rowGap);
                if (span.height() <= 0 || record.collapsed) continue;
                const int x = layout_.headers.value(folder).isNull() ? span.left()
                    : layout_.headers.at(folder).translated(origin).left() + metrics_.folderIndent / 2 - metrics_.folderLine / 2;
                addLine(QRect(x, span.top(), metrics_.folderLine, span.height()), state.color);
            } else {
                const int start = headerHere && row.left() == layout_.headers.at(folder).translated(origin).left()
                    ? painter_.folderChipRect(layout_.headers.at(folder).translated(origin)).left() : row.left();
                const int y = bottom ? row.top() : row.bottom() + 1 - metrics_.folderLine;
                QRect line(start, y, row.right() + 1 - start, metrics_.folderLine);
                const auto current = folderOf(current_) == folder && layout_.lanes.value(current_, -1) == laneIndex ? animatedRect(current_) : QRect{};
                if (!current.isEmpty() && current.intersects(line.adjusted(0, -1, 0, 1))) {
                    addLine(QRect(line.left(), y, current.left() - line.left(), metrics_.folderLine), state.color);
                    addLine(QRect(current.right() + 1, y, line.right() - current.right(), metrics_.folderLine), state.color);
                } else addLine(line, state.color);
            }
        }
    }
    const int folder = folderOf(current_);
    if (folder >= 0 && folder != draggedFolder_ && current_ != dragged_ && layout_.lanes.value(current_, -1) == laneIndex) {
        const auto state = folderPaintState(folder);
        frame.folderOutline = TabStripFolderOutline{animatedRect(current_), shapeOf(current_), state.color};
    }
}

std::shared_ptr<const TabStripFrame> TabBar::captureStripFrame(const QFont& font, Qt::LayoutDirection direction)
{
    std::shared_ptr<TabStripFrame> frame;
    for (auto& buffer : stripBuffers_) if (!buffer || buffer.use_count() == 1) {
        if (!buffer) buffer = std::make_shared<TabStripFrame>();
        frame = buffer; break;
    }
    if (!frame) frame = std::make_shared<TabStripFrame>();
    frame->style = labelStyle(); frame->font = font; frame->direction = direction;
    frame->vertical = vertical(); frame->windowActive = window()->isActiveWindow();
    frame->background = {}; frame->separators.clear(); frame->scrollbars.clear(); frame->overlayShadow = {};
    if (overlayActive() && !vertical()) {
        frame->background = rect();
        if (placement_ == TabBarPlacement::Bottom) frame->background.setTop(rowShadowExtent());
        else frame->background.setBottom(height() - rowShadowExtent() - 1);
        frame->shadowAbove = placement_ == TabBarPlacement::Bottom;
        const int shadow = rowShadowExtent();
        frame->overlayShadow = QRect(0, frame->shadowAbove ? 0 : height() - shadow, width(), shadow);
        frame->shadowOpacity = rowsReveal_.value();
    }
    const auto origin = tabOrigin();
    for (int index = 0; index + 1 < tabs_.size(); ++index) {
        const int next = index + 1;
        if (vertical() || layout_.lanes.at(index) < 0 || layout_.lanes.at(index) != layout_.lanes.at(next)) continue;
        if (index == current_ || next == current_ || index == hover_.hit().tab || next == hover_.hit().tab || tabPreset(index) || tabPreset(next)) continue;
        if (slides_.contains(tabs_.at(index).data.toUuid()) || slides_.contains(tabs_.at(next).data.toUuid())) continue;
        const auto a = layout_.visibleRect(index), b = layout_.visibleRect(next);
        if (a.isEmpty() || b.isEmpty() || a.top() != b.top() || a.right() + 1 != b.left()) continue;
        const int height = metrics_.rowHeight / 2;
        frame->separators.append(QRect(b.left() + origin.x(), a.top() + origin.y() + (a.height() - height) / 2, metrics_.separator, height));
    }
    frame->lanes.resize(layout_.laneList.size());
    for (int lane = 0; lane < layout_.laneList.size(); ++lane) {
        captureLaneFrame(frame->lanes[lane], lane, font, direction);
        if (layout_.laneList.at(lane).maximumOffset() > 0 && lane == scrollingLane_ && scrollbarOpacity_.value() > 0)
            frame->scrollbars.append({scrollThumb(lane), scrollbarOpacity_.value(), lane == scrollDragLane_});
    }
    frame->sectionSeparator = layout_.separator.isNull() ? QRect{} : layout_.separator.translated(origin);
    sampleTabDepartures(departing_, mapToGlobal(QPoint()), 1 - presenceProgress_.value(), painter_, frame->departures);
    return frame;
}

} // namespace window_system
