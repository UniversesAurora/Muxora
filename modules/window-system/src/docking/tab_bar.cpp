#include <window_system/interface_appearance.hpp>
// SPDX-License-Identifier: MIT
#include "tab_bar.hpp"
#include "tab_keyboard_controller.hpp"
#include "style/compact_tool_button.hpp"
#include "style/tab_appearance_p.hpp"
#include "style/edge_fade.hpp"

#include <QAccessible>
#include <QAccessibleWidget>
#include <QApplication>
#include <QCoreApplication>
#include <QContextMenuEvent>
#include <QEasingCurve>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMenu>
#include <QPainter>
#include <QToolButton>
#include <QToolTip>
#include <QWheelEvent>
#include <algorithm>

namespace window_system {
namespace {

bool animationsSwitch = true;

// Rows of a column or an icon column are shaped as rows; the rest are tabs.
TabShape horizontalShape(TabBarPlacement placement) { return placement == TabBarPlacement::Bottom ? TabShape::Bottom : TabShape::Top; }

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

// Screen readers see the strip as a page tab list of its tabs.
class TabAccessible final : public QAccessibleInterface {
public:
    TabAccessible(TabBar* bar, QVariant id) : bar_(bar), id_(std::move(id)) {}
    [[nodiscard]] bool isValid() const override { return bar_ && index() >= 0; }
    [[nodiscard]] QObject* object() const override { return nullptr; }
    [[nodiscard]] QWindow* window() const override { return bar_ ? bar_->window()->windowHandle() : nullptr; }
    [[nodiscard]] QAccessibleInterface* parent() const override { return bar_ ? QAccessible::queryAccessibleInterface(bar_) : nullptr; }
    [[nodiscard]] QAccessibleInterface* child(int) const override { return nullptr; }
    [[nodiscard]] int childCount() const override { return 0; }
    [[nodiscard]] int indexOfChild(const QAccessibleInterface*) const override { return -1; }
    [[nodiscard]] QAccessibleInterface* childAt(int, int) const override { return nullptr; }
    [[nodiscard]] QString text(QAccessible::Text type) const override
    {
        const int at = index();
        if (at < 0) return {};
        if (type == QAccessible::Name) return bar_->tabText(at);
        if (type == QAccessible::Description) return bar_->tabToolTip(at);
        return {};
    }
    void setText(QAccessible::Text, const QString&) override {}
    [[nodiscard]] QRect rect() const override
    {
        const int at = index();
        if (at < 0) return {};
        const auto area = bar_->tabRect(at);
        return QRect(bar_->mapToGlobal(area.topLeft()), area.size());
    }
    [[nodiscard]] QAccessible::Role role() const override { return QAccessible::PageTab; }
    [[nodiscard]] QAccessible::State state() const override
    {
        QAccessible::State result;
        const int at = index();
        if (at < 0) { result.invalid = true; return result; }
        result.selectable = true; result.selected = at == bar_->currentIndex();
        result.focusable = true; result.focused = result.selected && bar_->hasFocus();
        result.invisible = bar_->tabRect(at).isEmpty();
        return result;
    }
private:
    [[nodiscard]] int index() const
    {
        if (!bar_) return -1;
        for (int at = 0; at < bar_->count(); ++at) if (bar_->tabData(at) == id_) return at;
        return -1;
    }
    QPointer<TabBar> bar_;
    QVariant id_;
};

// A tab folder's header: a button that expands or collapses the folder.
class TabFolderAccessible final : public QAccessibleInterface, public QAccessibleActionInterface {
public:
    TabFolderAccessible(TabBar* bar, QUuid id) : bar_(bar), id_(id) {}
    [[nodiscard]] bool isValid() const override { return bar_ && index() >= 0; }
    [[nodiscard]] QObject* object() const override { return nullptr; }
    [[nodiscard]] QWindow* window() const override { return bar_ ? bar_->window()->windowHandle() : nullptr; }
    [[nodiscard]] QAccessibleInterface* parent() const override { return bar_ ? QAccessible::queryAccessibleInterface(bar_) : nullptr; }
    [[nodiscard]] QAccessibleInterface* child(int) const override { return nullptr; }
    [[nodiscard]] int childCount() const override { return 0; }
    [[nodiscard]] int indexOfChild(const QAccessibleInterface*) const override { return -1; }
    [[nodiscard]] QAccessibleInterface* childAt(int, int) const override { return nullptr; }
    [[nodiscard]] QString text(QAccessible::Text type) const override
    {
        const int at = index();
        if (at < 0 || type != QAccessible::Name) return {};
        const auto& folder = bar_->folders().at(at);
        const auto count = QCoreApplication::translate("WindowSystem", "%n tab(s)", nullptr, folder.count);
        return folder.name.isEmpty() ? QCoreApplication::translate("WindowSystem", "Tab folder, %1").arg(count)
                                     : QCoreApplication::translate("WindowSystem", "Tab folder %1, %2").arg(folder.name, count);
    }
    void setText(QAccessible::Text, const QString&) override {}
    [[nodiscard]] QRect rect() const override
    {
        const int at = index();
        if (at < 0) return {};
        const auto area = bar_->folderHeaderRect(at);
        return QRect(bar_->mapToGlobal(area.topLeft()), area.size());
    }
    [[nodiscard]] QAccessible::Role role() const override { return QAccessible::Button; }
    [[nodiscard]] QAccessible::State state() const override
    {
        QAccessible::State result;
        const int at = index();
        if (at < 0) { result.invalid = true; return result; }
        result.expandable = true; result.expanded = !bar_->folders().at(at).collapsed;
        result.collapsed = !result.expanded; result.invisible = bar_->folderHeaderRect(at).isEmpty();
        return result;
    }
    void* interface_cast(QAccessible::InterfaceType type) override
    {
        return type == QAccessible::ActionInterface ? static_cast<QAccessibleActionInterface*>(this) : nullptr;
    }
    [[nodiscard]] QStringList actionNames() const override { return {pressAction()}; }
    void doAction(const QString& name) override
    {
        const int at = index();
        if (at >= 0 && name == pressAction()) if (const auto callback = bar_->toggleFolder) callback(at);
    }
    [[nodiscard]] QStringList keyBindingsForAction(const QString&) const override { return {}; }
private:
    [[nodiscard]] int index() const
    {
        if (!bar_) return -1;
        for (int at = 0; at < bar_->folders().size(); ++at) if (bar_->folders().at(at).id == id_) return at;
        return -1;
    }
    QPointer<TabBar> bar_;
    QUuid id_;
};

class TabBarAccessible final : public QAccessibleWidget {
public:
    explicit TabBarAccessible(TabBar* bar) : QAccessibleWidget(bar, QAccessible::PageTabList) {}
    // The tabs, then the folder headers.
    [[nodiscard]] int childCount() const override { return bar()->count() + int(bar()->folders().size()); }
    [[nodiscard]] QAccessibleInterface* child(int index) const override
    {
        if (index < 0 || index >= childCount()) return nullptr;
        const bool folder = index >= bar()->count();
        const auto key = folder ? bar()->folders().at(index - bar()->count()).id : bar()->tabData(index).toUuid();
        auto& cached = children_[key];
        if (!cached || !QAccessible::accessibleInterface(cached)) {
            QAccessibleInterface* created = folder ? static_cast<QAccessibleInterface*>(new TabFolderAccessible(bar(), key))
                                                   : new TabAccessible(bar(), key);
            cached = QAccessible::registerAccessibleInterface(created);
        }
        return QAccessible::accessibleInterface(cached);
    }
    [[nodiscard]] int indexOfChild(const QAccessibleInterface* child) const override
    {
        for (int index = 0; index < childCount(); ++index) if (this->child(index) == child) return index;
        return -1;
    }
    [[nodiscard]] QAccessibleInterface* childAt(int x, int y) const override
    {
        const auto local = bar()->mapFromGlobal(QPoint(x, y));
        const int index = bar()->tabAt(local);
        if (index >= 0) return child(index);
        const int folder = bar()->folderAt(local);
        return folder >= 0 ? child(bar()->count() + folder) : nullptr;
    }
    ~TabBarAccessible() override
    {
        for (const auto id : std::as_const(children_)) if (QAccessible::accessibleInterface(id)) QAccessible::deleteAccessibleInterface(id);
    }
private:
    [[nodiscard]] TabBar* bar() const { return static_cast<TabBar*>(widget()); }
    mutable QHash<QUuid, QAccessible::Id> children_;
};

QAccessibleInterface* accessibleFactory(const QString& className, QObject* object)
{
    if (className == QLatin1StringView("window_system::TabBar") && object && object->isWidgetType())
        return new TabBarAccessible(static_cast<TabBar*>(object));
    return nullptr;
}

} // namespace

void TabBar::setAnimationsEnabled(bool enabled) { animationsSwitch = enabled; }

bool TabBar::animationsEnabled()
{
    return animationsSwitch && motionAnimationsEnabled();
}

TabBar::TabBar(const Theme& theme, QWidget* parent)
    : QWidget(parent), theme_(theme), metrics_(TabMetrics::forTheme(theme, font())),
      painter_(metrics_, TabPalette::forTheme(theme))
{
    static const bool factory = [] { QAccessible::installFactory(accessibleFactory); return true; }();
    Q_UNUSED(factory);
    setObjectName("TabBar");
    rowWheelClock_.start();
    setMouseTracking(true);
    setFocusPolicy(Qt::TabFocus);
    setAttribute(Qt::WA_Hover);
    countButton_ = new CompactToolButton(this, QSize(metrics_.countMinimumWidth, metrics_.rowHeight));
    countButton_->setObjectName("TabRowCountButton");
    countButton_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    countButton_->hide();
    connect(countButton_, &QToolButton::clicked, this, [this] { setExpanded(!rowsState_.expanded()); });
    collapseTimer_.setSingleShot(true); collapseTimer_.setInterval(TabRowsState::collapseDelayMs);
    connect(&collapseTimer_, &QTimer::timeout, this, [this] { if (rowsState_.collapseOnLeave()) setExpanded(false); });
    peekTimer_.setSingleShot(true); peekTimer_.setInterval(TabColumnPeek::delayMs);
    connect(&peekTimer_, &QTimer::timeout, this, [this] { applyColumnPeekAction(columnPeek_.timeout()); });
    slideTimer_.setInterval(15);
    connect(&slideTimer_, &QTimer::timeout, this, [this] {
        positions_.advance(rowWheelClock_.nsecsElapsed());
        if (!positions_.needsFrame() && (!planarReflow_ || presenceProgress_.state() != QAbstractAnimation::Running)) {
            positions_.clear(); planarReflow_ = false; slideTimer_.stop();
        }
        syncAttachment(); surfaceFrameChanged();
    });
    connect(&actionOpacity_, &QVariantAnimation::valueChanged, this, [this] { invalidateCarrier(false); update(); });
    connect(&actionOpacity_, &QVariantAnimation::finished, this, [this] {
        if (actionOpacity_.value() == 0) fadingAction_ = -1;
        invalidateCarrier(true);
    });
    connect(&presenceProgress_, &QVariantAnimation::valueChanged, this, [this] { surfaceFrameChanged(); });
    connect(&presenceProgress_, &QVariantAnimation::finished, this, [this] {
        departing_.clear();
        presence_.finish();
        surfaceFrameChanged();
    });
    rowsDriver_.advance = [this](qint64 timestampNs) {
        if (!rowsState_.advance(timestampNs)) return;
        invalidateCarrier(false);
        const QPointer<TabBar> self(this);
        if (const auto callback = reservedSizeChanged) callback();
        if (!self) return;
        relayoutTrailing(); refreshScrollInput(); update();
    };
    connect(&rowsDriver_, &QAbstractAnimation::finished, this, [this] {
        rowsState_.finish();
        if (!rowsState_.open()) qApp->removeEventFilter(this);
        const QPointer<TabBar> self(this);
        if (const auto callback = reservedSizeChanged) callback();
        if (!self) return;
        invalidateCarrier(false); update();
    });
    const auto surfaceFrame = [this] { surfaceFrameChanged(); };
    connect(&attachment_, &QVariantAnimation::valueChanged, this, surfaceFrame);
    connect(&selection_, &QVariantAnimation::valueChanged, this, surfaceFrame);
    connect(&liftElevation_, &QVariantAnimation::valueChanged, this, surfaceFrame);
    scrollbarHideTimer_.setSingleShot(true); scrollbarHideTimer_.setInterval(700);
    scrollMotion_.applyValue = [this](qreal value) {
        if (smoothLane_ < 0 || smoothLane_ >= layout_.laneList.size()) return;
        const int lane = smoothLane_;
        if (!isSmoothScrollingEnabled(this) || !animationsEnabled()) {
            scrollMotion_.stopAt(layout_.laneList.at(lane).offset); return;
        }
        const QPointer<TabBar> self(this);
        applyingScrollMotion_ = true;
        setLaneOffset(lane, qRound(value));
        if (!self) return;
        applyingScrollMotion_ = false;
        wheelController_.observeRowScroll(layout_, lane, rowsState_.expanded(), placement_ == TabBarPlacement::Bottom, rowWheelClock_.elapsed());
    };
    connect(&scrollbarOpacity_, &QVariantAnimation::valueChanged, this, [this] { update(); refreshScrollInput(); });
    connect(&scrollbarHideTimer_, &QTimer::timeout, this, [this] {
        if (scrollDragLane_ < 0) scrollbarOpacity_.animateTo(0, Motion::Presence, animationsEnabled());
    });
}

TabBar::~TabBar() { qApp->removeEventFilter(this); delete scrollInput_.data(); delete liftedLayer_.data(); }

int TabBar::addTab(const QIcon& icon, const QString& title)
{
    tabs_.append(icon, title);
    if (current_ < 0) current_ = 0;
    relayout(true);
    return int(tabs_.size()) - 1;
}

void TabBar::removeTab(int index)
{
    if (index < 0 || index >= tabs_.size()) return;
    const auto id = tabs_.at(index).data.toUuid();
    presence_.remove(id); departing_.remove(id);
    const bool wasCurrent = index == current_;
    tabs_.removeAt(index);
    if (index < stickyCount_) --stickyCount_;
    if (dragged_ == index) dragged_ = -1; else if (dragged_ > index) --dragged_;
    hover_.removedTab(index);
    if (fadingAction_ == index) fadingAction_ = -1; else if (fadingAction_ > index) --fadingAction_;
    pointer_.cancelTabs();
    const int previous = current_;
    if (tabs_.isEmpty()) current_ = -1;
    else if (index < current_) --current_;
    else if (wasCurrent) current_ = std::min(index, int(tabs_.size()) - 1);
    relayout(true);
    if (wasCurrent || previous != current_) emit currentChanged(current_);
}

QVariant TabBar::tabData(int index) const { return tabs_.data(index); }
void TabBar::setTabData(int index, const QVariant& value)
{
    if (tabs_.setData(index, value)) invalidateCarrier(true);
}
QString TabBar::tabText(int index) const { return tabs_.title(index); }
void TabBar::setTabText(int index, const QString& text)
{
    if (tabs_.setTitle(index, text)) relayout(true);
}
QIcon TabBar::tabIcon(int index) const { return tabs_.icon(index); }
void TabBar::setTabIcon(int index, const QIcon& icon)
{
    if (tabs_.setIcon(index, icon)) relayout(true);
}
QString TabBar::tabToolTip(int index) const { return tabs_.toolTip(index); }
void TabBar::setTabToolTip(int index, const QString& tip) { tabs_.setToolTip(index, tip); }
void TabBar::setTabMetadata(int index, const TabRecord& tab)
{
    if (!tabs_.setColor(index, tab.colorId)) return;
    invalidateCarrier(true); update();
}
std::shared_ptr<const TabStripLabelStyle> TabBar::labelStyle() const
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

std::shared_ptr<const TabLabelContent> TabBar::labelContent(int index) const
{
    return tabs_.content(index, labelStyle());
}

const TabColorPreset* TabBar::tabPreset(int index) const
{
    if (index < 0 || index >= tabs_.size()) return nullptr;
    const auto content = labelContent(index);
    return content->preset ? &*content->preset : nullptr;
}

int TabBar::indexOf(TabId tab) const
{
    return tabs_.indexOf(tab);
}

void TabBar::setCurrentIndex(int index)
{
    if (index < -1 || index >= tabs_.size() || index == current_) return;
    current_ = index;
    selectionStart_ = 0;
    selection_.jump(0);
    selection_.animateTo(1, Motion::SurfaceChangeLift, isVisible() && animationsEnabled());
    // The current row may change while collapsed.
    relayout(false);
    emit currentChanged(index);
}

qreal TabBar::selectionWeight(int index) const
{
    if (index < 0 || index != current_ || index >= count() || !incoming_.visual().empty()) return 0;
    return selectionStart_ + (1 - selectionStart_) * selection_.value();
}

std::pair<qreal, qreal> TabBar::attachmentValues() const
{
    const auto progress = attachment_.value();
    return {attachmentFrom_.first + (attachmentTo_.first - attachmentFrom_.first) * progress,
            attachmentFrom_.second + (attachmentTo_.second - attachmentFrom_.second) * progress};
}

SurfaceGeometry TabBar::anchorGeometry(QRectF anchor, QRectF visible, bool compact, bool lifted, bool transitioning) const
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

QRect TabBar::activeDragRect() const
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

void TabBar::syncAttachment()
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
    if (!attachmentInitialized_ || attachmentOwner_ != owner) {
        attachmentInitialized_ = true; attachmentOwner_ = owner;
        attachmentFrom_ = attachmentTo_ = target; attachment_.jump(1);
        return;
    }
    if (attachmentTo_ == target) return;
    attachmentFrom_ = attachmentValues(); attachmentTo_ = target;
    const auto distance = std::max(std::abs(attachmentTo_.first - attachmentFrom_.first),
                                   std::abs(attachmentTo_.second - attachmentFrom_.second));
    attachment_.jump(0);
    attachment_.animateTo(1, target.first > attachmentFrom_.first ? Motion::SurfaceChangeLift : Motion::SurfaceChangeLower,
                          isVisible() && animationsEnabled(), distance);
}

