// SPDX-License-Identifier: MIT
#include "tab_strip_controller.hpp"
#include "style/tab_appearance_p.hpp"
#include <window_system/interface_appearance.hpp>
#include <algorithm>

namespace window_system {

int TabStripController::addTab(const QIcon& icon, const QString& title)
{
    tabs_.append(icon, title);
    if (current_ < 0) current_ = 0;
    surfaceMotion_.bindCurrent(currentLabelId());
    relayout(true);
    return int(tabs_.size()) - 1;
}

void TabStripController::removeTab(int index)
{
    if (index < 0 || index >= tabs_.size()) return;
    const auto id = tabs_.at(index).data.toUuid();
    presence_.remove(id); departing_.remove(id);
    const bool wasCurrent = index == current_;
    tabs_.removeAt(index);
    if (index < stickyCount_) --stickyCount_;
    if (dragged_ == index) dragged_ = -1; else if (dragged_ > index) --dragged_;
    hover_.removedTab(index);
    actionMotion_.removedTab(index);
    pointer_.cancelTabs();
    const int previous = current_;
    if (tabs_.isEmpty()) current_ = -1;
    else if (index < current_) --current_;
    else if (wasCurrent) current_ = std::min(index, int(tabs_.size()) - 1);
    surfaceMotion_.bindCurrent(currentLabelId());
    relayout(true);
    if (wasCurrent || previous != current_) changes_.current = current_;
}

QVariant TabStripController::tabData(int index) const { return tabs_.data(index); }

void TabStripController::setTabData(int index, const QVariant& value)
{
    if (!tabs_.setData(index, value)) return;
    if (index == current_) surfaceMotion_.bindCurrent(currentLabelId());
    invalidateCarrier(true);
}

QString TabStripController::tabText(int index) const { return tabs_.title(index); }

void TabStripController::setTabText(int index, const QString& text)
{
    if (tabs_.setTitle(index, text)) relayout(true);
}

QIcon TabStripController::tabIcon(int index) const { return tabs_.icon(index); }

void TabStripController::setTabIcon(int index, const QIcon& icon)
{
    if (tabs_.setIcon(index, icon)) relayout(true);
}

QString TabStripController::tabToolTip(int index) const { return tabs_.toolTip(index); }

void TabStripController::setTabToolTip(int index, const QString& tip) { tabs_.setToolTip(index, tip); }

void TabStripController::setTabMetadata(int index, const TabRecord& tab)
{
    if (!tabs_.setColor(index, tab.colorId)) return;
    invalidateCarrier(true); requestRepaint();
}

std::shared_ptr<const TabStripLabelStyle> TabStripController::labelStyle() const
{
    if (!labelStyle_ || labelStyle_->logicalDpiX != logicalDpiX() || labelStyle_->logicalDpiY != logicalDpiY()) {
        auto style = std::make_shared<TabStripLabelStyle>();
        style->metrics = metrics_; style->palette = painter_.palette();
        style->font = font(); style->direction = layoutDirection();
        style->logicalDpiX = logicalDpiX(); style->logicalDpiY = logicalDpiY();
        style->closeIcon = iconFor(QStringLiteral("close"), theme_);
        style->stickyIcon = iconFor(vertical() ? QStringLiteral("sticky-vertical") : QStringLiteral("sticky"), theme_);
        style->addIcon = iconFor(QStringLiteral("add"), theme_);
        style->showNumbers = appearance_.showNumbers; style->showIcons = appearance_.showIcons; style->colors = appearance_.colors;
        labelStyle_ = std::move(style);
    }
    return labelStyle_;
}

std::shared_ptr<const TabLabelContent> TabStripController::labelContent(int index) const
{
    return tabs_.content(index, labelStyle());
}

const TabColorPreset* TabStripController::tabPreset(int index) const
{
    if (index < 0 || index >= tabs_.size()) return nullptr;
    const auto content = labelContent(index);
    return content->preset ? &*content->preset : nullptr;
}

int TabStripController::indexOf(TabId tab) const
{
    return tabs_.indexOf(tab);
}

void TabStripController::setCurrentIndex(int index)
{
    if (index < -1 || index >= tabs_.size() || index == current_) return;
    current_ = index;
    const auto timestamp = timestampNs_;
    (void)surfaceMotion_.select(currentLabelId(), isVisible() && animationsEnabled(), timestamp);
    // The current row may change while collapsed.
    relayout(false);
    changes_.current = index;
}

void TabStripController::setTheme(const Theme& theme)
{
    theme_ = theme;
    metrics_ = TabMetrics::forTheme(theme, font());
    painter_ = TabPainter(metrics_, TabPalette::forTheme(theme));
    updateTitleWidths();
    relayout(false);
}

void TabStripController::setAppearance(const TabAppearance& appearance, TabBarPlacement placement)
{
    const bool arrangement = appearance_.multiline != appearance.multiline || appearance_.rowExpansion != appearance.rowExpansion
        || appearance_.showAllRows != appearance.showAllRows || appearance_.stickyMultiline != appearance.stickyMultiline
        || appearance_.stickyShowAllRows != appearance.stickyShowAllRows
        || placement_ != placement;
    appearance_ = appearance; placement_ = placement;
    if (arrangement) {
        // A new arrangement starts collapsed (when it collapses) and at its start.
        rowsState_.resetArrangement(); wheelController_.reset(); rowsOffset_ = 0;
    }
    if (!vertical()) columnPeek_.setIconsOnly(false);
    updateTitleWidths();
    relayout(false);
    changes_.reservedSize = true;
}

void TabStripController::setStickyCount(int count)
{
    count = std::clamp(count, 0, int(tabs_.size()));
    if (stickyCount_ == count) return;
    stickyCount_ = count;
    relayout(true);
    changes_.reservedSize = true;
}

void TabStripController::setFolders(QVector<TabStripFolder> folders)
{
    if (folders_ == folders) return;
    const auto dragged = draggedFolder_ >= 0 && draggedFolder_ < folders_.size() ? folders_.at(draggedFolder_).id : QUuid{};
    folders_ = std::move(folders);
    draggedFolder_ = -1;
    for (int folder = 0; folder < folders_.size(); ++folder) if (folders_.at(folder).id == dragged) draggedFolder_ = folder;
    hover_.clearFolder(); pointer_.cancelFolders();
    relayout(true);
    changes_.reservedSize = true;
}

int TabStripController::folderOf(int index) const
{
    for (int folder = 0; folder < folders_.size(); ++folder)
        if (index >= folders_.at(folder).first && index < folders_.at(folder).first + folders_.at(folder).count) return folder;
    return -1;
}

QRect TabStripController::folderHeaderRect(int folder) const
{
    const auto visible = layout_.visibleHeaderRect(folder);
    return visible.isEmpty() ? QRect{} : visible.translated(tabOrigin());
}

int TabStripController::folderAt(QPoint local) const { return layout_.headerAt(local - tabOrigin()); }

const TabColorPreset* TabStripController::folderPreset(int folder) const
{
    if (folder < 0 || folder >= folders_.size() || appearance_.colors.isEmpty()) return nullptr;
    const auto& colors = appearance_.colors;
    const auto explicitIndex = [&](int at) {
        for (int index = 0; index < colors.size(); ++index) if (colors.at(index).id == folders_.at(at).colorId) return index;
        return -1;
    };
    if (const int index = explicitIndex(folder); index >= 0) return &colors.at(index);
    // Automatic: from the folder's identity, so it stays the same, past the
    // colors that other folders of this strip already show.
    QVector<bool> taken(colors.size(), false);
    for (int other = 0; other < folders_.size(); ++other) if (other != folder && explicitIndex(other) >= 0) taken[explicitIndex(other)] = true;
    int chosen = -1;
    for (int at = 0; at <= folder; ++at) {
        if (explicitIndex(at) >= 0) continue;
        int index = int(qHash(folders_.at(at).id) % uint(colors.size()));
        for (int step = 0; step < colors.size() && taken.at(index); ++step) index = (index + 1) % int(colors.size());
        taken[index] = true; chosen = index;
    }
    return &colors.at(std::max(0, chosen));
}

int TabStripController::folderHeaderExtent(const TabStripFolder& folder) const
{
    if (vertical()) return 0;
    const auto& m = metrics_;
    const auto count = QString::number(folder.count);
    if (folder.name.isEmpty() && !folder.collapsed) return m.folderDot + 2 * m.folderChipMargin + 4;
    const auto label = folder.collapsed ? (folder.name.isEmpty() ? count : folder.name + QStringLiteral("  ") + count) : folder.name;
    // A pixel of slack: elision compares fractional advances.
    const int text = std::min(fontMetrics().horizontalAdvance(label) + 2, uiPixels(appearance_.maximumTitleWidth));
    return text + 2 * m.folderChipPadding + 2 * m.folderChipMargin;
}

void TabStripController::setGroupFocused(bool focused)
{
    if (groupFocused_ == focused) return;
    groupFocused_ = focused; invalidateCarrier(true); requestRepaint();
}

void TabStripController::refreshTrailing()
{
    // Only the controls were remeasured. Their room changes the rows, but
    // the tabs are as they were: a wheel scroll in progress continues.
    rowMeasure_.reset();
    relayout(false, false);
    changes_.reservedSize = true;
}

void TabStripController::setCarriedDisplay(bool carried)
{
    if (carriedDisplay_ == carried) return;
    carriedDisplay_ = carried;
    invalidateCarrier(true);
    if (!carried) { setCarriedTileColumns(0); setCarriedPresentation({}); }
    if (!carried) { carrierContent_.reset(); carrierFrame_.reset(); carrierBuffers_ = {}; }
    requestRepaint();
}

void TabStripController::setCarriedPresentation(std::optional<VerticalPresentation> presentation)
{
    if (carriedPresentation_ == presentation) return;
    carriedPresentation_ = presentation; relayout(false);
}

void TabStripController::setCarriedTileColumns(int columns)
{
    if (carriedTileColumns_ == columns) return;
    carriedTileColumns_ = columns;
    relayout(false);
}



QRect TabStripController::carrierBounds() const
{
    if (!carrierBounds_) {
        QRect bounds;
        carrierTabs_.clear();
        for (int index = 0; index < count(); ++index) if (const auto rect = tabRect(index); !rect.isEmpty()) {
            bounds |= rect; carrierTabs_.append(index);
        }
        carrierBounds_ = bounds;
    }
    return *carrierBounds_;
}

void TabStripController::invalidateCarrier(bool content)
{
    invalidatePresentation();
    const bool observed = bool(carrierContent_) || bool(carrierFrame_);
    carrierContentDirty_ = carrierContentDirty_ || content;
    carrierFrame_.reset();
    if (observed && carriedDisplay_) changes_.carrier = true;
}

std::shared_ptr<const TabCarrierFrame> TabStripController::carrierFrame()
{
    if (carrierFrame_) return carrierFrame_;
    (void)carrierBounds();
    if (!carrierContent_ || carrierContentDirty_) {
        auto content = std::make_shared<TabCarrierLabels>();
        content->metrics = metrics_; content->palette = painter_.palette();
        content->labels.reserve(carrierTabs_.size());
        for (const auto index : std::as_const(carrierTabs_)) {
            auto state = paintState(index);
            state.selected = index == current_; state.lifted = false;
            state.elevationManaged = true;
            content->labels.append({tabData(index).toUuid(), TabPainter::captureLabel({}, state, font(), layoutDirection())});
        }
        carrierContent_ = std::move(content); carrierContentDirty_ = false;
    }
    // Reuse two GUI-side buffers when their last published frame is no longer
    // retained. A consumer's const frame is never changed under it.
    auto available = std::find_if(carrierBuffers_.begin(), carrierBuffers_.end(),
        [](const auto& buffer) { return !buffer || buffer.use_count() == 1; });
    if (available == carrierBuffers_.end()) available = carrierBuffers_.begin();
    if (!*available || available->use_count() != 1) *available = std::make_shared<TabCarrierFrame>();
    auto& frame = **available;
    frame.content = carrierContent_; frame.samples.resize(carrierTabs_.size());
    for (int at = 0; at < carrierTabs_.size(); ++at) {
        const int index = carrierTabs_.at(at);
        auto& sample = frame.samples[at];
        sample.parts = partsOf(index, animatedRect(index));
        const auto& state = carrierContent_->labels.at(at).label.state;
        sample.titleOpacity = vertical() && state.shape == TabShape::Row && !state.iconOnly ? columnReveal_ : 1;
        sample.actionOpacity = state.sticky || index == pointer_.pressedAction() ? 1 : actionMotion_.opacity();
    }
    carrierFrame_ = *available;
    return carrierFrame_;
}

QPoint TabStripController::carrierAnchor() const
{
    (void)carrierBounds();
    return carrierTabs_.isEmpty() ? QPoint{} : tabRect(carrierTabs_.first()).center();
}

TabPaintState TabStripController::paintState(int index)
{
    const auto content = labelContent(index);
    TabPaintState state;
    state.shape = shapeOf(index);
    state.selected = index == current_ && incoming_.visual().empty();
    state.hovered = !carriedDisplay_ && index == hover_.hit().tab;
    state.pressed = !carriedDisplay_ && index == pointer_.pressedTab();
    state.groupFocused = carriedDisplay_ || groupFocused_;
    state.windowActive = environment_.windowActive;
    state.lifted = carriedDisplay_;
    state.sticky = index < stickyCount_;
    state.compactSticky = state.shape == TabShape::Tile;
    state.iconOnly = (vertical() && iconsOnly() && columnReveal_ <= 0) || (!vertical() && isCompactSticky(index));
    state.titleOpacity = vertical() && state.shape == TabShape::Row && !state.iconOnly ? columnReveal_ : 1.0;
    state.outerRight = placement_ == TabBarPlacement::Right;
    state.preset = content->preset ? &*content->preset : nullptr;
    state.number = content->number;
    state.icon = content->icon;
    state.title = content->title;
    state.titleLayout = &tabs_.textLayout(index);
    // A sticky tab always shows its action; the others on hover.
    state.actionShown = hasLabelAction(index) && (state.sticky || (!carriedDisplay_ && (index == pointer_.pressedAction()
        || (index == actionMotion_.index() && actionMotion_.opacity() > 0))));
    state.actionHovered = !carriedDisplay_ && index == hover_.hit().tab && hover_.hit().action;
    state.actionPressed = !carriedDisplay_ && index == pointer_.pressedAction();
    state.actionReserved = state.sticky;
    state.actionOpacity = state.sticky || index == pointer_.pressedAction() ? 1.0 : actionMotion_.opacity();
    state.actionIcon = state.sticky ? content->style->stickyIcon : content->style->closeIcon;
    return state;
}

TabFolderPaintState TabStripController::folderPaintState(int folder) const
{
    const auto& record = folders_.at(folder);
    TabFolderPaintState state;
    state.name = record.name; state.count = record.count; state.collapsed = record.collapsed;
    state.hovered = folder == hover_.hit().folder; state.pressed = folder == pointer_.pressedFolder();
    state.addHovered = state.hovered && hover_.hit().folderAdd;
    if (const auto* preset = folderPreset(folder)) {
        state.color = preset->groupActive.line;
        state.wash = preset->groupInactive.background; state.washText = preset->groupInactive.text;
    } else { state.color = painter_.palette().accent; state.wash = painter_.palette().hover; state.washText = painter_.palette().text; }
    state.addIcon = labelStyle()->addIcon;
    return state;
}

TabStripController::TabStripController(const Theme& theme, TabStripEnvironment environment)
    : environment_(std::move(environment)), theme_(theme),
      metrics_(TabMetrics::forTheme(theme, font())), painter_(metrics_, TabPalette::forTheme(theme))
{
    relayout(false);
}

void TabStripController::setTimestampNs(qint64 timestampNs)
{
    timestampNs_ = std::max(timestampNs_, timestampNs);
}

void TabStripController::setEnvironment(TabStripEnvironment next)
{
    const auto previousSize = environment_.size;
    const bool measure = environment_.font != next.font || environment_.direction != next.direction
        || environment_.logicalDpiX != next.logicalDpiX || environment_.logicalDpiY != next.logicalDpiY;
    const auto& a = environment_.controls;
    const auto& b = next.controls;
    const bool controls = a.trailingWidth != b.trailingWidth || a.endWidth != b.endWidth
        || a.trailingPresent != b.trailingPresent || a.endPresent != b.endPresent
        || a.countPresent != b.countPresent || a.countSize != b.countSize || a.countShown != b.countShown;
    const bool presentation = environment_.globalOrigin != next.globalOrigin || environment_.windowArea != next.windowArea
        || environment_.windowActive != next.windowActive || environment_.visible != next.visible;
    environment_ = std::move(next);
    if (!environment_.visible || !environment_.smoothScrolling || !environment_.animations) {
        scrollMotion_.cancel(); smoothLane_ = -1;
    }
    if (measure) { refreshStyle(); return; }
    if (controls || previousSize != size()) {
        if (!controls && !vertical() && rowTransitionRunning() && previousSize.width() == width()) {
            relayoutTrailing(); surfaceFrameChanged();
        } else relayout(vertical() && columnModeTransition_ && stickyCount_ > 1
            && appearance_.stickySizing == StickyTabSizing::Compact);
    } else if (presentation) { invalidateCarrier(true); requestRepaint(); }
}

void TabStripController::refreshStyle()
{
    metrics_ = TabMetrics::forTheme(theme_, font());
    painter_ = TabPainter(metrics_, TabPalette::forTheme(theme_));
    updateTitleWidths(); relayout(false);
    changes_.reservedSize = true;
}

bool TabStripController::needsFrame() const
{
    return surfaceMotion_.needsFrame() || presence_.needsFrame() || rowsState_.needsFrame()
        || actionMotion_.needsFrame() || scrollbars_.needsFrame() || scrollMotion_.needsFrame()
        || positions_.needsFrame();
}

std::optional<qint64> TabStripController::nextWakeupNs() const
{
    std::optional<qint64> result;
    for (const auto deadline : {collapseDeadlineNs_, peekDeadlineNs_, scrollbarHideDeadlineNs_})
        if (deadline && (!result || *deadline < *result)) result = deadline;
    return result;
}

void TabStripController::advance(qint64 timestampNs)
{
    setTimestampNs(timestampNs);
    if (collapseDeadlineNs_ && timestampNs_ >= *collapseDeadlineNs_) {
        collapseDeadlineNs_.reset();
        if (rowsState_.collapseOnLeave()) setExpanded(false);
    }
    if (peekDeadlineNs_ && timestampNs_ >= *peekDeadlineNs_) {
        peekDeadlineNs_.reset(); applyColumnPeekAction(columnPeek_.timeout());
    }
    if (scrollbarHideDeadlineNs_ && timestampNs_ >= *scrollbarHideDeadlineNs_) {
        scrollbarHideDeadlineNs_.reset();
        if (!scrollbars_.dragging()) showScrollbar(false);
    }
    const bool actionRunning = actionMotion_.needsFrame();
    const bool actionChanged = actionMotion_.advance(timestampNs_);
    const bool actionFinished = actionRunning && !actionMotion_.needsFrame();
    if (actionFinished) actionMotion_.finish();
    if (actionChanged || actionFinished) invalidateCarrier(actionFinished);
    if (actionChanged) requestRepaint();
    const bool presenceRunning = presence_.needsFrame();
    const bool presenceChanged = presence_.advance(timestampNs_);
    const bool presenceFinished = presenceRunning && !presence_.needsFrame();
    if (presenceFinished) { departing_.clear(); presence_.finish(); }
    if (presenceChanged || presenceFinished) surfaceFrameChanged();
    const bool rowsRunning = rowsState_.needsFrame();
    const bool rowsChanged = rowsState_.advance(timestampNs_);
    const bool rowsFinished = rowsRunning && !rowsState_.needsFrame();
    if (rowsFinished) rowsState_.finish();
    if (rowsChanged || rowsFinished) {
        changes_.reservedSize = true; invalidateCarrier(false); relayoutTrailing(); requestRepaint();
    }
    if (surfaceMotion_.advance(timestampNs_)) surfaceFrameChanged();
    if (scrollbars_.advance(timestampNs_)) requestRepaint();
    if (scrollMotion_.advance(timestampNs_) && smoothLane_ >= 0 && smoothLane_ < layout_.laneList.size()) {
        const int lane = smoothLane_;
        applyingScrollMotion_ = true; setLaneOffset(lane, qRound(scrollMotion_.value())); applyingScrollMotion_ = false;
        wheelController_.observeRowScroll(layout_, lane, rowsState_.expanded(), placement_ == TabBarPlacement::Bottom,
            timestampNs_ / 1'000'000);
    }
    const bool positionsRunning = positions_.needsFrame();
    positions_.advance(timestampNs_);
    if (positionsRunning || (planarReflow_ && (presence_.needsFrame() || presenceFinished))) {
        if (!positions_.needsFrame() && (!planarReflow_ || !presence_.needsFrame())) {
            positions_.clear(); planarReflow_ = false;
        }
        syncAttachment(); surfaceFrameChanged();
    }
}

void TabStripController::requestRepaint()
{
    invalidatePresentation(); changes_.repaint = true;
}

void TabStripController::invalidatePresentation()
{
    presentationFrame_.reset(); changes_.presentation = true;
}

void TabStripController::refreshPresentationLayers()
{
    invalidatePresentation();
}

std::shared_ptr<const TabPresentationFrame> TabStripController::presentationFrame()
{
    if (presentationFrame_) return presentationFrame_;
    std::shared_ptr<TabPresentationFrame> next;
    for (auto& buffer : presentationBuffers_) if (!buffer || buffer.use_count() == 1) {
        if (!buffer) buffer = std::make_shared<TabPresentationFrame>();
        next = buffer; break;
    }
    if (!next) next = std::make_shared<TabPresentationFrame>();
    next->base.reset(); next->foreground.reset(); next->size = size();
    next->base = captureStripFrame(font(), layoutDirection());
    next->foreground = captureForegroundFrame(font(), layoutDirection());
    presentationFrame_ = std::move(next);
    return presentationFrame_;
}

} // namespace window_system
