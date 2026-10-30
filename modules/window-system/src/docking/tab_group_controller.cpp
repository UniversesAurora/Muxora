// SPDX-License-Identifier: MIT
#include "tab_group_controller.hpp"
#include "style/tab_appearance_p.hpp"
#include <window_system/interface_appearance.hpp>

#include <QScopedValueRollback>
#include <algorithm>

namespace window_system {

TabGroupController::TabGroupController(const Theme& theme, TabStripController& strip)
    : theme_(theme), strip_(strip)
{
}

void TabGroupController::setEnvironment(const TabGroupEnvironment& environment)
{
    const bool moved = environment_.size != environment.size || environment_.originInWindow != environment.originInWindow
        || environment_.windowClientBounds != environment.windowClientBounds
        || (environment.visible && !environment_.visible);
    const bool surface = environment_.windowActive != environment.windowActive;
    environment_ = environment;
    if (moved) queueViewportReflow();
    if (surface) changes_.surface = true;
}

void TabGroupController::setTheme(const Theme& theme)
{
    theme_ = theme;
    changes_.layout = true;
    updateCardJoin();
}

void TabGroupController::advance(qint64 timestampNs)
{
    timestampNs_ = timestampNs;
    const auto changed = motion_.advance(timestampNs);
    if (changed.layout) changes_.layout = true;
    else if (changed.surface) updateCardJoin();
}

void TabGroupController::syncMotion(bool restarted)
{
    if (restarted) changes_.motion = true;
}

int TabGroupController::columnWidth() const
{
    return std::clamp(uiPixels(verticalWidth_ > 0 ? verticalWidth_ : appearance_.verticalTabWidth),
        strip_.minimumColumnWidth(), compactPresentation_ ? uiPixels(maximumVerticalTabWidth) : maximumColumnWidth());
}

int TabGroupController::maximumColumnWidth() const
{
    const int margin = contentEdgeToEdge_ ? 0 : strip_.metrics().cardMargin;
    // The strip borrows space only from its own content, never from the
    // enclosing splitter. Keep the content's declared minimum intact.
    return tabGroupColumnLimit(environment_.size.width(), environment_.contentMinimumWidth, margin,
        strip_.metrics().columnGap, strip_.minimumColumnWidth(), maximumVerticalTabWidth);
}

TabGroupGeometryInput TabGroupController::geometryInput() const
{
    const auto& metrics = strip_.metrics();
    TabGroupGeometryInput input;
    input.bounds = QRect(QPoint(), environment_.size); input.placement = strip_.placement();
    input.compact = compactPresentation_; input.edgeToEdge = contentEdgeToEdge_; input.barVisible = barVisible_;
    input.titled = !groupName_.isEmpty() && appearance_.showGroupNames && !compactPresentation_;
    input.iconMode = iconMode_; input.resizing = resizeOrigin_.has_value();
    input.normalDragStyle = normalDragStyle_.has_value(); input.dragWidth = dragTabArea_.width();
    input.cardMargin = metrics.cardMargin; input.columnGap = metrics.columnGap;
    input.columnWidth = vertical() ? columnWidth() : 0; input.iconColumnWidth = metrics.verticalIconWidth;
    input.columnOutset = metrics.columnOverlayOutset; input.verticalInset = metrics.verticalInset;
    input.rowHeight = metrics.rowHeight; input.resizeBand = metrics.resizeBand;
    input.expansion = motion_.expansion(); input.peek = motion_.peek();
    return input;
}

TabGroupPlacement TabGroupController::place()
{
    if (vertical()) {
        syncMotion(motion_.syncColumn(iconMode_, strip_.peeking(),
            environment_.visible && !compactPresentation_ && !resizeOrigin_, environment_.animations, timestampNs_));
    }
    auto input = geometryInput();
    TabGroupPlacement result;
    result.vertical = vertical(); result.iconMode = iconMode_;
    result.nameShown = !groupName_.isEmpty() && appearance_.showGroupNames && !vertical();
    if (!barVisible_) {
        result.geometry = layoutTabGroup(input);
    } else if (vertical()) {
        strip_.setColumnReveal(std::max(input.expansion, input.peek), motion_.columnRunning());
        strip_.setAvailableSize(tabGroupAvailableSize(input));
        input.columnWidth = columnWidth();
        result.geometry = layoutTabGroup(input);
        result.barOrigin = result.geometry.panel.topLeft() + result.geometry.bar.topLeft();
    } else {
        const auto available = tabGroupAvailableSize(input);
        strip_.setAvailableSize(available);
        input.reservedRows = strip_.reservedSize(available); input.shownRows = strip_.displaySize(available);
        result.geometry = layoutTabGroup(input);
        result.barOrigin = result.geometry.bar.topLeft();
    }
    placement_ = result;
    return result;
}

void TabGroupController::placed()
{
    if (lifting_) strip_.setLiftedWithAnchor(toStrip(liftedGlobal_), liftedGrab_);
    updateCardJoin();
}

int TabGroupController::shownColumnWidth() const
{
    return tabColumnExtent(strip_.metrics().verticalIconWidth, columnWidth(), std::max(motion_.expansion(), motion_.peek()));
}

QSize TabGroupController::minimumSize(QSize contentMinimum) const
{
    const int margin = compactPresentation_ || contentEdgeToEdge_ ? 0 : strip_.metrics().cardMargin;
    auto size = contentMinimum + QSize(2 * margin, 2 * margin);
    if (!barVisible_) return size;
    // A requested column width must not become a splitter minimum: resizing
    // the column would otherwise resize this group and squeeze its neighbor.
    if (vertical()) size.rwidth() += (iconMode_ ? strip_.metrics().verticalIconWidth : strip_.minimumColumnWidth())
        + strip_.metrics().columnGap;
    else size.rheight() += strip_.reservedSize(QSize(std::max(environment_.size.width(), 1), environment_.size.height())).height();
    return size;
}

QSize TabGroupController::preferredSize(QSize contentPreferred) const
{
    // The card keeps a margin all round; a vertical column a gap beside it.
    const int margin = compactPresentation_ || contentEdgeToEdge_ ? 0 : strip_.metrics().cardMargin;
    auto size = contentPreferred + QSize(2 * margin, 2 * margin);
    if (!barVisible_) return size;
    if (vertical()) size.rwidth() += columnWidth() + 2 * margin;
    else size.rheight() += strip_.metrics().rowHeight + strip_.metrics().outerMargin;
    return size;
}

void TabGroupController::setAppearance(const TabAppearance& appearance, bool pinned)
{
    // Label widths may change, which moves every tab after the first. Keep a
    // fully shown active tab where it is on screen and let the others resize
    // around it; once it reaches a viewport edge it is pushed only as far as
    // needed to stay shown. Without a shown active tab, keep the tab at the
    // leading edge in place. The raw offset alone would clamp and then land
    // on unrelated tabs.
    struct Anchor { int tab; int shift; bool active; };
    std::optional<Anchor> anchor;
    // Settle a queued reflow first, so the anchor is taken from what is shown.
    if (pendingViewportState_ && !pendingScrollOffset_ && applyPendingViewportState()) {
        pendingViewportState_.reset(); settledViewport_ = currentViewportState();
    }
    auto next = appearance;
    if (pinned) { next.showIcons = appearance.showSidebarIcons; next.showNumbers = appearance.showSidebarNumbers; }
    const auto placement = pinned ? next.pinnedPlacement : next.centralPlacement;
    const bool sameRow = strip_.scrollsHorizontally() && !next.multiline
        && (placement == TabBarPlacement::Top || placement == TabBarPlacement::Bottom);
    if (environment_.visible && barVisible_ && sameRow && !pendingViewportState_ && !pendingScrollOffset_ && strip_.count()) {
        const int offset = scrollOffset();
        const int width = strip_.scrollViewport().width();
        const int current = strip_.currentIndex();
        const auto active = current >= 0 ? strip_.contentTabRect(current) : QRect{};
        if (!active.isEmpty() && active.left() >= offset + stickyInset(current) && active.right() < offset + width) {
            anchor = Anchor{current, active.left() - offset, true};
        } else {
            // Sticky tabs kept in view cannot anchor the scrolled ones.
            const int inset = strip_.frozenExtent();
            int leading = inset ? std::min(strip_.stickyCount(), strip_.count() - 1) : 0;
            while (leading + 1 < strip_.count() && strip_.contentTabRect(leading).right() < offset + inset) ++leading;
            anchor = Anchor{leading, strip_.contentTabRect(leading).left() - offset - inset, false};
        }
    }
    appearance_ = next; pinned_ = pinned;
    strip_.setAppearance(appearance_, placement);
    (void)place();
    changes_.layout = true;
    if (!anchor || anchor->tab >= strip_.count()) return;
    const auto area = strip_.contentTabRect(anchor->tab);
    const int inset = stickyInset(anchor->tab);
    int offset = area.left() - anchor->shift;
    if (anchor->active) {
        const int width = strip_.scrollViewport().width();
        offset = width - inset >= area.width() ? qBound(area.right() - width + 1, offset, area.left() - inset) : area.left() - inset;
    } else {
        // A partly scrolled-off leading tab keeps its hidden part, within its new width.
        offset = area.left() - inset - std::max(anchor->shift, -std::max(0, area.width() - 1));
    }
    // The strip limits the offset to the real content range. A reflow
    // queued by the resize above still holds the old widths; this replaces it.
    strip_.setScrollOffset(offset);
    pendingViewportState_.reset();
    settledViewport_ = currentViewportState();
}

void TabGroupController::setGroupName(const QString& name)
{
    // At the end of a horizontal strip; above a vertical one, as its title
    // (tab strip design 13).
    const bool shown = !name.isEmpty() && appearance_.showGroupNames && !vertical();
    if (groupName_ == name && placement_.nameShown == shown) return;
    groupName_ = name;
    placement_.nameShown = shown;
    changes_.layout = true;
}

void TabGroupController::setVerticalState(int width, bool iconMode)
{
    if (verticalWidth_ == width && iconMode_ == iconMode && strip_.iconMode() == iconMode) return;
    verticalWidth_ = width; iconMode_ = iconMode;
    strip_.setIconMode(iconMode);
    changes_.layout = true;
}

void TabGroupController::toggleIconMode()
{
    iconMode_ = !iconMode_;
    strip_.setIconMode(iconMode_);
    changes_.layout = true;
    changes_.columnState = std::pair{verticalWidth_, iconMode_};
}

void TabGroupController::setTabBarVisible(bool visible)
{
    if (barVisible_ == visible) return;
    barVisible_ = visible;
    changes_.layout = true;
}

void TabGroupController::setContentEdgeToEdge(bool enabled)
{
    if (contentEdgeToEdge_ == enabled) return;
    contentEdgeToEdge_ = enabled;
    changes_.layout = true;
}

void TabGroupController::setGroupFocused(bool focused)
{
    strip_.setGroupFocused(focused);
    if (motion_.focused() == focused) return;
    syncMotion(motion_.setFocused(focused, environment_.visible && environment_.animations, timestampNs_));
    updateCardJoin();
}

TabGroupController::SurfaceState TabGroupController::captureSurfaceState() const
{
    return {motion_.focus(), motion_.focused(), strip_.captureSurfaceState()};
}

void TabGroupController::inheritSurface(const SurfaceState& previous, bool animate)
{
    syncMotion(motion_.inheritFocus(previous.focus, previous.focused, animate, timestampNs_));
    strip_.inheritSurface(previous.tabs, animate);
    updateCardJoin();
}

void TabGroupController::updateCardJoin()
{
    const QRectF body = compactPresentation_ ? QRectF{}
        : QRectF(placement_.geometry.content).translated(-placement_.barOrigin);
    strip_.setSurfaceContext(body, motion_.focus(), 1 - motion_.peek());
    changes_.surface = true;
}

TabGroupSurfaceFrame TabGroupController::surfaceFrame(bool extension) const
{
    TabGroupSurfaceFrame frame;
    if (compactPresentation_ || contentEdgeToEdge_) return frame;
    const auto& geometry = placement_.geometry;
    frame.groupBounds = QRect(QPoint(), environment_.size); frame.body = geometry.content; frame.radius = strip_.metrics().cardRadius;
    frame.elevation = interpolateElevation(elevationProfile(ElevationLevel::BodyRest),
        elevationProfile(ElevationLevel::BodyRaised), motion_.focus());
    frame.style = elevationStyleForTheme(theme_); frame.style.focused = motion_.focused();
    if (barVisible_) frame.anchors = strip_.surfaceAnchors(placement_.barOrigin);
    if (vertical() && motion_.peek() > 0) {
        const auto palette = TabPalette::forTheme(theme_);
        auto line = palette.tileBorder.resolve(environment_.windowActive);
        line.setAlphaF(line.alphaF() * motion_.peek());
        frame.column = TabColumnSurface{geometry.panel, QRectF(geometry.panelBody).translated(geometry.panel.topLeft()),
            uiPixelsF(8), motion_.peek(), theme_.headerBackground, line, palette.elevation};
    }
    prepareTabGroupSurfaceFrame(frame);
    if (extension) frame.extension = strip_.surfaceExtension(placement_.barOrigin);
    return frame;
}

void TabGroupController::restoreColumnPeek()
{
    if (!vertical() || !iconMode_) return;
    motion_.restorePeek();
    strip_.restoreColumnPeek(true);
    changes_.layout = true;
}

int TabGroupController::stickyInset(int index) const
{
    return index >= strip_.stickyCount() ? strip_.frozenExtent() : 0;
}

TabViewportState TabGroupController::currentViewportState() const
{
    TabViewportState state;
    state.offset = scrollOffset();
    state.stickyOffset = strip_.stickyScrollOffset();
    state.rowsOffset = strip_.rowScrollOffset();
    state.rowsExpanded = strip_.expanded();
    state.windowClientBounds = environment_.windowClientBounds;
    if (environment_.visible && barVisible_) {
        const auto viewport = strip_.scrollViewport();
        state.bounds = {environment_.originInWindow + placement_.barOrigin + viewport.topLeft(), viewport.size()};
    }
    const auto current = strip_.currentIndex();
    if (current >= 0) {
        state.activeTab = strip_.tabData(current).toUuid();
        state.activeFullyVisible = !state.bounds.isEmpty() && strip_.fullyVisible(current);
    }
    return state;
}

TabViewportState TabGroupController::captureViewportState() const
{
    // Consecutive API mutations before the next layout turn retain the same
    // origin instead of capturing an intermediate, incompletely sized strip.
    auto state = pendingViewportState_.value_or(currentViewportState());
    state.rowsExpanded = strip_.expanded();
    return state;
}

void TabGroupController::restoreViewportState(TabViewportState state)
{
    if (strip_.collapsible() && state.rowsExpanded) strip_.setExpanded(*state.rowsExpanded, false, false);
    pendingScrollOffset_.reset();
    pendingViewportState_ = std::move(state);
    applyPendingViewportState();
    queueViewportReflow();
}

void TabGroupController::cancelViewportReflow()
{
    pendingViewportState_.reset(); pendingScrollOffset_.reset();
    settledViewport_ = currentViewportState();
}

void TabGroupController::queueViewportReflow()
{
    if (!pendingViewportState_ && settledViewport_ && !pendingScrollOffset_)
        pendingViewportState_ = settledViewport_;
    changes_.reflow = true;
}

void TabGroupController::applyViewportReflow()
{
    if (applyPendingViewportState()) {
        pendingViewportState_.reset(); settledViewport_ = currentViewportState();
    }
}

bool TabGroupController::applyPendingViewportState()
{
    if (!pendingViewportState_) return true;
    QScopedValueRollback applying(applyingViewportState_, true);
    const auto old = *pendingViewportState_;
    const auto current = currentViewportState();
    const auto decision = tabViewportReflow(old, current, strip_.scrollsHorizontally(), barVisible_,
        strip_.contentTabRect(strip_.currentIndex()), stickyInset(strip_.currentIndex()));
    if (!decision.ready) return false;
    if (decision.restore) {
        strip_.restoreScrollOffsets(decision.offset, decision.stickyOffset, decision.rowsOffset);
        if (decision.ensureActive) strip_.ensureVisible(strip_.currentIndex());
    }
    return true;
}

void TabGroupController::restoreScrollOffset(int offset)
{
    cancelViewportReflow();
    pendingScrollOffset_ = qMax(0, offset);
    strip_.setScrollOffset(*pendingScrollOffset_);
    // The replacement viewport gets its real width from the owning layout.
    // Restore again after that pass; explicit navigation or scrolling cancels
    // the pending restore so it cannot overwrite newer input.
    changes_.scrollRestore = true;
}

void TabGroupController::finishScrollOffsetRestore()
{
    if (pendingScrollOffset_) strip_.setScrollOffset(*pendingScrollOffset_);
    pendingScrollOffset_.reset(); settledViewport_ = currentViewportState();
}

void TabGroupController::viewChanged()
{
    if (!applyingViewportState_ && !pendingViewportState_ && !pendingScrollOffset_) settledViewport_ = currentViewportState();
    updateCardJoin();
}

void TabGroupController::setCurrentIndex(int index, bool reveal)
{
    strip_.setCurrentIndex(index);
    if (reveal && index >= 0) { cancelViewportReflow(); strip_.ensureVisible(index); }
}

void TabGroupController::beginColumnResize(int globalX)
{
    resizeOrigin_ = globalX;
    resizeStartWidth_ = columnWidth();
}

void TabGroupController::moveColumnResize(int globalX)
{
    if (!resizeOrigin_) return;
    const bool left = strip_.placement() == TabBarPlacement::Left;
    const int moved = globalX - *resizeOrigin_;
    const int requested = resizeStartWidth_ + (left ? moved : -moved);
    const int minimum = strip_.minimumColumnWidth();
    // Reaching the title-less row is not yet a collapse. A little extra
    // travel makes the icon-only transition deliberate.
    const bool icons = requested <= minimum - strip_.metrics().iconCollapseTravel;
    // Crossing the collapse threshold is a mode switch, not a new expanded
    // width. Retain this gesture's initial width for peeking and for a later
    // explicit expansion; reversing the drag still resumes direct sizing
    // from its original pointer origin.
    verticalWidth_ = qRound((icons ? resizeStartWidth_ : std::clamp(requested, minimum, maximumColumnWidth())) / interfaceScale());
    if (iconMode_ != icons) { iconMode_ = icons; strip_.setIconMode(icons); }
    changes_.layout = true;
}

void TabGroupController::endColumnResize()
{
    if (!resizeOrigin_) return;
    resizeOrigin_.reset();
    changes_.layout = true;
    changes_.columnState = std::pair{verticalWidth_, iconMode_};
}

void TabGroupController::resetColumnWidth()
{
    resizeOrigin_.reset(); verticalWidth_ = 0;
    changes_.layout = true;
    changes_.columnState = std::pair{0, iconMode_};
}

void TabGroupController::setDragPresentation(bool compact, bool edge)
{
    if (normalDragStyle_ && (!compact || edge)) {
        const auto saved = *normalDragStyle_;
        normalDragStyle_.reset(); dragTabArea_ = {};
        appearance_ = saved.appearance; verticalWidth_ = saved.width; iconMode_ = saved.iconMode;
        strip_.setAppearance(appearance_, saved.placement);
        strip_.setIconMode(iconMode_); strip_.setStickyCount(saved.stickyCount);
    }
    compactPresentation_ = compact;
    strip_.setCarriedDisplay(compact && !edge);
    if (compact) setTabBarVisible(!edge);
    changes_.layout = true;
}

bool TabGroupController::setDragTabStyle(const TabStripController& target, bool sticky)
{
    if (!compactPresentation_) return false;
    const int count = sticky ? strip_.count() : 0;
    const QSize area(target.environment().size.width(), target.availableSize().height());
    const bool icons = target.iconsOnly();
    const auto presentation = target.verticalPresentation();
    const int columns = target.tabLayout().tileColumns;
    if (normalDragStyle_ && appearance_ == target.appearance() && strip_.placement() == target.placement()
        && dragTabArea_ == area && iconMode_ == icons && strip_.stickyCount() == count
        && strip_.carriedTileColumns() == columns && strip_.verticalPresentation() == presentation) return false;
    if (!normalDragStyle_) normalDragStyle_ = DragStyle{appearance_, strip_.placement(), verticalWidth_, iconMode_, strip_.stickyCount()};
    appearance_ = target.appearance(); dragTabArea_ = area;
    verticalWidth_ = qRound(dragTabArea_.width() / interfaceScale()); iconMode_ = icons;
    strip_.setAppearance(appearance_, target.placement());
    strip_.setIconMode(iconMode_); strip_.setStickyCount(count);
    strip_.setCarriedPresentation(presentation);
    strip_.setCarriedTileColumns(columns);
    changes_.layout = true;
    return true;
}

void TabGroupController::setLiftedTab(QPoint global, const DragAnchor& grab)
{
    lifting_ = true;
    liftedGlobal_ = global; liftedGrab_ = grab;
    strip_.setLiftedWithAnchor(toStrip(global), grab);
    // beginTab opens the first gap before asking for an insertion index.
    // Its probe must already belong to the row where the label was taken.
    dragProbe_ = toStrip(global);
}

void TabGroupController::clearLiftedTab()
{
    lifting_ = false;
    strip_.setLifted({});
}

int TabGroupController::insertionIndex(QPoint global, TabLayoutInput::Carried carried)
{
    dragProbe_ = toStrip(global);
    return strip_.insertionAt(dragProbe_, 0, carried).index;
}

void TabGroupController::setInsertionPreview(int index, const QVector<CarriedLabel>& carried, bool carriedSticky,
                                             TabLayoutInput::Carried kind)
{
    if (index < 0 || carried.isEmpty()) { strip_.setDropPreview(-1, false, {}); return; }
    // The drop position decides stickiness (tab strip design 5); a block that
    // was sticky stays so a little past the sticky tabs. It also decides the
    // tab folder a single tab joins (tab strip design 12).
    const int first = strip_.carriedExtent(carried.first(), carriedSticky);
    if (lifting_ && strip_.dropIndex() < 0 && (strip_.draggedIndex() >= 0 || strip_.draggedFolder() >= 0)) {
        // A local lift starts in the slot it vacated. Compacting first and
        // classifying the grabbed point would let the first movement skip a
        // neighbor, especially when the press was near a label's far edge.
        QVector<int> initial;
        for (const auto& label : carried) initial.append(strip_.carriedExtent(label, carriedSticky));
        const int folder = kind == TabLayoutInput::Carried::Tab ? strip_.folderOf(strip_.draggedIndex()) : -1;
        strip_.setDropPreview(index, carriedSticky, std::move(initial), folder);
    }
    const auto decision = strip_.insertionAt(dragProbe_, carriedSticky ? first / 2 : 0, kind);
    QVector<int> block;
    for (const auto& label : carried) block.append(strip_.carriedExtent(label, decision.sticky));
    strip_.setDropPreview(decision.index, decision.sticky, block, decision.folder);
}

DragAnchor TabGroupController::folderGrabAnchor(QUuid folder, QPoint global) const
{
    for (int index = 0; index < strip_.folders().size(); ++index)
        if (strip_.folders().at(index).id == folder)
            return DragAnchor::capture(toStrip(global), strip_.tabLayout().headers.at(index).translated(strip_.tabOrigin()));
    return {};
}

QRect TabGroupController::stripRectGlobal() const
{
    if (!barVisible_) return {};
    return QRect(strip_.environment().globalOrigin, strip_.environment().size);
}

QPoint TabGroupController::setDragHover(QPoint global)
{
    const bool hovered = stripRectGlobal().contains(global);
    if (vertical()) strip_.setColumnHovered(hovered);
    dragProbe_ = toStrip(global);
    // Dragging over collapsed rows shows them all (tab strip design 3.4).
    strip_.setDragExpanded(hovered);
    if (!hovered) return {};
    const auto lane = strip_.scrollViewportAt(dragProbe_);
    if (strip_.scrollsHorizontallyAt(dragProbe_)) {
        const int start = lane.left() + strip_.frozenExtent();
        if (dragProbe_.x() >= start && dragProbe_.x() < start + 24) return {-1, 0};
        if (dragProbe_.x() > lane.right() - 24) return {1, 0};
    } else if (!lane.isEmpty()) {
        if (dragProbe_.y() < lane.top() + 24) return {0, -1};
        if (dragProbe_.y() > lane.bottom() - 24) return {0, 1};
    }
    return {};
}

void TabGroupController::clearDragHover(bool overColumnPanel)
{
    // End drag-only expansion and edge scrolling without inventing a leave
    // event for a pointer that still belongs to the complete column panel.
    strip_.setDragExpanded(false);
    if (vertical()) strip_.setColumnHovered(overColumnPanel);
}

bool TabGroupController::autoScrollStep(QPoint direction)
{
    return !direction.isNull() && strip_.scrollAt(dragProbe_, -direction * 12, false);
}

} // namespace window_system