void TabBar::surfaceFrameChanged()
{
    invalidateCarrier(false);
    update(); updateLiftedLayer();
    if (const auto callback = surfaceChanged) callback();
}

TabBar::SurfaceState TabBar::captureSurfaceState() const
{
    SurfaceState state{{}, tabData(current_).toUuid(), liftElevation_.value(), !incoming_.visual().empty()};
    for (int index = 0; index < count(); ++index) {
        const auto weight = selectionWeight(index);
        if (weight > 0) state.selection.insert(tabData(index).toUuid(), weight);
    }
    return state;
}

void TabBar::inheritSurface(const SurfaceState& previous, bool animate)
{
    selectionStart_ = previous.incoming ? 1 : previous.selection.value(tabData(current_).toUuid());
    selection_.jump(0);
    selection_.animateTo(1, Motion::SurfaceChangeLift, animate, 1 - selectionStart_);
    const bool sameTab = previous.current == tabData(current_).toUuid();
    liftElevation_.jump(sameTab || previous.incoming ? previous.lift : 0);
    liftElevation_.animateTo(0, Motion::SurfaceLower, animate);
    surfaceFrameChanged();
}

void TabBar::setSurfaceContext(QRectF body, qreal focus, qreal join)
{
    if (surfaceBody_ == body && surfaceFocus_ == focus && surfaceJoin_ == join) return;
    surfaceBody_ = body; surfaceFocus_ = focus; surfaceJoin_ = join;
    syncAttachment(); surfaceFrameChanged();
}

LabelPresencePose TabBar::labelPresence(int index) const
{
    if (index < 0 || index >= count()) return {};
    return presenceFor(index, animatedRect(index));
}

LabelPresencePose TabBar::presenceFor(int index, QRect area) const
{
    if (index < 0 || index >= count()) return {};
    return presence_.pose(tabs_.at(index).data.toUuid(), area, presenceProgress_.value());
}

QVector<TabSurfaceAnchor> TabBar::surfaceAnchors(QPointF offset) const
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
                elevationProfile(ElevationLevel::Lifted), liftElevation_.value()), true,
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
        if (index == current_ && liftElevation_.value() > 0)
            profile = interpolateElevation(profile, elevationProfile(ElevationLevel::Lifted), liftElevation_.value());
        anchors.append({translatedSurfaceGeometry(geometry, offset), profile, liftElevation_.value() > 0 || settling,
            overlayActive() || vertical() && surfaceJoin_ < 1, presence.translated(offset)});
    }
    return anchors;
}

