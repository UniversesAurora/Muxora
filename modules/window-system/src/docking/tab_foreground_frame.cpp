// SPDX-License-Identifier: MIT
#include "tab_bar.hpp"

namespace window_system {

void TabBar::captureCarriedFolder(TabForegroundFrame& frame, QRect lifted)
{
    const auto& folder = folders_.at(draggedFolder_);
    const auto state = folderPaintState(draggedFolder_);
    const int pitch = metrics_.rowHeight + metrics_.rowGap;
    const QRect header = vertical() ? QRect(lifted.left(), lifted.top(), lifted.width(), metrics_.rowHeight)
        : QRect(lifted.left(), lifted.top(), folderHeaderExtent(folder), metrics_.rowHeight);
    frame.folder = TabStripFolderSample{header, state, folder.id};
    int x = header.right() + 1, y = header.top() + pitch;
    const int count = folder.collapsed ? 0 : folder.count;
    const QRect windowArea(mapFrom(window(), QPoint()), window()->size());
    for (int index = folder.first; index < folder.first + count && index < tabs_.size(); ++index) {
        const QRect area = vertical() ? QRect(lifted.left() + metrics_.folderIndent, y, lifted.width() - metrics_.folderIndent, metrics_.rowHeight)
            : QRect(x, lifted.top(), tabExtent(index, false), metrics_.rowHeight);
        if (area.intersects(windowArea)) {
            TabPaintState tab;
            tab.shape = shapeOf(index); tab.selected = index == current_; tab.groupFocused = true;
            tab.outerRight = placement_ == TabBarPlacement::Right; tab.preset = tabPreset(index);
            tab.icon = appearance_.showIcons ? tabs_.at(index).icon : QIcon{};
            tab.title = tabs_.at(index).title;
            std::optional<SurfaceGeometry> geometry;
            if (tab.selected) geometry = anchorGeometry(area, area.intersected(rect()), false, true);
            tab.elevationManaged = true; tab.surface = geometry ? &*geometry : nullptr;
            auto label = TabPainter::captureLabel(labelGeometry(area, tab.shape, labelParts(index, false), false, false, false),
                tab, frame.font, frame.direction);
            frame.carried.append({tabData(index).toUuid(), index, labelContent(index), std::move(label)});
        }
        x = area.right() + 1; y += pitch;
    }
    if (count && !vertical()) {
        const int line = placement_ == TabBarPlacement::Bottom ? lifted.top() : lifted.bottom() + 1 - metrics_.folderLine;
        const int start = painter_.folderChipRect(header).left();
        frame.folderLine = TabStripFolderLine{QRect(start, line, lifted.right() + 1 - start, metrics_.folderLine), state.color};
    } else if (count) {
        const int x0 = lifted.left() + metrics_.folderIndent / 2 - metrics_.folderLine / 2;
        frame.folderLine = TabStripFolderLine{QRect(x0, header.bottom() + 1 + metrics_.rowGap,
            metrics_.folderLine, lifted.bottom() - header.bottom() - metrics_.rowGap), state.color};
    }
}

TabStripLabelSample TabBar::captureLiftedLabel(QRect area, const QFont& font, Qt::LayoutDirection direction)
{
    TabPaintState state;
    state.shape = shapeOf(dragged_);
    state.lifted = true; state.selected = true; state.groupFocused = true;
    state.shadowOpacity = liftProgress();
    state.sticky = dropIndex_ >= 0 ? dropSticky_ : dragged_ < stickyCount_;
    if (vertical()) state.shape = state.sticky && compactStickyTiles() ? TabShape::Tile : TabShape::Row;
    state.outerRight = placement_ == TabBarPlacement::Right;
    state.preset = tabPreset(dragged_);
    state.number = labelContent(dragged_)->number;
    state.icon = appearance_.showIcons ? tabs_.at(dragged_).icon : QIcon{};
    state.title = tabs_.at(dragged_).title;
    state.iconOnly = state.shape == TabShape::Tile || (vertical() && iconsOnly() && columnReveal_ <= 0)
        || (!vertical() && state.sticky && appearance_.stickySizing == StickyTabSizing::Compact);
    state.actionShown = state.sticky && !state.iconOnly; state.actionReserved = true;
    state.actionIcon = labelStyle()->stickyIcon;
    const auto geometry = anchorGeometry(area, area.intersected(rect()), state.shape == TabShape::Tile, true);
    const auto body = interpolateElevation(elevationProfile(ElevationLevel::BodyRest),
        elevationProfile(ElevationLevel::BodyRaised), surfaceFocus_);
    state.surface = &geometry; state.elevationManaged = true; state.bodyElevation = &body;
    auto label = TabPainter::captureLabel(labelGeometry(area, state.shape, labelParts(dragged_, state.sticky),
        state.iconOnly, state.actionShown, state.sticky), state, font, direction);
    return {tabData(dragged_).toUuid(), dragged_, labelContent(dragged_), std::move(label)};
}

std::shared_ptr<const TabForegroundFrame> TabBar::captureForegroundFrame(const QFont& font, Qt::LayoutDirection direction)
{
    std::shared_ptr<TabForegroundFrame> frame;
    for (auto& buffer : foregroundBuffers_) if (!buffer || buffer.use_count() == 1) {
        if (!buffer) buffer = std::make_shared<TabForegroundFrame>();
        frame = buffer; break;
    }
    if (!frame) frame = std::make_shared<TabForegroundFrame>();
    frame->style = labelStyle(); frame->font = font; frame->direction = direction; frame->vertical = vertical();
    frame->elevation = painter_.palette().elevation;
    frame->elevation.focused = groupFocused_ && window()->isActiveWindow();
    frame->shadows.clear(); frame->incoming.reset(); frame->folder.reset(); frame->folderLine.reset();
    frame->carried.clear(); frame->settling.clear();
    const auto body = interpolateElevation(elevationProfile(ElevationLevel::BodyRest),
        elevationProfile(ElevationLevel::BodyRaised), surfaceFocus_);
    const auto anchors = surfaceAnchors();
    for (const auto& anchor : anchors) if (anchor.lifted || anchor.foreground)
        frame->shadows.append({anchor.geometry, anchor.foreground ? elevationProfile(ElevationLevel::Flat) : body,
            anchor.elevation, anchor.presence, !anchor.foreground});
    if (const auto incoming = incomingRect(); !incoming.isEmpty())
        frame->incoming = TabIncomingSample{incoming.topLeft(), incomingVisual_, anchors.isEmpty() ? SurfaceGeometry{}
            : translatedSurfaceGeometry(anchors.first().geometry, -incoming.topLeft())};
    if (const auto lifted = liftedRect(); !lifted.isEmpty()) {
        if (draggedFolder_ >= 0) captureCarriedFolder(*frame, lifted);
        else frame->carried.append(captureLiftedLabel(lifted, font, direction));
    }
    for (auto it = settling_.cbegin(); it != settling_.cend(); ++it)
        if (it.value() >= 0 && it.value() < count() && tabData(it.value()).toUuid() == it.key())
            if (auto label = labelSample(it.value(), animatedRect(it.value()), font, direction, true))
                frame->settling.append(std::move(*label));
    return frame;
}

} // namespace window_system
