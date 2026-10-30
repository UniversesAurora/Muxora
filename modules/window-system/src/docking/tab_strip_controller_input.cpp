// SPDX-License-Identifier: MIT
#include "tab_strip_controller.hpp"
#include "tab_keyboard_controller.hpp"
#include "style/tab_appearance_p.hpp"
#include <window_system/interface_appearance.hpp>
#include <algorithm>

namespace window_system {

void TabStripController::restoreScrollOffsets(int regular, int sticky, int rows)
{
    scrollMotion_.cancel(); smoothLane_ = -1;
    regular = std::max(0, regular); sticky = std::max(0, sticky); rows = std::max(0, rows);
    if (regularOffset_ == regular && stickyOffset_ == sticky && rowsOffset_ == rows) return;
    regularOffset_ = regular; stickyOffset_ = sticky; rowsOffset_ = rows;
    relayout(false, false);
}

void TabStripController::setScrollOffset(int offset)
{
    for (int lane = 0; lane < layout_.laneList.size(); ++lane)
        if (!layout_.laneList.at(lane).sticky && !layout_.laneList.at(lane).vertical) { setLaneOffset(lane, offset); return; }
}

void TabStripController::setLaneOffset(int lane, int offset)
{
    if (!applyingScrollMotion_) { scrollMotion_.cancel(); smoothLane_ = -1; }
    if (lane < 0 || lane >= layout_.laneList.size()) return;
    const auto& info = layout_.laneList.at(lane);
    offset = std::clamp(offset, 0, info.maximumOffset());
    if (offset == info.offset) return;
    if (info.maximumOffset() > 0 && isVisible()) revealScrollbar(lane);
    if (!vertical() && info.vertical) rowsOffset_ = offset;
    else (info.sticky ? stickyOffset_ : regularOffset_) = offset;
    if (!layout_.scrollLane(lane, offset)) { relayout(false, false); return; }
    arrangedInput_.stickyOffset = stickyOffset_; arrangedInput_.regularOffset = regularOffset_;
    arrangedInput_.rowsOffset = rowsOffset_;
    relayoutTrailing();
    carrierBounds_.reset();
    invalidateCarrier(true);

    syncAttachment();
    surfaceFrameChanged();
    changes_.view = true;

}

bool TabStripController::scrollLaneBy(int lane, int delta, bool smooth)
{
    const auto info = layout_.laneList.at(lane);
    if (!smooth || !animationsEnabled() || !environment_.smoothScrolling) {
        const bool moved = std::clamp(info.offset + delta, 0, info.maximumOffset()) != info.offset;
        setLaneOffset(lane, info.offset + delta); return moved;
    }
    if (smoothLane_ != lane) scrollMotion_.cancel();
    smoothLane_ = lane;
    scrollMotion_.scrollBy(delta, info.offset, 0, info.maximumOffset(), timestampNs_);
    return qRound(scrollMotion_.target()) != info.offset;
}

void TabStripController::revealScrollbar(int lane)
{
    scrollbars_.reveal(lane);
    showScrollbar(true);
    if (!scrollbars_.dragging()) scrollbarHideDeadlineNs_ = timestampNs_ + qint64(TabScrollbarState::hideDelayMs) * 1'000'000;
    else scrollbarHideDeadlineNs_.reset();

}

void TabStripController::showScrollbar(bool shown)
{
    if (!scrollbars_.show(shown, animationsEnabled(), timestampNs_)) return;
    requestRepaint();
}

bool TabStripController::scrollAt(QPoint local, QPoint delta, bool smooth)
{
    return applyScroll(routeScroll(local, delta), smooth);
}

void TabStripController::ensureVisible(int index)
{
    scrollMotion_.cancel(); smoothLane_ = -1;
    if (index < 0 || index >= layout_.tabs.size() || layout_.lanes.at(index) < 0) return;
    const int outerIndex = layout_.rowScrollLane;
    if (outerIndex >= 0 && outerIndex != layout_.lanes.at(index)) {
        const auto outer = layout_.laneList.at(outerIndex);
        setLaneOffset(outerIndex, outer.offsetToReveal(layout_.tabs.at(index), metrics_.edgeFade));
        if (index >= layout_.tabs.size() || layout_.lanes.at(index) < 0) return;
    }
    const int laneIndex = layout_.lanes.at(index);
    const auto lane = layout_.laneList.at(laneIndex);
    if (lane.frozen && index < stickyCount_) return;
    setLaneOffset(laneIndex, lane.offsetToReveal(layout_.tabs.at(index), metrics_.edgeFade));
}

bool TabStripController::fullyVisible(int index) const
{
    if (index < 0 || index >= layout_.tabs.size() || layout_.tabs.at(index).isEmpty() || layout_.lanes.at(index) < 0) return false;
    const auto& lane = layout_.laneList.at(layout_.lanes.at(index));
    const auto viewport = lane.frozen && index < stickyCount_ ? lane.viewport : lane.readableViewport(metrics_.edgeFade);
    if (!viewport.contains(layout_.tabs.at(index))) return false;
    return layout_.rowScrollLane < 0 || layout_.rowScrollLane == layout_.lanes.at(index)
        || layout_.laneList.at(layout_.rowScrollLane).readableViewport(metrics_.edgeFade).contains(layout_.tabs.at(index));
}

void TabStripController::inheritHover(const TabStripController& previous, QPoint local)
{
    if (!previous.hover_.hasSession() || !rect().contains(local)) return;
    hover_.beginSession();
    updateHover(local);
    if (hover_.hit().tab >= 0) { applyActionFade(TabHoverTransition::Fade::Show, hover_.hit().tab); requestRepaint(); }
}

void TabStripController::applyActionFade(TabHoverTransition::Fade fade, int index)
{
    if (fade == TabHoverTransition::Fade::None) return;
    (void)actionMotion_.apply(fade, index, animationsEnabled(), timestampNs_);
    if (!actionMotion_.needsFrame()) actionMotion_.finish();
    invalidateCarrier(false);
}

void TabStripController::updateHover(QPoint local)
{
    TabHoverHit hit;
    hit.tab = tabAt(local);
    hit.action = hit.tab >= 0 && actionRect(hit.tab).contains(local)
        && (hit.tab < stickyCount_ ? !isCompactSticky(hit.tab) : true);
    hit.lane = layout_.laneList.isEmpty() || !rect().contains(local) ? -1 : laneAt(local);
    hit.folder = rect().contains(local) ? folderAt(local) : -1;
    hit.folderAdd = hit.folder >= 0 && vertical() && painter_.folderAddRect(folderHeaderRect(hit.folder)).contains(local);
    const auto transition = hover_.move(hit);
    applyActionFade(transition.fade, hit.tab);
    if (transition.changed) requestRepaint();
}

TabPointerHit TabStripController::pointerHit(QPoint local, bool releasing) const
{
    TabPointerHit hit;
    hit.tab = tabAt(local);
    hit.folder = hit.tab < 0 ? folderAt(local) : -1;
    const int action = releasing ? pointer_.pressedAction() : hit.tab;
    if (action >= 0 && actionRect(action).contains(local)
        && (releasing || !(isCompactSticky(action) && !vertical()))) hit.action = action;
    hit.folderAdd = releasing && vertical() && hit.folder >= 0
        && painter_.folderAddRect(folderHeaderRect(hit.folder)).contains(local);
    return hit;
}

TabWheelDecision TabStripController::routeScroll(QPoint local, QPoint delta)
{
    if (delta.isNull()) return {};
    const TabWheelPolicy policy{
        .vertical = vertical(), .bottom = placement_ == TabBarPlacement::Bottom,
        .collapsible = collapsible(), .wheelExpandRows = appearance_.wheelExpandRows,
        .collapsed = collapsedNow(), .expanded = rowsState_.expanded()
    };
    return wheelController_.route(layout_, local - tabOrigin(), delta, policy, timestampNs_ / 1'000'000);
}

bool TabStripController::applyScroll(const TabWheelDecision& decision, bool smooth)
{
    using Action = TabWheelDecision::Action;
    if (decision.action == Action::Ignore) return false;
    if (decision.action == Action::Expand) { setExpanded(true, true); return true; }
    if (decision.action == Action::Collapse) { setExpanded(false); return true; }
    if (decision.action == Action::HoldAtBoundary) return true;
    // The view delivers its pre-scroll notification between routing and apply.
    // That listener may replace the tab layout while keeping the view alive.
    const int lane = decision.lane;
    if (lane < 0 || lane >= layout_.laneList.size()) return true;
    if (decision.horizontalPriority) {
        revealScrollbar(lane); scrollLaneBy(lane, decision.delta, smooth); return true;
    }
    const bool moved = scrollLaneBy(lane, decision.delta, smooth);
    wheelController_.observeRowScroll(layout_, lane, rowsState_.expanded(), placement_ == TabBarPlacement::Bottom,
        timestampNs_ / 1'000'000);
    return moved;
}

bool TabStripController::scrollbarHit(QPoint local) const
{
    return scrollbars_.shownLane() >= 0 && scrollbars_.shownLane() < layout_.laneList.size()
        && scrollbarHitRect().contains(local);
}

void TabStripController::activatePointerDecision(const TabPointerDecision& decision)
{
    if (decision.action != TabPointerDecision::Action::ActivateTab) return;
    setCurrentIndex(decision.index);
    if (appearance_.rowExpansion == TabRowExpansion::Overlay && rowsState_.expanded()) setExpanded(false);
}

TabPointerDecision TabStripController::pointerPress(Qt::MouseButton button, QPoint local, QPoint global)
{
    if (button == Qt::MiddleButton) return pointer_.press(button, {tabAt(local)}, {});
    if (button != Qt::LeftButton) return {};
    if (scrollbarHit(local)) {
        const int lane = scrollbars_.shownLane();
        const auto geometry = tabScrollbarGeometry(layout_.laneList.at(lane), scrollbarMetrics());
        if (const auto offset = geometry.jumpOffset(local)) setLaneOffset(lane, *offset);
        wheelController_.reset();
        scrollbars_.beginDrag(lane, local, layout_.laneList.at(lane).offset);
        invalidatePresentation(); revealScrollbar(lane); return {};
    }
    const auto hit = pointerHit(local, false);
    const auto label = hit.folder >= 0 ? layout_.headers.at(hit.folder).translated(tabOrigin())
        : hit.tab >= 0 ? animatedRect(hit.tab) : QRect(local, QSize(1, 1));
    const auto decision = pointer_.press(button, hit, DragStart::capture(global, local, label));
    invalidatePresentation();
    if (hit.folder >= 0 || hit.action >= 0) requestRepaint();
    activatePointerDecision(decision);
    return decision;
}

TabPointerDecision TabStripController::pointerMove(Qt::MouseButtons buttons, QPoint local, QPoint global, int dragDistance)
{
    if (columnPeek_.iconsOnly() && rect().contains(local)) setColumnHovered(true);
    if (const auto scroll = scrollbars_.move(layout_, scrollbarMetrics(), local)) {
        setLaneOffset(scroll->lane, scroll->offset); return {};
    }
    updateHover(local);
    const auto decision = pointer_.move(buttons, global, dragDistance);
    if (decision.action != TabPointerDecision::Action::None) invalidatePresentation();
    return decision;
}

TabPointerDecision TabStripController::pointerRelease(Qt::MouseButton button, QPoint local)
{
    if (button == Qt::MiddleButton)
        return pointer_.release(button, {tabAt(local)}, stickyCount_, guardsMiddleClick(appearance_.preventStickyClose));
    if (scrollbars_.endDrag()) {
        wheelController_.reset(); hoverScrollbar(); changes_.reconcileHover = true; requestRepaint(); return {};
    }
    const auto hit = pointerHit(local, true);
    const bool repaint = pointer_.pressedFolder() >= 0 || pointer_.pressedAction() >= 0;
    const bool changed = repaint || pointer_.pressedTab() >= 0;
    const auto decision = pointer_.release(button, hit, stickyCount_, guardsMiddleClick(appearance_.preventStickyClose));
    if (changed) invalidatePresentation();
    if (repaint) requestRepaint();
    return decision;
}

void TabStripController::enter(QPoint local)
{
    collapseDeadlineNs_.reset(); updateHover(local); setColumnHovered(true);
}

void TabStripController::leave()
{
    applyActionFade(TabHoverTransition::Fade::Out); hover_.leave();
    if (!columnManaged_) setColumnHovered(false);
    if (rowsState_.collapseOnLeave())
        collapseDeadlineNs_ = timestampNs_ + qint64(TabRowsState::collapseDelayMs) * 1'000'000;
    requestRepaint();
}

void TabStripController::controlsEntered()
{
    updateHover(QPoint(-1, -1)); hover_.endSession();
}

void TabStripController::setPopupOpen(bool open)
{
    columnPeek_.setPopupOpen(open);
    if (open) peekDeadlineNs_.reset();
}

void TabStripController::hoverScrollbar()
{
    if (!scrollbars_.dragging())
        scrollbarHideDeadlineNs_ = timestampNs_ + qint64(TabScrollbarState::hideDelayMs) * 1'000'000;
}

bool TabStripController::keyPress(int key)
{
    const auto decision = tabKeyboardDecision(key, current_, count(), vertical(), rowsState_.expanded());
    using Action = TabKeyboardDecision::Action;
    if (decision.action == Action::Ignore) return false;
    if (decision.action == Action::ActivateTab) { setCurrentIndex(decision.index); ensureVisible(decision.index); }
    else if (decision.action == Action::Collapse) setExpanded(false);
    return true;
}

bool TabStripController::dismissesOutsideInput() const
{
    return rowsState_.dismissesOutsideInput(appearance_.rowExpansion == TabRowExpansion::Overlay);
}

bool TabStripController::watchesOutsideInput() const
{
    return rowsState_.watchesOutsideInput(rowsState_.needsFrame(), appearance_.rowExpansion == TabRowExpansion::Overlay);
}

} // namespace window_system