void TabBar::paintSurfaceExtension(QPainter& painter, QPointF offset) const
{
    if (surfaceBody_.isEmpty() || current_ < 0 || current_ >= count()) return;
    const bool moving = !incoming_.visual().empty() || (lifted_.active() && (dragged_ == current_ || draggedFolder_ >= 0));
    const QRect area = moving ? activeDragRect() : animatedRect(current_);
    const bool compact = moving ? dropSticky_ && compactStickyTiles() : shapeOf(current_) == TabShape::Tile;
    const auto geometry = translatedSurfaceGeometry(anchorGeometry(area, area.intersected(rect()), compact, moving), offset);
    if (!geometry.joined) return;
    const auto presence = moving ? LabelPresencePose{} : presenceFor(current_, area).translated(offset);
    if (presence.opacity <= 0) return;
    QPainterPath widget; widget.addRect(QRectF(rect()).translated(offset));
    painter.save(); painter.setRenderHint(QPainter::Antialiasing);
    painter.setOpacity(painter.opacity() * presence.opacity);
    QTransform transform; transform.translate(presence.offset.x(), presence.offset.y()); transform.scale(presence.scale, presence.scale);
    painter.fillPath(transform.map(geometry.anchorPath).subtracted(widget), theme_.surface);
    painter.restore();
}

void TabBar::setTheme(const Theme& theme)
{
    theme_ = theme;
    metrics_ = TabMetrics::forTheme(theme, font());
    painter_ = TabPainter(metrics_, TabPalette::forTheme(theme));
    updateTitleWidths();
    relayout(false);
}

void TabBar::setAppearance(const TabAppearance& appearance, TabBarPlacement placement)
{
    const bool arrangement = appearance_.multiline != appearance.multiline || appearance_.rowExpansion != appearance.rowExpansion
        || appearance_.showAllRows != appearance.showAllRows || appearance_.stickyMultiline != appearance.stickyMultiline
        || appearance_.stickyShowAllRows != appearance.stickyShowAllRows
        || placement_ != placement;
    appearance_ = appearance; placement_ = placement;
    if (arrangement) {
        // A new arrangement starts collapsed (when it collapses) and at its start.
        rowsDriver_.stop(); rowsState_.resetArrangement(); wheelController_.reset(); rowsOffset_ = 0;
    }
    if (!vertical()) columnPeek_.setIconsOnly(false);
    updateTitleWidths();
    relayout(false);
    if (const auto callback = reservedSizeChanged) callback();
}

void TabBar::setStickyCount(int count)
{
    count = std::clamp(count, 0, int(tabs_.size()));
    if (stickyCount_ == count) return;
    stickyCount_ = count;
    relayout(true);
    if (const auto callback = reservedSizeChanged) callback();
}

void TabBar::setFolders(QVector<TabStripFolder> folders)
{
    if (folders_ == folders) return;
    const auto dragged = draggedFolder_ >= 0 && draggedFolder_ < folders_.size() ? folders_.at(draggedFolder_).id : QUuid{};
    folders_ = std::move(folders);
    draggedFolder_ = -1;
    for (int folder = 0; folder < folders_.size(); ++folder) if (folders_.at(folder).id == dragged) draggedFolder_ = folder;
    hover_.clearFolder(); pointer_.cancelFolders();
    relayout(true);
    if (const auto callback = reservedSizeChanged) callback();
}
int TabBar::folderOf(int index) const
{
    for (int folder = 0; folder < folders_.size(); ++folder)
        if (index >= folders_.at(folder).first && index < folders_.at(folder).first + folders_.at(folder).count) return folder;
    return -1;
}
QRect TabBar::folderHeaderRect(int folder) const
{
    const auto visible = layout_.visibleHeaderRect(folder);
    return visible.isEmpty() ? QRect{} : visible.translated(tabOrigin());
}
int TabBar::folderAt(QPoint local) const { return layout_.headerAt(local - tabOrigin()); }
const TabColorPreset* TabBar::folderPreset(int folder) const
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
int TabBar::folderHeaderExtent(const TabStripFolder& folder) const
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
void TabBar::setGroupFocused(bool focused)
{
    if (groupFocused_ == focused) return;
    groupFocused_ = focused; invalidateCarrier(true); update();
}

void TabBar::setIconMode(bool iconMode)
{
    if (!vertical()) iconMode = false;
    if (!columnPeek_.setIconsOnly(iconMode)) return;
    const QPointer<TabBar> self(this);
    relayout(true);
    if (!self) return;
    if (columnPeek_.iconsOnly()) QTimer::singleShot(0, this, [this] { reconcileColumnHover(); });
    if (const auto callback = reservedSizeChanged) callback();
}

void TabBar::setColumnReveal(qreal reveal, bool modeTransition)
{
    if (columnReveal_ == reveal && columnModeTransition_ == modeTransition) return;
    const bool iconOnlyChanged = (columnReveal_ > 0) != (reveal > 0);
    columnReveal_ = reveal; columnModeTransition_ = modeTransition;
    invalidateCarrier(iconOnlyChanged); update();
}

void TabBar::applyColumnPeekAction(const TabColumnPeekAction& action)
{
    using Timer = TabColumnPeekAction::Timer;
    if (action.timer == Timer::Start) peekTimer_.start();
    else if (action.timer == Timer::Stop) peekTimer_.stop();
    if (!action.relayout) return;
    const QPointer<TabBar> self(this);
    relayout(action.animate);
    if (self) if (const auto callback = reservedSizeChanged) callback();
}

void TabBar::setColumnHovered(bool hovered)
{
    applyColumnPeekAction(columnPeek_.hover(hovered, peekTimer_.isActive(), scrollDragLane_ >= 0));
}

void TabBar::restoreColumnPeek(bool peeked)
{
    const auto action = columnPeek_.restore(peeked);
    const QPointer<TabBar> self(this);
    applyColumnPeekAction(action);
    if (self && action.relayout) QTimer::singleShot(0, this, [this] { trackColumnPopup(); });
}

void TabBar::showColumnPopup(std::function<void()> open)
{
    const QPointer<TabBar> self(this);
    columnPeek_.setPopupOpen(true); peekTimer_.stop();
    open();
    if (self) QTimer::singleShot(0, this, [this] { trackColumnPopup(); });
}

void TabBar::trackColumnPopup()
{
    if (columnPopup_ && columnPopup_->isVisible()) return;
    disconnect(popupHidden_); disconnect(popupDestroyed_);
    auto* popup = qobject_cast<QMenu*>(QApplication::activePopupWidget());
    if (popup && popup->isVisible()) {
        columnPeek_.setPopupOpen(true); columnPopup_ = popup;
        const auto closed = [this] { QTimer::singleShot(0, this, [this] { trackColumnPopup(); }); };
        popupHidden_ = connect(popup, &QMenu::aboutToHide, this, closed);
        popupDestroyed_ = connect(popup, &QObject::destroyed, this, closed);
        return;
    }
    columnPopup_.clear(); columnPeek_.setPopupOpen(false); reconcileColumnHover();
}

void TabBar::reconcileColumnHover()
{
    auto* owner = columnManaged_ ? parentWidget() : this;
    setColumnHovered(isVisible() && owner && owner->isVisible() && owner->rect().contains(owner->mapFromGlobal(QCursor::pos())));
}

TabVisualState TabBar::captureVisualState()
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
            retired->visibility = presence_.capturedVisibility(tabData(index).toUuid(), presenceProgress_.value());
        }
        result.insert(tabData(index).toUuid(), {rect.translated(globalOrigin), shapeOf(index),
            index < stickyCount_, index == current_, iconsOnly() || shapeOf(index) == TabShape::Tile, parts, std::move(retired), hasSingleTileRow()});
    }
    // Flat exits belong to this strip's single clock. Capture their current
    // width too, so reversing a pin operation does not restart from full size.
    for (auto it = departing_.cbegin(); it != departing_.cend(); ++it) if (it->collapse && it->retired) {
        auto visible = *it;
        visible.global = planarLabelRect(it->global, *it->collapse, 1 - presenceProgress_.value());
        if (!visible.global.isEmpty()) result.insert(it.key(), std::move(visible));
    }
    return result;
}

void TabBar::animatePresence(const QSet<QUuid>& ids, bool visible, Motion motion)
{
    presence_.retarget(ids, visible, presenceProgress_.value());
    presenceProgress_.jump(0);
    presenceProgress_.animateTo(1, motion, animationsEnabled());
    if (!animationsEnabled()) { departing_.clear(); if (visible) for (const auto& id : ids) presence_.remove(id); }
    surfaceFrameChanged();
}

bool TabBar::hasSingleTileRow() const
{
    return compactStickyTiles() && stickyCount_ > 0 && layout_.tileColumns >= stickyCount_;
}

bool TabBar::usesPlanarReflow(const TabVisualState& previous) const
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

TabVisualState TabBar::takeSeparateDepartures(TabVisualState& previous) const
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

void TabBar::transitionFrom(TabVisualState previous, Motion motion)
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
    if (!positions_.isEmpty()) { positions_.start(rowWheelClock_.nsecsElapsed()); slideTimer_.start(); }
    if (planarReflow_ || !appeared.isEmpty() || !departing_.isEmpty()) animatePresence(appeared, true, motion);
    presence_.setPlanar(planarEntries);
}

LabelPositions TabBar::captureDropPositions(std::optional<LabelPositions> carrierPositions) const
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

void TabBar::settleFrom(const LabelPositions& globalPositions)
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
    if (positions_.settling().isEmpty()) return;
    planarReflow_ = false;
    positions_.setMotion(Motion::SurfaceLower);
    positions_.start(rowWheelClock_.nsecsElapsed()); slideTimer_.start();
    surfaceFrameChanged();
}

void TabBar::setTrailingWidget(QWidget* widget)
{
    if (trailing_ && trailing_ != widget) trailing_->removeEventFilter(this);
    trailing_ = widget;
    if (widget) { widget->setParent(this); widget->installEventFilter(this); widget->show(); }
    relayout(false);
}

void TabBar::setEndWidget(QWidget* widget)
{
    if (end_ && end_ != widget) end_->removeEventFilter(this);
    end_ = widget;
    if (widget) { widget->setParent(this); widget->installEventFilter(this); widget->show(); }
    relayout(false);
}

void TabBar::refreshTrailing()
{
    relayout(false);
    if (const auto callback = reservedSizeChanged) callback();
}

bool TabBar::isCompactSticky(int index) const
{
    return index >= 0 && index < stickyCount_ && appearance_.stickySizing == StickyTabSizing::Compact;
}

bool TabBar::rowShape() const { return vertical(); }

TabLabelParts TabBar::labelParts(int index, bool sticky) const
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

int TabBar::tabExtent(int index, bool sticky) const
{
    if (index < 0 || index >= tabs_.size()) return 0;
    if (sticky && appearance_.stickySizing == StickyTabSizing::Compact) return uiPixels(compactStickyTabWidth);
    // A sticky tab's action always shows and keeps its room.
    return tabBarTabWidth(labelParts(index, sticky), sticky);
}

int TabBar::carriedExtent(const CarriedLabel& label, bool sticky) const
{
    if (label.folderHeader) return folderHeaderExtent({{}, label.title, {}, label.collapsed, 0, label.count});
    if (sticky && appearance_.stickySizing == StickyTabSizing::Compact) return uiPixels(compactStickyTabWidth);
    TabTitleLayout title;
    title.prepare(label.title, font(), layoutDirection(), true, this);
    TabLabelParts parts;
    parts.icon = appearance_.showIcons && label.icon ? metrics_.iconSize : 0;
    const int natural = title.naturalWidth();
    if (sticky && appearance_.stickySizing == StickyTabSizing::Shrink) parts.title = std::min(natural, uiPixels(appearance_.shrinkStickyTitleWidth));
    else parts.title = appearance_.clipTitles ? std::min(natural, uiPixels(appearance_.maximumTitleWidth)) : natural;
    // The number it gets is unknown until it lands; one digit approximates it.
    parts.number = appearance_.showNumbers ? fontMetrics().horizontalAdvance(QStringLiteral("0")) : 0;
    return tabBarTabWidth(parts, sticky);
}

int TabBar::naturalTabWidth(int index) const { return index >= 0 && index < tabs_.size() ? tabBarTabWidth(labelParts(index, false)) : 0; }

void TabBar::updateTitleWidths()
{
    labelStyle_.reset();
    tabs_.measureTitles(font(), layoutDirection(), this, true);
}

bool TabBar::wrappedRows() const
{
    return !vertical() && rowPolicy().wrapped();
}

bool TabBar::collapsible() const
{
    return !vertical() && rowPolicy().collapsible();
}

bool TabBar::collapsedNow() const { return !vertical() && rowPolicy().collapsed(); }

bool TabBar::overlayActive() const
{
    if (vertical()) return columnPeek_.iconsOnly() && columnPeek_.expanded();
    return rowPolicy().overlay();
}

TabStripRows TabBar::rowPolicy() const
{
    return {appearance_.multiline, appearance_.showAllRows, appearance_.stickyMultiline,
        appearance_.stickyShowAllRows, appearance_.stickyTabsOnSeparateRow, stickyCount_ > 0,
        rowsState_.expanded(), rowsState_.dragExpanded(), rowsState_.temporaryOverlay(), appearance_.rowExpansion, rowsState_.value()};
}

TabStripGeometry TabBar::stripGeometry() const
{
    return {placement_, rowPolicy(), metrics_.outerMargin, metrics_.shadowBlur, metrics_.rowHeight,
        columnPeek_.iconsOnly(), metrics_.verticalIconWidth, minimumColumnWidth(), maximumVerticalTabWidth};
}

TabStripControls TabBar::stripControls() const
{
    return {trailing_ && !trailing_->isHidden() ? trailing_->sizeHint().width() : 0,
        end_ && !end_->isHidden() ? end_->sizeHint().width() : 0,
        trailing_ && !trailing_->isHidden(), end_ && !end_->isHidden(),
        countButton_ ? countButton_->size() : QSize{}, metrics_.countMinimumWidth, uiPixels(14),
        countButton_ != nullptr, countButton_ && !countButton_->isHidden(), isVisible()};
}

TabStripArrangementInput TabBar::arrangementInput() const
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

QPoint TabBar::tabOrigin() const
{
    // Keep this hot query independent of label measurement and control hints.
    return tabStripOrigin(placement_, metrics_.outerMargin, vertical() ? 0 : rowShadowExtent());
}

int TabBar::minimumColumnWidth() const
{
    // Keep the icon clear of the action even when no title fits. Ordinals
    // and titles can disappear before either of these hit targets does.
    return std::max(uiPixels(minimumVerticalTabWidth), 2 * metrics_.verticalInset + uiPixels(tabBarLeading)
        + metrics_.iconSize + uiPixels(tabActionGap) + uiPixels(tabActionSize) + uiPixels(tabBarTrailing));
}

int TabBar::rowShadowExtent() const
{
    return overlayActive() ? qRound(metrics_.shadowBlur * rowsState_.value()) : 0;
}

const TabStripRowMeasure& TabBar::measureRows(QSize available) const
{
    if (!rowMeasure_ || rowMeasure_->available != available) rowMeasure_ = measureTabStripRows(arrangementInput(), available);
    return *rowMeasure_;
}

QSize TabBar::reservedSize(QSize available) const
{
    const auto geometry = stripGeometry();
    return tabStripReservedSize(geometry, available, vertical() ? TabStripRowMeasure{} : measureRows(available));
}

QSize TabBar::displaySize(QSize available) const
{
    const auto geometry = stripGeometry();
    if (vertical()) return tabStripReservedSize(geometry, available);
    std::optional<TabLayoutInput> preview;
    if (!geometry.rows.overlay() && dropIndex_ >= 0)
        preview = tabStripLayoutInput(arrangementInput(), QSize(available.width(), 0), collapsedNow());
    return tabStripDisplaySize(geometry, available, measureRows(available), preview ? &*preview : nullptr);
}

void TabBar::setAvailableSize(QSize available)
{
    if (available_ == available) return;
    available_ = available;
    relayout(false);
}

void TabBar::relayout(bool animate, bool measureChanged)
{
    invalidateCarrier(true);
    if (measureChanged) { scrollMotion_.stop(); smoothLane_ = -1; }
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
    tabs_.measureTitles(font(), layoutDirection(), this);
    auto arranged = arrangeTabStrip(arrangementInput(), size());
    arrangedInput_ = std::move(arranged.input); layout_ = std::move(arranged.layout);
    if (countButton_) {
        countButton_->setVisible(arranged.controls.countShown);
        if (arranged.countMeasured) {
            countButton_->setText(arranged.countText);
            if (countButton_->size() != arranged.controls.countSize)
                static_cast<CompactToolButton*>(countButton_)->setTargetSize(arranged.controls.countSize);
            countButton_->setToolTip(collapsedNow()
                ? QCoreApplication::translate("WindowSystem", "%n more tab(s) in other rows; click to expand", nullptr, layout_.hidden)
                : QCoreApplication::translate("WindowSystem", "Collapse other rows"));
            countButton_->setAccessibleName(countButton_->toolTip());
        }
    }
    stickyOffset_ = arranged.stickyOffset; regularOffset_ = arranged.regularOffset; rowsOffset_ = arranged.rowsOffset;
    collapsedStickyRow_ = arranged.collapsedStickyRow; collapsedRegularRow_ = arranged.collapsedRegularRow;
    applyControlRects(arranged.controlRects);
    carrierBounds_.reset();
    updateLiftedLayer();
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
        if (!positions_.isEmpty()) { positions_.start(rowWheelClock_.nsecsElapsed()); slideTimer_.start(); }
    }
    syncAttachment();
    surfaceFrameChanged();
    emit viewChanged();
    refreshScrollInput();
}

void TabBar::relayoutTrailing()
{
    if (vertical() && countButton_) countButton_->hide();
    applyControlRects(tabStripControlRects(stripGeometry(), layout_, stripControls()));
}

void TabBar::applyControlRects(const TabStripControlRects& controls)
{
    if (!controls.trailing.isNull()) { trailing_->setGeometry(controls.trailing); trailing_->raise(); }
    if (!controls.end.isNull()) { end_->setGeometry(controls.end); end_->raise(); }
    if (!controls.count.isNull()) { countButton_->move(controls.count.topLeft()); countButton_->raise(); }
}

QRect TabBar::tabRect(int index) const
{
    if (index < 0 || index >= layout_.tabs.size()) return {};
    const auto visible = layout_.visibleRect(index).intersected(rect().translated(-tabOrigin()));
    return visible.isEmpty() ? QRect{} : visible.translated(tabOrigin());
}

void TabBar::setCarriedDisplay(bool carried)
{
    if (carriedDisplay_ == carried) return;
    carriedDisplay_ = carried;
    invalidateCarrier(true);
    if (!carried) { setCarriedTileColumns(0); setCarriedPresentation({}); }
    if (!carried) { carrierContent_.reset(); carrierFrame_.reset(); carrierBuffers_ = {}; }
    update();
}

void TabBar::setCarriedPresentation(std::optional<VerticalPresentation> presentation)
{
    if (carriedPresentation_ == presentation) return;
    carriedPresentation_ = presentation; relayout(false);
}

void TabBar::setCarriedTileColumns(int columns)
{
    if (carriedTileColumns_ == columns) return;
    carriedTileColumns_ = columns;
    relayout(false);
}

QRegion TabBar::tabRegion(int index) const
{
    if (index < 0 || index >= layout_.tabs.size() || layout_.lanes.at(index) < 0) return {};
    const auto contour = painter_.outline(QRectF(layout_.tabs.at(index).translated(tabOrigin())), shapeOf(index), carriedDisplay_ || index == current_, carriedDisplay_);
    return QRegion(contour.toFillPolygon().toPolygon()).intersected(tabRect(index));
}

int TabBar::tabAt(QPoint local) const { return layout_.tabAt(local - tabOrigin()); }

TabShape TabBar::shapeOf(int index) const
{
    if (!vertical()) return horizontalShape(placement_);
    const bool tiles = compactStickyTiles();
    return index < stickyCount_ && tiles ? TabShape::Tile : TabShape::Row;
}

bool TabBar::hasLabelAction(int index) const
{
    if (index < 0 || index >= tabs_.size()) return false;
    if (!vertical()) return !isCompactSticky(index);
    // Do not expose a destructive action until the expanded column has
    // actually finished opening. Painting, hit testing and tips share this.
    if (iconsOnly() || columnReveal_ < 1 || shapeOf(index) == TabShape::Tile) return false;
    return index >= stickyCount_ || appearance_.stickySizing != StickyTabSizing::Compact;
}

TabParts TabBar::partsOf(int index, const QRect& rect) const
{
    const bool sticky = index < stickyCount_;
    const bool iconOnly = (vertical() && iconsOnly() && columnReveal_ <= 0) || (!vertical() && isCompactSticky(index));
    const bool action = hasLabelAction(index);
    return labelGeometry(rect, shapeOf(index), labelParts(index, sticky), iconOnly, action, sticky);
}

TabParts TabBar::labelGeometry(const QRect& rect, TabShape shape, TabLabelParts label,
                              bool iconOnly, bool action, bool sticky) const
{
    if (vertical() && shape == TabShape::Row)
        return layoutColumnTabParts(rect, label, iconOnly, action, sticky, placement_ == TabBarPlacement::Right,
            {width(), metrics_.verticalIconWidth, metrics_.iconSize, metrics_.rowHeight});
    return layoutTabParts(rect, shape, label, iconOnly, action, sticky, layoutDirection());
}

QRect TabBar::titleRect(int index) const
{
    const auto rect = tabRect(index);
    return rect.isEmpty() ? QRect{} : partsOf(index, layout_.tabs.at(index).translated(tabOrigin())).title.intersected(rect);
}

QRect TabBar::actionRect(int index) const
{
    const auto rect = tabRect(index);
    return rect.isEmpty() ? QRect{} : partsOf(index, layout_.tabs.at(index).translated(tabOrigin())).action.intersected(rect);
}

QRect TabBar::animatedRect(int index) const
{
    const auto target = layout_.tabs.at(index).translated(tabOrigin());
    if (positions_.isEmpty()) return target;
    const bool rearrangingTiles = columnModeTransition_ && stickyCount_ > 1 && index < stickyCount_
        && appearance_.stickySizing == StickyTabSizing::Compact;
    return positions_.rect(tabs_.at(index).data.toUuid(), target,
        {vertical() && columnManaged_, columnModeTransition_, rearrangingTiles},
        planarReflow_ ? std::optional(presenceProgress_.value()) : std::nullopt);
}

int TabBar::scrollOffset() const
{
    return regularOffset_;
}

void TabBar::restoreScrollOffsets(int regular, int sticky, int rows)
{
    scrollMotion_.stop(); smoothLane_ = -1;
    regular = std::max(0, regular); sticky = std::max(0, sticky); rows = std::max(0, rows);
    if (regularOffset_ == regular && stickyOffset_ == sticky && rowsOffset_ == rows) return;
    regularOffset_ = regular; stickyOffset_ = sticky; rowsOffset_ = rows;
    relayout(false, false);
}

void TabBar::setScrollOffset(int offset)
{
    for (int lane = 0; lane < layout_.laneList.size(); ++lane)
        if (!layout_.laneList.at(lane).sticky && !layout_.laneList.at(lane).vertical) { setLaneOffset(lane, offset); return; }
}

int TabBar::maximumScrollOffset() const
{
    for (const auto& lane : layout_.laneList) if (!lane.sticky) return lane.maximumOffset();
    return 0;
}

QRect TabBar::scrollViewportAt(QPoint local) const
{
    const int index = laneAt(local);
    return index < 0 ? QRect{} : layout_.laneList.at(index).viewport.translated(tabOrigin());
}
bool TabBar::scrollsHorizontallyAt(QPoint local) const
{
    const int index = laneAt(local);
    return index >= 0 && !layout_.laneList.at(index).vertical;
}

QRect TabBar::scrollViewport() const
{
    for (const auto& lane : layout_.laneList) if (!lane.sticky) return lane.viewport.translated(tabOrigin());
    return {};
}

int TabBar::frozenExtent() const
{
    for (const auto& lane : layout_.laneList) if (!lane.sticky) return lane.frozen;
    return 0;
}

QRect TabBar::contentTabRect(int index) const
{
    if (index < 0 || index >= layout_.tabs.size() || layout_.lanes.at(index) < 0) return {};
    const auto& lane = layout_.laneList.at(layout_.lanes.at(index));
    auto rect = layout_.tabs.at(index).translated(tabOrigin());
    // Frozen sticky tabs do not scroll; the others are shifted back.
    if (lane.frozen && index < stickyCount_) return rect.translated(lane.offset, 0);
    return lane.vertical ? rect.translated(0, lane.offset) : rect.translated(lane.offset, 0);
}

bool TabBar::scrollsHorizontally() const { return !vertical() && !wrappedRows(); }

bool TabBar::isOverflowing() const
{
    for (const auto& lane : layout_.laneList) if (lane.maximumOffset() > 0) return true;
    return false;
}

int TabBar::laneAt(QPoint local) const
{
    return TabWheelController::laneAt(layout_, local - tabOrigin(), vertical());
}

void TabBar::setLaneOffset(int lane, int offset)
{
    if (!applyingScrollMotion_) { scrollMotion_.stop(); smoothLane_ = -1; }
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
    updateLiftedLayer();
    syncAttachment();
    const QPointer<TabBar> self(this);
    surfaceFrameChanged();
    if (!self) return;
    emit viewChanged();
    if (!self) return;
    refreshScrollInput();
}

bool TabBar::scrollLaneBy(int lane, int delta, bool smooth)
{
    const auto info = layout_.laneList.at(lane);
    if (!smooth || !animationsEnabled() || !isSmoothScrollingEnabled(this)) {
        const bool moved = std::clamp(info.offset + delta, 0, info.maximumOffset()) != info.offset;
        setLaneOffset(lane, info.offset + delta); return moved;
    }
    if (smoothLane_ != lane) scrollMotion_.stop();
    smoothLane_ = lane;
    scrollMotion_.scrollBy(delta, info.offset, 0, info.maximumOffset());
    return qRound(scrollMotion_.target()) != info.offset;
}

void TabBar::revealScrollbar(int lane)
{
    scrollingLane_ = lane;
    scrollbarOpacity_.animateTo(1, Motion::Presence, animationsEnabled());
    if (scrollDragLane_ < 0) scrollbarHideTimer_.start();
    else scrollbarHideTimer_.stop();
    refreshScrollInput();
}

QRect TabBar::scrollbarHitRect() const
{
    if (!isVisible() || scrollbarOpacity_.value() <= 0.05 || scrollingLane_ < 0 || scrollingLane_ >= layout_.laneList.size()) return {};
    const auto& lane = layout_.laneList.at(scrollingLane_);
    if (lane.maximumOffset() <= 0) return {};
    const auto track = scrollTrack(scrollingLane_);
    return (lane.vertical ? track.adjusted(-2, 0, 2, 0) : track.adjusted(0, -2, 0, 2)).intersected(rect());
}

void TabBar::refreshScrollInput()
{
    const auto hit = scrollbarHitRect();
    if (hit.isEmpty()) { if (scrollInput_ && scrollDragLane_ < 0) scrollInput_->hide(); return; }
    if (!scrollInput_) {
        scrollInput_ = new QWidget(window());
        scrollInput_->setObjectName("TabScrollbarInput"); scrollInput_->setMouseTracking(true);
        scrollInput_->setCursor(Qt::ArrowCursor); scrollInput_->setFocusPolicy(Qt::NoFocus);
        scrollInput_->installEventFilter(this);
    } else if (scrollInput_->parentWidget() != window()) scrollInput_->setParent(window());
    scrollInput_->setGeometry(QRect(mapTo(window(), hit.topLeft()), hit.size()));
    scrollInput_->show(); scrollInput_->raise();
}

bool TabBar::scrollAt(QPoint local, QPoint delta, bool smooth)
{
    if (delta.isNull()) return false;
    const TabWheelPolicy policy{
        .vertical = vertical(),
        .bottom = placement_ == TabBarPlacement::Bottom,
        .collapsible = collapsible(),
        .wheelExpandRows = appearance_.wheelExpandRows,
        .collapsed = collapsedNow(),
        .expanded = rowsState_.expanded()
    };
    const auto decision = wheelController_.route(layout_, local - tabOrigin(), delta, policy, rowWheelClock_.elapsed());
    using Action = TabWheelDecision::Action;
    if (decision.action == Action::Ignore) return false;
    const QPointer<TabBar> self(this);
    emit scrollInputStarted();
    if (!self) return true;
    if (decision.action == Action::Expand) { setExpanded(true, true); return true; }
    if (decision.action == Action::Collapse) { setExpanded(false); return true; }
    if (decision.action == Action::HoldAtBoundary) return true;
    const int lane = decision.lane;
    if (decision.horizontalPriority) {
        revealScrollbar(lane);
        scrollLaneBy(lane, decision.delta, smooth);
        return true;
    }
    // A listener of the change may destroy this strip.
    const bool moved = scrollLaneBy(lane, decision.delta, smooth);
    if (!self) return true;
    wheelController_.observeRowScroll(layout_, lane, rowsState_.expanded(), placement_ == TabBarPlacement::Bottom, rowWheelClock_.elapsed());
    return moved;
}

void TabBar::ensureVisible(int index)
{
    scrollMotion_.stop(); smoothLane_ = -1;
    if (index < 0 || index >= layout_.tabs.size() || layout_.lanes.at(index) < 0) return;
    const QPointer<TabBar> self(this);
    const int outerIndex = layout_.rowScrollLane;
    if (outerIndex >= 0 && outerIndex != layout_.lanes.at(index)) {
        const auto outer = layout_.laneList.at(outerIndex);
        setLaneOffset(outerIndex, outer.offsetToReveal(layout_.tabs.at(index), metrics_.edgeFade));
        if (!self || index >= layout_.tabs.size() || layout_.lanes.at(index) < 0) return;
    }
    const int laneIndex = layout_.lanes.at(index);
    const auto lane = layout_.laneList.at(laneIndex);
    if (lane.frozen && index < stickyCount_) return;
    setLaneOffset(laneIndex, lane.offsetToReveal(layout_.tabs.at(index), metrics_.edgeFade));
}

bool TabBar::fullyVisible(int index) const
{
    if (index < 0 || index >= layout_.tabs.size() || layout_.tabs.at(index).isEmpty() || layout_.lanes.at(index) < 0) return false;
    const auto& lane = layout_.laneList.at(layout_.lanes.at(index));
    const auto viewport = lane.frozen && index < stickyCount_ ? lane.viewport : lane.readableViewport(metrics_.edgeFade);
    if (!viewport.contains(layout_.tabs.at(index))) return false;
    return layout_.rowScrollLane < 0 || layout_.rowScrollLane == layout_.lanes.at(index)
        || layout_.laneList.at(layout_.rowScrollLane).readableViewport(metrics_.edgeFade).contains(layout_.tabs.at(index));
}

void TabBar::updateRowPresentation(TabVisualState previous, bool moving, bool revealActive)
{
    const QPointer<TabBar> self(this);
    const bool open = rowsState_.open();
    if (rowsState_.watchesOutsideInput(moving, appearance_.rowExpansion == TabRowExpansion::Overlay)) qApp->installEventFilter(this);
    else if (!open && !(moving && rowsState_.value() > 0)) qApp->removeEventFilter(this);
    const auto now = rowsDriver_.now(rowWheelClock_.nsecsElapsed());
    const bool restart = rowsState_.begin(moving, now);
    if (!moving || !rowsState_.needsFrame()) rowsDriver_.stop();
    else if (restart) rowsDriver_.restart(now, motionDuration(Motion::Reveal));
    if (!self) return;
    relayout(false);
    if (!self) return;
    if (open && revealActive) ensureVisible(current_);
    if (!self) return;
    if (const auto callback = reservedSizeChanged) callback();
    if (!self) return;
    if (moving) transitionFrom(std::move(previous), Motion::Reveal);
}

void TabBar::setExpanded(bool expanded, bool byWheel, bool animate)
{
    if (!collapsible()) expanded = false;
    collapseTimer_.stop();
    if (rowsState_.expanded() == expanded) {
        rowsState_.setExpanded(expanded, byWheel);
        if (!animate) { rowsDriver_.stop(); rowsState_.jumpToTarget(); }
        return;
    }
    const bool moving = animate && isVisible() && animationsEnabled();
    const bool activeWasVisible = fullyVisible(current_);
    auto before = moving ? captureVisualState() : TabVisualState{};
    rowsState_.setExpanded(expanded, byWheel);
    wheelController_.reset();
    updateRowPresentation(std::move(before), moving, activeWasVisible);
}

void TabBar::setDragExpanded(bool expanded)
{
    if (!collapsible()) expanded = false;
    if (rowsState_.dragExpanded() == expanded) return;
    const bool moving = isVisible() && animationsEnabled();
    const bool activeWasVisible = fullyVisible(current_);
    auto before = moving ? captureVisualState() : TabVisualState{};
    rowsState_.setDragExpanded(expanded, moving);
    updateRowPresentation(std::move(before), moving, activeWasVisible);
}

void TabBar::setDraggedTab(TabId tab)
{
    const int index = tab.isNull() ? -1 : indexOf(tab);
    if (index == dragged_) return;
    initialStickyBoundary_ = index >= 0 ? stickyBoundary() : std::nullopt;
    dragged_ = index;
    if (index < 0) { lifted_.clear(); liftElevation_.animateTo(0, Motion::SurfaceLower, animationsEnabled()); }
    relayout(true);
}

void TabBar::setDraggedFolder(QUuid folder)
{
    int index = -1;
    for (int at = 0; at < folders_.size(); ++at) if (folders_.at(at).id == folder) index = at;
    if (index == draggedFolder_) return;
    draggedFolder_ = index;
    if (index < 0) lifted_.clear();
    relayout(true);
}
void TabBar::setLifted(QPoint local, QPoint grab)
{
    setLiftedWithAnchor(local, {grab, {}});
}

void TabBar::setLiftedWithAnchor(QPoint local, const DragAnchor& grab)
{
    const auto change = lifted_.update(local.isNull() ? std::nullopt : std::optional(local), grab);
    if (change == TabLiftState::Change::None) return;
    if (change == TabLiftState::Change::Lift || change == TabLiftState::Change::Lower) {
        const bool lifting = change == TabLiftState::Change::Lift;
        liftElevation_.animateTo(lifting ? 1 : 0, lifting ? Motion::SurfaceLift : Motion::SurfaceLower, animationsEnabled());
    }
    syncAttachment(); surfaceFrameChanged();
}

void TabBar::updateLiftedLayer()
{
    // The logical strip still constrains the label. Its elevation must not
    // inherit a narrow column's resting mask or stop at the strip's edge.
    auto* owner = window();
    const bool shown = !positions_.settling().isEmpty() || (lifted_.active() && (dragged_ >= 0 || draggedFolder_ >= 0)) || !incoming_.visual().empty()
        || (current_ >= 0 && (liftElevation_.value() > 0 || overlayActive() || vertical() && surfaceJoin_ < 1));
    QRegion current;
    if (shown) {
        const auto origin = mapTo(owner, QPoint());
        const auto labelMargins = elevationMargins(elevationProfile(ElevationLevel::Lifted));
        const auto bodyMargins = elevationMargins(elevationProfile(ElevationLevel::BodyRaised));
        const QMarginsF margins(std::max(labelMargins.left(), bodyMargins.left()), std::max(labelMargins.top(), bodyMargins.top()),
            std::max(labelMargins.right(), bodyMargins.right()), std::max(labelMargins.bottom(), bodyMargins.bottom()));
        const auto include = [&](QRect area) {
            if (!area.isEmpty()) current |= QRectF(area.translated(origin)).marginsAdded(margins).toAlignedRect().adjusted(-1, -1, 1, 1);
        };
        include(liftedRect()); include(incomingRect());
        for (auto it = positions_.settling().cbegin(); it != positions_.settling().cend(); ++it)
            if (it.value() >= 0 && it.value() < count()) include(animatedRect(it.value()));
        for (const auto& anchor : surfaceAnchors(origin)) if (anchor.lifted || anchor.foreground)
            current |= anchor.presence.mapRect(anchor.geometry.anchorPath.boundingRect().marginsAdded(margins))
                .toAlignedRect().adjusted(-1, -1, 1, 1);
        current &= owner->rect();
    }
    if (!current.isEmpty() && !liftedLayer_) {
        // One sparse, transparent child per active strip, not per tab. The
        // window owns its stacking plane; the strip explicitly destroys it.
        liftedLayer_ = new QWidget(owner);
        liftedLayer_->setObjectName("LiftedTabLayer");
        liftedLayer_->setAttribute(Qt::WA_TransparentForMouseEvents);
        liftedLayer_->setAttribute(Qt::WA_NoSystemBackground);
        liftedLayer_->installEventFilter(this);
    }
    if (!liftedLayer_) return;
    if (liftedLayer_->parentWidget() != owner) {
        liftedLayer_->setParent(owner); liftedPaintRegion_ = {};
    }
    const auto dirty = liftedPaintRegion_ | current;
    if (current.isEmpty()) {
        liftedLayer_->hide();
    } else {
        liftedLayer_->setGeometry(owner->rect());
        // A full-window host must not turn each pointer update, mask change,
        // or final hide into a full-window repaint.
        if (current != liftedPaintRegion_) liftedLayer_->setMask(current);
        liftedLayer_->show(); liftedLayer_->raise();
        liftedLayer_->update(dirty);
    }
    if (!dirty.isEmpty()) owner->update(dirty);
    liftedPaintRegion_ = std::move(current);
}

void TabBar::refreshPresentationLayers()
{
    updateLiftedLayer();
    refreshScrollInput();
}

QRect TabBar::carrierBounds() const
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

void TabBar::invalidateCarrier(bool content)
{
    const bool observed = bool(carrierContent_) || bool(carrierFrame_);
    carrierContentDirty_ = carrierContentDirty_ || content;
    carrierFrame_.reset();
    if (observed && carriedDisplay_) emit carrierInvalidated();
}

std::shared_ptr<const TabCarrierFrame> TabBar::carrierFrame()
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
        sample.actionOpacity = state.sticky || index == pointer_.pressedAction() ? 1 : actionOpacity_.value();
    }
    carrierFrame_ = *available;
    return carrierFrame_;
}

QPoint TabBar::carrierAnchor() const
{
    (void)carrierBounds();
    return carrierTabs_.isEmpty() ? QPoint{} : tabRect(carrierTabs_.first()).center();
}

void TabBar::setIncomingVisual(TabDragVisual visual, QPoint local)
{
    const auto change = incoming_.replace(std::move(visual), local, layout_, dragArea(), incomingConstraint());
    if (change == TabIncomingState::Change::None) return;
    if (change == TabIncomingState::Change::Leave) incomingPainter_.clear();
    if (change == TabIncomingState::Change::Enter || change == TabIncomingState::Change::Leave) {
        const bool entering = change == TabIncomingState::Change::Enter;
        liftElevation_.animateTo(entering ? 1 : 0,
            entering ? Motion::SurfaceLift : Motion::SurfaceLower, animationsEnabled());
    }
    syncAttachment(); surfaceFrameChanged();
}

TabDragArea TabBar::dragArea() const
{
    return {size(), tabOrigin(), metrics_.rowHeight, metrics_.verticalInset};
}

QRect TabBar::incomingRect() const
{
    return incoming_.rect(layout_, dragArea(), incomingConstraint());
}

QRect TabBar::incomingRectAt(QPoint local) const
{
    return incoming_.rectAt(local, layout_, dragArea(), incomingConstraint());
}

TabDragConstraint TabBar::incomingConstraint() const
{
    return vertical() && dropSticky_ && compactStickyTiles() ? TabDragConstraint::CompactTile
        : vertical() ? TabDragConstraint::VerticalRow : TabDragConstraint::HorizontalRow;
}

QRect TabBar::liftedRect() const
{
    const auto target = liftTarget();
    return target ? lifted_.rect(layout_, dragArea(), *target) : QRect{};
}

QRect TabBar::liftedRectAt(QPoint local) const
{
    const auto target = liftTarget();
    return target ? lifted_.rectAt(local, layout_, dragArea(), *target) : QRect{};
}

std::optional<TabLiftTarget> TabBar::liftTarget() const
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

std::optional<int> TabBar::stickyBoundary() const
{
    if (stickyCount_ <= 0) return {};
    if (vertical() && !layout_.separator.isNull()) return layout_.separator.top() + tabOrigin().y();
    if (!vertical() && appearance_.stickyTabsOnSeparateRow)
        return layout_.sectionBoundary.value_or(metrics_.rowHeight) + tabOrigin().y();
    return {};
}

TabInsertion TabBar::insertionAt(QPoint local, int stickyReach, TabLayoutInput::Carried carried) const
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

void TabBar::setDropPreview(int index, bool sticky, QVector<int> block, int folder)
{
    if (index < 0) { block.clear(); sticky = false; index = -1; folder = -1; incoming_.resetReorderOffset(); }
    if (dropIndex_ == index && dropSticky_ == sticky && dropBlock_ == block && dropFolder_ == folder) return;
    dropIndex_ = index; dropSticky_ = sticky; dropBlock_ = std::move(block); dropFolder_ = folder;
    relayout(true);
    if (const auto callback = reservedSizeChanged) callback();
}

QRect TabBar::dropPreviewRect() const
{
    QRect result;
    for (const auto& cell : layout_.gap) if (!cell.isNull()) result |= cell.translated(tabOrigin());
    if (layout_.gapLane >= 0) result &= layout_.laneList.at(layout_.gapLane).viewport.translated(tabOrigin());
    return result;
}

QRect TabBar::scrollTrack(int laneIndex) const
{
    const auto& lane = layout_.laneList.at(laneIndex);
    const auto viewport = lane.viewport.translated(tabOrigin());
    const int thickness = metrics_.scrollbar;
    if (!lane.vertical)
        return QRect(viewport.left() + lane.frozen, viewport.bottom() - thickness + 1, viewport.width() - lane.frozen, thickness);
    // Vertical lanes put the bar on the content side.
    const bool left = placement_ == TabBarPlacement::Right;
    return QRect(left ? viewport.left() + 1 : viewport.right() - thickness, viewport.top(), thickness, viewport.height());
}

QRect TabBar::scrollThumb(int laneIndex) const
{
    const auto& lane = layout_.laneList.at(laneIndex);
    if (lane.maximumOffset() <= 0 || lane.extent <= 0) return {};
    const auto track = scrollTrack(laneIndex);
    const int length = lane.vertical ? track.height() : track.width();
    const int visible = lane.vertical ? lane.viewport.height() : lane.viewport.width() - lane.frozen;
    const int thumb = std::max(16, int(qint64(length) * visible / std::max(1, lane.extent - (lane.vertical ? 0 : lane.frozen))));
    const int position = int(qint64(length - thumb) * lane.offset / std::max(1, lane.maximumOffset()));
    return lane.vertical ? QRect(track.left(), track.top() + position, track.width(), thumb)
                         : QRect(track.left() + position, track.top(), thumb, track.height());
}

TabPaintState TabBar::paintState(int index)
{
    const auto content = labelContent(index);
    TabPaintState state;
    state.shape = shapeOf(index);
    state.selected = index == current_ && incoming_.visual().empty();
    state.hovered = !carriedDisplay_ && index == hover_.hit().tab;
    state.pressed = !carriedDisplay_ && index == pointer_.pressedTab();
    state.groupFocused = carriedDisplay_ || groupFocused_;
    state.windowActive = window()->isActiveWindow();
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
        || (index == fadingAction_ && actionOpacity_.value() > 0))));
    state.actionHovered = !carriedDisplay_ && index == hover_.hit().tab && hover_.hit().action;
    state.actionPressed = !carriedDisplay_ && index == pointer_.pressedAction();
    state.actionReserved = state.sticky;
    state.actionOpacity = state.sticky || index == pointer_.pressedAction() ? 1.0 : actionOpacity_.value();
    state.actionIcon = state.sticky ? content->style->stickyIcon : content->style->closeIcon;
    return state;
}


TabFolderPaintState TabBar::folderPaintState(int folder) const
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
void TabBar::paintEvent(QPaintEvent*)
{
    QPainter painter(this); painter.setFont(font());
    const auto frame = captureStripFrame(painter.font(), painter.layoutDirection());
    if (!frame->background.isEmpty()) painter.fillRect(frame->background, frame->style->palette.strip);
    for (const auto& area : frame->separators) painter_.paintSeparator(painter, area);
    const auto paintLabel = [&](QPainter& target, const TabStripLabelSample& sample) {
        if (target.hasClipping() && !target.clipBoundingRect().intersects(
            QRectF(sample.label.parts.tab).adjusted(-metrics_.controlRadius, 0, metrics_.controlRadius, 0))) return;
        painter_.paintLabel(target, sample.label, &tabs_.textLayout(sample.sourceIndex));
    };
    for (const auto& lane : frame->lanes) {
        const auto drawLane = [&](QPainter& target) {
            target.save(); target.setClipRect(lane.viewport, Qt::IntersectClip);
            target.save(); target.setClipRect(lane.regularClip, Qt::IntersectClip);
            for (const auto& folder : lane.folders) painter_.paintFolderHeader(target, folder.area, frame->vertical, folder.state);
            for (const auto& label : lane.regular) paintLabel(target, label);
            target.restore();
            for (const auto& label : lane.frozen) paintLabel(target, label);
            target.save(); target.setClipRect(lane.regularClip, Qt::IntersectClip);
            for (const auto& line : lane.folderLines) painter_.paintFolderLine(target, line.area, line.color);
            if (lane.folderOutline) {
                const auto& outline = *lane.folderOutline;
                painter_.paintFolderOutline(target, outline.area, outline.shape, outline.color);
            }
            target.restore(); target.restore();
        };
        paintEdgeFades(painter, lane.viewport, lane.fades, drawLane);
    }
    for (const auto& scrollbar : frame->scrollbars) {
        painter.save(); painter.setOpacity(painter.opacity() * scrollbar.opacity);
        painter_.paintScrollbar(painter, scrollbar.thumb, scrollbar.emphasized); painter.restore();
    }
    if (!frame->sectionSeparator.isEmpty())
        painter_.paintSeparator(painter, frame->sectionSeparator, frame->windowActive, frame->vertical);
    departurePainter_.paint(painter, painter_, frame->departures);
    if (!frame->overlayShadow.isEmpty()) surfaceRenderer_.paintOverlayBand(painter, frame->overlayShadow,
        frame->shadowAbove, frame->style->palette.elevation, frame->shadowOpacity);
}

void TabBar::paintLiftedTabs(QPainter& painter)
{
    painter.setFont(font());
    const auto frame = captureForegroundFrame(painter.font(), painter.layoutDirection());
    for (const auto& shadow : frame->shadows) LabelPresenceEffect::paint(painter, shadow.presence, [&] {
        surfaceRenderer_.paintAnchor(painter, shadow.geometry, shadow.body, shadow.anchor, frame->elevation, shadow.incremental);
    });
    if (frame->incoming) {
        const auto& incoming = *frame->incoming;
        painter.save(); painter.translate(incoming.origin);
        incomingPainter_.paint(painter, incoming.visual, &incoming.geometry); painter.restore();
    }
    if (frame->folder) painter_.paintFolderHeader(painter, frame->folder->area, frame->vertical, frame->folder->state);
    for (const auto& sample : frame->carried) painter_.paintLabel(painter, sample.label, &tabs_.textLayout(sample.sourceIndex));
    if (frame->folderLine) painter_.paintFolderLine(painter, frame->folderLine->area, frame->folderLine->color);
    for (const auto& sample : frame->settling) {
        if (painter.hasClipping() && !painter.clipBoundingRect().intersects(QRectF(sample.label.parts.tab)
            .adjusted(-metrics_.controlRadius, 0, metrics_.controlRadius, 0))) continue;
        painter_.paintLabel(painter, sample.label, &tabs_.textLayout(sample.sourceIndex));
    }
}

qreal TabBar::liftProgress() const { return liftElevation_.value(); }

void TabBar::inheritHover(const TabBar& previous)
{
    if (!previous.hover_.hasSession() || !rect().contains(mapFromGlobal(QCursor::pos()))) return;
    hover_.beginSession();
    updateHover(mapFromGlobal(QCursor::pos()));
    if (hover_.hit().tab >= 0) { fadingAction_ = hover_.hit().tab; actionOpacity_.jump(1); update(); }
}

void TabBar::updateHover(QPoint local)
{
    TabHoverHit hit;
    hit.tab = tabAt(local);
    hit.action = hit.tab >= 0 && actionRect(hit.tab).contains(local)
        && (hit.tab < stickyCount_ ? !isCompactSticky(hit.tab) : true);
    hit.lane = layout_.laneList.isEmpty() || !rect().contains(local) ? -1 : laneAt(local);
    hit.folder = rect().contains(local) ? folderAt(local) : -1;
    hit.folderAdd = hit.folder >= 0 && vertical() && painter_.folderAddRect(folderHeaderRect(hit.folder)).contains(local);
    const auto transition = hover_.move(hit);
    using Fade = TabHoverTransition::Fade;
    if (transition.fade == Fade::In || transition.fade == Fade::Show) {
        fadingAction_ = hit.tab;
        if (transition.fade == Fade::Show) actionOpacity_.jump(1);
        else actionOpacity_.animateTo(1, Motion::Hover, animationsEnabled());
    } else if (transition.fade == Fade::Out) actionOpacity_.animateTo(0, Motion::Hover, animationsEnabled());
    if (transition.changed) update();
}

TabPointerHit TabBar::pointerHit(QPoint local, bool releasing) const
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

void TabBar::applyPointerDecision(const TabPointerDecision& decision)
{
    using Action = TabPointerDecision::Action;
    switch (decision.action) {
    case Action::None: return;
    case Action::ActivateTab: {
        const QPointer<TabBar> self(this);
        setCurrentIndex(decision.index);
        if (self && appearance_.rowExpansion == TabRowExpansion::Overlay && rowsState_.expanded()) setExpanded(false);
        return;
    }
    case Action::DragTab: {
        const auto callback = drag;
        if (callback) callback(decision.index, decision.start);
        return;
    }
    case Action::DragFolder: {
        update();
        const auto callback = folderDrag;
        if (callback) callback(decision.index, decision.start);
        return;
    }
    case Action::CloseTab: { const auto callback = close; if (callback) callback(decision.index); return; }
    case Action::UnstickTab: { const auto callback = unstick; if (callback) callback(decision.index); return; }
    case Action::ToggleFolder: { const auto callback = toggleFolder; if (callback) callback(decision.index); return; }
    case Action::AddTabToFolder: { const auto callback = newTabInFolder; if (callback) callback(decision.index); return; }
    }
}

void TabBar::mousePressEvent(QMouseEvent* event)
{
    const auto point = event->position().toPoint();
    if (event->button() == Qt::MiddleButton) {
        (void)pointer_.press(event->button(), {tabAt(point)}, {}); event->accept(); return;
    }
    if (event->button() != Qt::LeftButton) { event->accept(); return; }
    // Hidden scrollbars do not create invisible drag targets over labels.
    if (scrollingLane_ >= 0 && scrollingLane_ < layout_.laneList.size() && scrollbarHitRect().contains(point)) {
        const QPointer<TabBar> self(this);
        event->accept();
        emit scrollInputStarted();
        if (!self) return;
        const int laneIndex = scrollingLane_;
        const auto track = scrollTrack(laneIndex);
        const auto thumb = scrollThumb(laneIndex);
        const auto& lane = layout_.laneList.at(laneIndex);
        if (!thumb.contains(point)) {
            // A press on the track jumps there.
            const int length = lane.vertical ? track.height() - thumb.height() : track.width() - thumb.width();
            const int along = lane.vertical ? point.y() - track.top() - thumb.height() / 2 : point.x() - track.left() - thumb.width() / 2;
            setLaneOffset(laneIndex, int(qint64(std::clamp(along, 0, std::max(1, length))) * lane.maximumOffset() / std::max(1, length)));
        }
        wheelController_.reset();
        scrollDragLane_ = laneIndex; scrollDragOrigin_ = layout_.laneList.at(laneIndex).offset; scrollDragStart_ = point;
        revealScrollbar(scrollDragLane_);
        event->accept(); return;
    }
    const auto hit = pointerHit(point, false);
    const auto label = hit.folder >= 0 ? layout_.headers.at(hit.folder).translated(tabOrigin())
        : hit.tab >= 0 ? animatedRect(hit.tab) : QRect(point, QSize(1, 1));
    const auto start = DragStart::capture(event->globalPosition().toPoint(), point, label);
    const auto decision = pointer_.press(event->button(), hit, start);
    if (hit.folder >= 0 || hit.action >= 0) update();
    event->accept();
    applyPointerDecision(decision);
}

void TabBar::mouseMoveEvent(QMouseEvent* event)
{
    const auto point = event->position().toPoint();
    if (columnPeek_.iconsOnly() && rect().contains(point)) setColumnHovered(true);
    if (scrollDragLane_ >= 0 && scrollDragLane_ < layout_.laneList.size()) {
        const auto& lane = layout_.laneList.at(scrollDragLane_);
        const auto track = scrollTrack(scrollDragLane_);
        const auto thumb = scrollThumb(scrollDragLane_);
        const int length = std::max(1, lane.vertical ? track.height() - thumb.height() : track.width() - thumb.width());
        const int moved = lane.vertical ? point.y() - scrollDragStart_.y() : point.x() - scrollDragStart_.x();
        setLaneOffset(scrollDragLane_, scrollDragOrigin_ + int(qint64(moved) * lane.maximumOffset() / length));
        return;
    }
    updateHover(point);
    applyPointerDecision(pointer_.move(event->buttons(), event->globalPosition().toPoint(), QApplication::startDragDistance()));
}

void TabBar::mouseReleaseEvent(QMouseEvent* event)
{
    const auto point = event->position().toPoint();
    event->accept();
    if (event->button() == Qt::MiddleButton) {
        applyPointerDecision(pointer_.release(event->button(), {tabAt(point)}, stickyCount_, guardsMiddleClick(appearance_.preventStickyClose)));
        return;
    }
    if (scrollDragLane_ >= 0) { wheelController_.reset(); scrollDragLane_ = -1; scrollbarHideTimer_.start(); reconcileColumnHover(); update(); return; }
    const auto hit = pointerHit(point, true);
    const bool repaint = pointer_.pressedFolder() >= 0 || pointer_.pressedAction() >= 0;
    const auto decision = pointer_.release(event->button(), hit, stickyCount_, guardsMiddleClick(appearance_.preventStickyClose));
    if (repaint) update();
    applyPointerDecision(decision);
}

void TabBar::mouseDoubleClickEvent(QMouseEvent* event)
{
    // A double click on a tab is a second press; nothing else happens.
    mousePressEvent(event);
}

void TabBar::wheelEvent(QWheelEvent* event)
{
    const auto delta = event->pixelDelta().isNull() ? event->angleDelta() / 3 : event->pixelDelta();
    scrollAt(event->position().toPoint(), delta, event->pixelDelta().isNull());
    event->accept();
}

void TabBar::enterEvent(QEnterEvent* event)
{
    collapseTimer_.stop();
    updateHover(event->position().toPoint());
    setColumnHovered(true);
    QWidget::enterEvent(event);
}

void TabBar::leaveEvent(QEvent* event)
{
    actionOpacity_.animateTo(0, Motion::Hover, animationsEnabled());
    hover_.leave();
    if (!columnManaged_) setColumnHovered(false);
    if (rowsState_.collapseOnLeave()) collapseTimer_.start();
    update();
    QWidget::leaveEvent(event);
}

void TabBar::contextMenuEvent(QContextMenuEvent* event)
{
    if (const int folder = tabAt(event->pos()) < 0 ? folderAt(event->pos()) : -1; folder >= 0) {
        const auto callback = folderMenu;
        if (callback) showColumnPopup([callback, folder, global = event->globalPos()] { callback(folder, global); });
        return;
    }
    const auto callback = menu;
    if (callback) showColumnPopup([callback, index = tabAt(event->pos()), global = event->globalPos()] { callback(index, global); });
}

void TabBar::keyPressEvent(QKeyEvent* event)
{
    const auto decision = tabKeyboardDecision(event->key(), current_, count(), vertical(), rowsState_.expanded());
    using Action = TabKeyboardDecision::Action;
    if (decision.action == Action::Ignore) { QWidget::keyPressEvent(event); return; }
    event->accept();
    if (decision.action == Action::ActivateTab) {
        const QPointer<TabBar> self(this);
        setCurrentIndex(decision.index);
        if (self) ensureVisible(decision.index);
    } else if (decision.action == Action::Collapse) {
        setExpanded(false);
    }
}

void TabBar::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (!vertical() && rowTransitionRunning() && event->oldSize().width() == event->size().width()) {
        relayoutTrailing(); updateLiftedLayer(); refreshScrollInput(); update(); return;
    }
    relayout(vertical() && columnModeTransition_ && stickyCount_ > 1
        && appearance_.stickySizing == StickyTabSizing::Compact);
}

void TabBar::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::ActivationChange) {
        invalidateCarrier(true);
        update();
        if (liftedLayer_) liftedLayer_->update();
    }
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange || event->type() == QEvent::LayoutDirectionChange) {
        metrics_ = TabMetrics::forTheme(theme_, font());
        painter_ = TabPainter(metrics_, TabPalette::forTheme(theme_));
        updateTitleWidths();
        relayout(false);
        if (const auto callback = reservedSizeChanged) callback();
    }
    QWidget::changeEvent(event);
}

bool TabBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == scrollInput_) {
        if (event->type() == QEvent::Enter) {
            if (scrollDragLane_ < 0) scrollbarHideTimer_.start();
            setColumnHovered(true);
        } else if (event->type() == QEvent::Leave) {
            if (scrollDragLane_ < 0) { scrollbarHideTimer_.start(); reconcileColumnHover(); }
        } else if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease
                   || event->type() == QEvent::MouseButtonDblClick || event->type() == QEvent::MouseMove) {
            auto* source = static_cast<QMouseEvent*>(event);
            QMouseEvent forwarded(source->type(), QPointF(mapFromGlobal(source->globalPosition().toPoint())), source->globalPosition(),
                source->button(), source->buttons(), source->modifiers(), source->pointingDevice());
            forwarded.setTimestamp(source->timestamp());
            QCoreApplication::sendEvent(this, &forwarded);
            return true;
        } else if (event->type() == QEvent::Wheel) {
            auto* source = static_cast<QWheelEvent*>(event);
            scrollAt(mapFromGlobal(source->globalPosition().toPoint()), source->pixelDelta().isNull() ? source->angleDelta() / 3 : source->pixelDelta(), source->pixelDelta().isNull());
            return true;
        } else if (event->type() == QEvent::ContextMenu) {
            auto* source = static_cast<QContextMenuEvent*>(event);
            QContextMenuEvent forwarded(source->reason(), mapFromGlobal(source->globalPos()), source->globalPos(), source->modifiers());
            QCoreApplication::sendEvent(this, &forwarded); return true;
        }
    }
    if (watched == liftedLayer_ && event->type() == QEvent::Paint) {
        QPainter painter(liftedLayer_);
        painter.translate(mapTo(liftedLayer_->parentWidget(), QPoint()) - liftedLayer_->pos());
        paintLiftedTabs(painter);
        return true;
    }
    if ((watched == trailing_ || watched == end_) && event->type() == QEvent::Enter) {
        updateHover(QPoint(-1, -1)); hover_.endSession();
    }
    // The trailing controls change size when their contents change.
    if ((watched == trailing_ || watched == end_) && (event->type() == QEvent::LayoutRequest || event->type() == QEvent::ShowToParent
                                 || event->type() == QEvent::HideToParent)) {
        QTimer::singleShot(0, this, [self = QPointer<TabBar>(this)] { if (self) self->refreshTrailing(); });
        return false;
    }
    // An expanded overlay closes on a press elsewhere or Escape.
    if (rowsState_.dismissesOutsideInput(appearance_.rowExpansion == TabRowExpansion::Overlay)) {
        if (event->type() == QEvent::MouseButtonPress) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            const QRect area(mapToGlobal(QPoint()), size());
            if (!area.contains(mouse->globalPosition().toPoint())) setExpanded(false);
        } else if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            setExpanded(false);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

bool TabBar::event(QEvent* event)
{
    if (event->type() == QEvent::Hide) { scrollMotion_.stop(); smoothLane_ = -1; }
    if (event->type() == QEvent::Hide && scrollInput_) scrollInput_->hide();
    if (event->type() == QEvent::Show || event->type() == QEvent::Move)
        QTimer::singleShot(0, this, [this] { refreshScrollInput(); if (columnPeek_.iconsOnly()) reconcileColumnHover(); });
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        const int index = tabAt(help->pos());
        QString text; QRect area;
        if (index >= 0) {
            area = tabRect(index);
            const auto action = actionRect(index);
            if (!action.isEmpty() && action.contains(help->pos())) {
                area = action;
                text = index < stickyCount_ ? QCoreApplication::translate("WindowSystem", "Unstick tab")
                                            : QCoreApplication::translate("WindowSystem", "Close tab");
            } else {
                text = tabToolTip(index);
                // Leaving the title for its action dismisses the title's tip.
                if (!action.isEmpty()) {
                    if (help->pos().x() < action.left()) area.setRight(action.left() - 1);
                    else if (help->pos().x() > action.right()) area.setLeft(action.right() + 1);
                }
            }
        }
        if (const int folder = index < 0 ? folderAt(help->pos()) : -1; folder >= 0) {
            area = folderHeaderRect(folder);
            const auto& record = folders_.at(folder);
            if (vertical() && painter_.folderAddRect(area).contains(help->pos()))
                text = QCoreApplication::translate("WindowSystem", "New tab in this tab folder");
            else {
                const auto count = QCoreApplication::translate("WindowSystem", "%n tab(s)", nullptr, record.count);
                text = record.name.isEmpty() ? count : record.name + QStringLiteral(" · ") + count;
            }
        }
        if (!text.isEmpty()) QToolTip::showText(help->globalPos(), text, this, area);
        else { QToolTip::hideText(); event->ignore(); }
        return true;
    }
    return QWidget::event(event);
}

} // namespace window_system
