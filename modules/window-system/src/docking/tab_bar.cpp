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

QRect lerp(const QRect& from, const QRect& to, qreal t)
{
    const auto mix = [t](int a, int b) { return int(std::lround(a + (b - a) * t)); };
    return QRect(QPoint(mix(from.left(), to.left()), mix(from.top(), to.top())),
                 QSize(mix(from.width(), to.width()), mix(from.height(), to.height())));
}

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
    connect(countButton_, &QToolButton::clicked, this, [this] { setExpanded(!expanded_); });
    collapseTimer_.setSingleShot(true); collapseTimer_.setInterval(400);
    connect(&collapseTimer_, &QTimer::timeout, this, [this] { if (expandedByWheel_) setExpanded(false); });
    peekTimer_.setSingleShot(true); peekTimer_.setInterval(120);
    connect(&peekTimer_, &QTimer::timeout, this, [this] {
        if (!iconMode_ || peeking_) return;
        peeking_ = true; relayout(true);
        if (const auto callback = reservedSizeChanged) callback();
    });
    slideTimer_.setInterval(15);
    connect(&slideTimer_, &QTimer::timeout, this, [this] {
        if (slideClock_.elapsed() >= slideDuration_ && (!planarReflow_ || presenceProgress_.state() != QAbstractAnimation::Running)) {
            slides_.clear(); settling_.clear(); planarReflow_ = false; slideTimer_.stop();
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
        for (auto it = presence_.begin(); it != presence_.end();)
            if (it->to == 1) it = presence_.erase(it); else ++it;
        surfaceFrameChanged();
    });
    connect(&rowsReveal_, &QVariantAnimation::valueChanged, this, [this] {
        invalidateCarrier(false);
        if (const auto callback = reservedSizeChanged) callback();
        relayoutTrailing(); refreshScrollInput();
        update();
    });
    connect(&rowsReveal_, &QVariantAnimation::finished, this, [this] {
        if (!dragExpanded_) temporaryRowsOverlay_ = false;
        if (!expanded_ && !dragExpanded_) qApp->removeEventFilter(this);
        if (const auto callback = reservedSizeChanged) callback();
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
        wheelController_.observeRowScroll(layout_, lane, expanded_, placement_ == TabBarPlacement::Bottom, rowWheelClock_.elapsed());
    };
    connect(&scrollbarOpacity_, &QVariantAnimation::valueChanged, this, [this] { update(); refreshScrollInput(); });
    connect(&scrollbarHideTimer_, &QTimer::timeout, this, [this] {
        if (scrollDragLane_ < 0) scrollbarOpacity_.animateTo(0, Motion::Presence, animationsEnabled());
    });
}

TabBar::~TabBar() { qApp->removeEventFilter(this); delete scrollInput_.data(); delete liftedLayer_.data(); }

int TabBar::addTab(const QIcon& icon, const QString& title)
{
    Tab tab; tab.icon = icon; tab.title = title;
    tabs_.append(std::move(tab));
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

QVariant TabBar::tabData(int index) const { return index >= 0 && index < tabs_.size() ? tabs_.at(index).data : QVariant{}; }
void TabBar::setTabData(int index, const QVariant& value)
{
    if (index < 0 || index >= tabs_.size() || tabs_.at(index).data == value) return;
    tabs_[index].data = value;
    invalidateCarrier(true);
}
QString TabBar::tabText(int index) const { return index >= 0 && index < tabs_.size() ? tabs_.at(index).title : QString{}; }
void TabBar::setTabText(int index, const QString& text)
{
    if (index < 0 || index >= tabs_.size() || tabs_.at(index).title == text) return;
    tabs_[index].title = text; tabs_[index].titleWidth = -1;
    relayout(true);
}
QIcon TabBar::tabIcon(int index) const { return index >= 0 && index < tabs_.size() ? tabs_.at(index).icon : QIcon{}; }
void TabBar::setTabIcon(int index, const QIcon& icon)
{
    if (index < 0 || index >= tabs_.size()) return;
    tabs_[index].icon = icon; relayout(true);
}
QString TabBar::tabToolTip(int index) const { return index >= 0 && index < tabs_.size() ? tabs_.at(index).tooltip : QString{}; }
void TabBar::setTabToolTip(int index, const QString& tip) { if (index >= 0 && index < tabs_.size()) tabs_[index].tooltip = tip; }
void TabBar::setTabMetadata(int index, const TabRecord& tab)
{
    if (index < 0 || index >= tabs_.size()) return;
    tabs_[index].colorId = tab.colorId; invalidateCarrier(true); update();
}
const TabColorPreset* TabBar::tabPreset(int index) const
{
    if (index < 0 || index >= tabs_.size() || tabs_.at(index).colorId.isEmpty()) return nullptr;
    for (const auto& preset : appearance_.colors) if (preset.id == tabs_.at(index).colorId) return &preset;
    return nullptr;
}
int TabBar::indexOf(TabId tab) const
{
    for (int index = 0; index < tabs_.size(); ++index) if (tabs_.at(index).data.toUuid() == tab) return index;
    return -1;
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
    if (index < 0 || index != current_ || index >= count() || !incomingVisual_.empty()) return 0;
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
    const qreal panel = transitioning ? (columnOverlay ? surfaceJoin_ : rowOverlay ? 1 - rowsReveal_.value() : 1) : 1;
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
    if (!incomingVisual_.empty()) {
        const auto incoming = incomingRect();
        return incomingVisual_.active.isEmpty() ? incoming : incomingVisual_.active.translated(incoming.topLeft());
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
    if (!incomingVisual_.empty()) {
        anchor = activeDragRect();
        compact = dropSticky_ && compactStickyTiles();
    } else if (current_ >= 0 && current_ < layout_.tabs.size()) {
        anchor = liftedPointer_ && (dragged_ == current_ || draggedFolder_ >= 0) ? activeDragRect() : animatedRect(current_);
        compact = shapeOf(current_) == TabShape::Tile;
        if (liftedPointer_ && dragged_ == current_) compact = dropSticky_ && compactStickyTiles();
    }
    const auto geometry = anchorGeometry(anchor, anchor.intersected(rect()), compact,
        !incomingVisual_.empty() || bool(liftedPointer_), false);
    const std::pair<qreal, qreal> target{geometry.joinProgress, geometry.coveredProgress};
    const auto owner = !incomingVisual_.empty() ? incomingVisual_.identity : tabData(current_).toUuid();
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
    SurfaceState state{{}, tabData(current_).toUuid(), liftElevation_.value(), !incomingVisual_.empty()};
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
    const auto found = presence_.constFind(tabs_.at(index).data.toUuid());
    if (found == presence_.cend()) return {};
    return labelPresencePose(area, found->from + (found->to - found->from) * presenceProgress_.value(), found->kind);
}

QVector<TabSurfaceAnchor> TabBar::surfaceAnchors(QPointF offset) const
{
    QVector<TabSurfaceAnchor> anchors;
    if (surfaceBody_.isEmpty()) return anchors;
    const auto rest = interpolateElevation(elevationProfile(ElevationLevel::AnchorRest),
                                           elevationProfile(ElevationLevel::AnchorRaised), surfaceFocus_);
    if (!incomingVisual_.empty() || (liftedPointer_ && (dragged_ == current_ || draggedFolder_ >= 0))) {
        const auto anchor = activeDragRect();
        const bool compact = (dropIndex_ >= 0 ? dropSticky_ : dragged_ < stickyCount_) && compactStickyTiles();
        const auto geometry = anchorGeometry(anchor, anchor.intersected(rect()), compact, true);
        if (!geometry.anchorPath.isEmpty()) anchors.append({translatedSurfaceGeometry(geometry, offset),
            interpolateElevation(!incomingVisual_.empty() ? rest : interpolateElevation(elevationProfile(ElevationLevel::Flat), rest, selectionWeight(current_)),
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
        const bool settling = settling_.contains(tabData(index).toUuid());
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
    const bool moving = !incomingVisual_.empty() || (liftedPointer_ && (dragged_ == current_ || draggedFolder_ >= 0));
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
        expanded_ = false; expandedByWheel_ = false; temporaryRowsOverlay_ = false; wheelController_.reset(); rowsReveal_.jump(0); rowsOffset_ = 0;
    }
    if (!vertical()) { iconMode_ = false; peeking_ = false; }
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
    if (iconMode_ == iconMode) return;
    iconMode_ = iconMode; peeking_ = false;
    relayout(true);
    if (iconMode_) QTimer::singleShot(0, this, [this] { reconcileColumnHover(); });
    if (const auto callback = reservedSizeChanged) callback();
}

void TabBar::setColumnReveal(qreal reveal, bool modeTransition)
{
    if (columnReveal_ == reveal && columnModeTransition_ == modeTransition) return;
    const bool iconOnlyChanged = (columnReveal_ > 0) != (reveal > 0);
    columnReveal_ = reveal; columnModeTransition_ = modeTransition;
    invalidateCarrier(iconOnlyChanged); update();
}

void TabBar::setColumnHovered(bool hovered)
{
    if (hovered) {
        if (iconMode_ && !peeking_ && !peekTimer_.isActive()) peekTimer_.start();
        return;
    }
    peekTimer_.stop();
    if (columnPopupOpen_ || scrollDragLane_ >= 0) return;
    if (!peeking_) return;
    peeking_ = false; relayout(true);
    if (const auto callback = reservedSizeChanged) callback();
}

void TabBar::restoreColumnPeek(bool peeked)
{
    if (!iconMode_ || !peeked || peeking_) return;
    peekTimer_.stop(); peeking_ = true; relayout(false);
    if (const auto callback = reservedSizeChanged) callback();
    QTimer::singleShot(0, this, [this] { trackColumnPopup(); });
}

void TabBar::showColumnPopup(std::function<void()> open)
{
    const QPointer<TabBar> self(this);
    columnPopupOpen_ = true; peekTimer_.stop();
    open();
    if (self) QTimer::singleShot(0, this, [this] { trackColumnPopup(); });
}

void TabBar::trackColumnPopup()
{
    if (columnPopup_ && columnPopup_->isVisible()) return;
    disconnect(popupHidden_); disconnect(popupDestroyed_);
    auto* popup = qobject_cast<QMenu*>(QApplication::activePopupWidget());
    if (popup && popup->isVisible()) {
        columnPopupOpen_ = true; columnPopup_ = popup;
        const auto closed = [this] { QTimer::singleShot(0, this, [this] { trackColumnPopup(); }); };
        popupHidden_ = connect(popup, &QMenu::aboutToHide, this, closed);
        popupDestroyed_ = connect(popup, &QObject::destroyed, this, closed);
        return;
    }
    columnPopup_.clear(); columnPopupOpen_ = false; reconcileColumnHover();
}

void TabBar::reconcileColumnHover()
{
    auto* owner = columnManaged_ ? parentWidget() : this;
    setColumnHovered(isVisible() && owner && owner->isVisible() && owner->rect().contains(owner->mapFromGlobal(QCursor::pos())));
}

TabBar::VisualState TabBar::captureVisualState()
{
    VisualState result;
    const auto globalOrigin = mapToGlobal(QPoint());
    for (int index = 0; index < count(); ++index) {
        const auto rect = animatedRect(index);
        if (rect.isEmpty()) continue;
        auto parts = partsOf(index, rect);
        for (auto* part : {&parts.tab, &parts.icon, &parts.number, &parts.title, &parts.action}) part->translate(globalOrigin);
        std::shared_ptr<RetiredLabel> retired;
        if (isVisible() && !tabRect(index).isEmpty()) {
            retired = std::make_shared<RetiredLabel>();
            retired->label = TabPainter::captureLabel(parts, paintState(index), font(), layoutDirection());
            if (retired->label.state.selected) {
                auto geometry = anchorGeometry(rect, rect.intersected(this->rect()), shapeOf(index) == TabShape::Tile, false);
                geometry.body = {}; geometry.bodyPath = {}; geometry.mergedPath = geometry.anchorPath;
                geometry.joined = false;
                retired->label.surface = translatedSurfaceGeometry(geometry, globalOrigin);
            }
            retired->titleLayout = tabs_.at(index).titleLayout;
            const auto presence = presence_.constFind(tabData(index).toUuid());
            if (presence != presence_.cend() && presence->kind != LabelPresenceEffect::Kind::Planar)
                retired->visibility = presence->from + (presence->to - presence->from) * presenceProgress_.value();
        }
        result.insert(tabData(index).toUuid(), {rect.translated(globalOrigin), shapeOf(index),
            index < stickyCount_, index == current_, iconsOnly() || shapeOf(index) == TabShape::Tile, parts, std::move(retired), hasSingleTileRow()});
    }
    // Flat exits belong to this strip's single clock. Capture their current
    // width too, so reversing a pin operation does not restart from full size.
    for (auto it = departing_.cbegin(); it != departing_.cend(); ++it) if (it->collapse && it->retired) {
        auto visible = *it;
        visible.global = LabelPresenceEffect::planarRect(it->global, *it->collapse, 1 - presenceProgress_.value());
        if (!visible.global.isEmpty()) result.insert(it.key(), std::move(visible));
    }
    return result;
}

void TabBar::paintRetiredLabel(QPainter& painter, const TabPainter& style, const LabelVisual& label,
                              QPoint globalOrigin, qreal visibility)
{
    if (!label.retired) return;
    auto& retired = *label.retired;
    auto presentation = retired.label;
    if (presentation.surface) presentation.surface = translatedSurfaceGeometry(*presentation.surface, -globalOrigin);
    presentation.state.elevationManaged = false;
    presentation.bodyElevation = elevationProfile(ElevationLevel::Flat);
    auto& parts = presentation.parts;
    parts = label.parts;
    for (auto* part : {&parts.tab, &parts.icon, &parts.number, &parts.title, &parts.action}) part->translate(-globalOrigin);
    if (label.collapse) {
        const auto area = LabelPresenceEffect::planarRect(label.global, *label.collapse, visibility).translated(-globalOrigin);
        if (!area.isEmpty()) {
            TabLabelParts sizes; sizes.icon = parts.icon.width();
            parts = style.parts(area, presentation.state.shape, sizes, true, false, false, Qt::LeftToRight);
            presentation.surface.reset(); presentation.state.elevationManaged = true; presentation.state.lifted = false;
            style.paintLabel(painter, presentation, &retired.titleLayout);
        }
    } else {
        presentation.presence = labelPresencePose(parts.tab, visibility * retired.visibility);
        style.paintLabel(painter, presentation, &retired.titleLayout);
    }
}

void TabBar::animatePresence(const QSet<QUuid>& ids, bool visible, Motion motion)
{
    const qreal progress = presenceProgress_.value();
    for (auto& value : presence_) value.from += (value.to - value.from) * progress;
    for (const auto& id : ids) {
        const qreal from = presence_.contains(id) ? presence_.value(id).from : (visible ? 0 : 1);
        presence_.insert(id, {from, visible ? 1.0 : 0.0});
    }
    presenceProgress_.jump(0);
    presenceProgress_.animateTo(1, motion, animationsEnabled());
    if (!animationsEnabled()) { departing_.clear(); if (visible) for (const auto& id : ids) presence_.remove(id); }
    surfaceFrameChanged();
}

bool TabBar::hasSingleTileRow() const
{
    return compactStickyTiles() && stickyCount_ > 0 && layout_.tileColumns >= stickyCount_;
}

bool TabBar::usesPlanarReflow(const VisualState& previous) const
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

TabBar::VisualState TabBar::takeSeparateDepartures(VisualState& previous) const
{
    VisualState separate;
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

void TabBar::transitionFrom(VisualState previous, Motion motion)
{
    if (!animationsEnabled() || !isVisible()) return;
    planarReflow_ = usesPlanarReflow(previous);
    if (planarReflow_) motion = Motion::Presence;
    slideDuration_ = motionDuration(motion); slideCurve_ = motionCurve(motion);
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
    slides_.clear(); settling_.clear(); departing_.clear();
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
                slides_.insert(id, {QRect(mapFromGlobal(slot.topLeft()), slot.size()), true}); planarEntries.insert(id);
            }
            continue;
        }
        const QRect from(mapFromGlobal(old->global.topLeft()), old->global.size());
        if (old->sticky != (index < stickyCount_)) {
            departing_.insert(id, *old); appeared.insert(id);
            if (entrySlots.contains(id)) {
                const auto slot = entrySlots.value(id);
                slides_.insert(id, {QRect(mapFromGlobal(slot.topLeft()), slot.size()), true}); planarEntries.insert(id);
            }
        } else if (!from.isEmpty() && from != layout_.tabs.at(index).translated(tabOrigin()))
            slides_.insert(id, {from, old->shape == TabShape::Tile && shapeOf(index) == TabShape::Tile});
    }
    for (auto it = previous.cbegin(); it != previous.cend(); ++it)
        if (it->retired && !currentIds.contains(it.key())) departing_.insert(it.key(), *it);
    if (!slides_.isEmpty()) { slideClock_.start(); slideTimer_.start(); }
    if (planarReflow_ || !appeared.isEmpty() || !departing_.isEmpty()) animatePresence(appeared, true, motion);
    for (const auto& id : planarEntries) if (presence_.contains(id)) presence_[id].kind = LabelPresenceEffect::Kind::Planar;
}

LabelPositions TabBar::captureDropPositions(std::optional<LabelPositions> carrierPositions) const
{
    LabelPositions positions;
    const auto origin = mapToGlobal(QPoint());
    if (!incomingVisual_.empty()) {
        if (carrierPositions) positions = std::move(*carrierPositions);
        else if (!incomingVisual_.identity.isNull()) positions.insert(incomingVisual_.identity, incomingVisual_.active);
        const auto offset = origin + incomingRect().topLeft();
        for (auto& area : positions) area.translate(offset);
    } else if (liftedPointer_) {
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
                slides_.insert(id, {local, true}); settling_.insert(id, index); presence_.remove(id);
            }
        } else if (slides_.contains(id)) slides_[id].from = animatedRect(index);
    }
    if (settling_.isEmpty()) return;
    planarReflow_ = false;
    slideCurve_ = motionCurve(Motion::SurfaceLower); slideDuration_ = motionDuration(Motion::SurfaceLower);
    slideClock_.start(); slideTimer_.start();
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
    parts.number = appearance_.showNumbers ? fontMetrics().horizontalAdvance(QString::number(index + 1)) : 0;
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
    for (auto& tab : tabs_) {
        tab.titleLayout.prepare(tab.title, font(), layoutDirection(), true, this);
        tab.titleWidth = tab.titleLayout.naturalWidth();
    }
}

bool TabBar::wrappedRows() const
{
    return !vertical() && (appearance_.multiline
        || (appearance_.stickyTabsOnSeparateRow && stickyCount_ > 0 && appearance_.stickyMultiline));
}

bool TabBar::collapsible() const
{
    return !vertical() && ((appearance_.multiline && !appearance_.showAllRows)
        || (appearance_.stickyTabsOnSeparateRow && stickyCount_ > 0 && appearance_.stickyMultiline && !appearance_.stickyShowAllRows));
}

bool TabBar::collapsedNow() const { return collapsible() && !expanded_ && !dragExpanded_; }

bool TabBar::overlayActive() const
{
    if (vertical()) return iconMode_ && peeking_;
    return collapsible() && ((temporaryRowsOverlay_ && (dragExpanded_ || rowsReveal_.value() > 0))
        || ((expanded_ || rowsReveal_.value() > 0) && appearance_.rowExpansion == TabRowExpansion::Overlay));
}

TabLayoutInput TabBar::layoutInput(QSize area, bool collapsed) const
{
    TabLayoutInput input;
    input.orientation = vertical() ? TabLayoutInput::Orientation::Vertical : TabLayoutInput::Orientation::Horizontal;
    input.width = area.width();
    // Horizontal rows touch, so colored tabs join across rows.
    input.rowHeight = metrics_.rowHeight; input.rowGap = vertical() ? metrics_.rowGap : 0;
    input.inset = metrics_.verticalInset; input.tile = metrics_.tile; input.minimumTileWidth = metrics_.tile.width();
    input.tileColumns = carriedTileColumns_;
    input.stickyCount = stickyCount_;
    input.extents.reserve(tabs_.size());
    for (int index = 0; index < tabs_.size(); ++index)
        input.extents.append(vertical() ? 0 : tabExtent(index, index < stickyCount_));
    if (vertical()) {
        input.height = area.height();
        input.stickyTiles = compactStickyTiles();
        input.stickyOffset = stickyOffset_; input.regularOffset = regularOffset_;
    } else {
        input.multiline = appearance_.multiline;
        input.stickyMultiline = appearance_.stickyMultiline;
        input.separateStickyRow = appearance_.stickyTabsOnSeparateRow;
        input.freezeSticky = !input.multiline && !input.separateStickyRow && appearance_.stickySizing != StickyTabSizing::Normal;
        input.collapsed = collapsed && !appearance_.showAllRows;
        input.stickyCollapsed = collapsed && !appearance_.stickyShowAllRows;
        // Rows take at most 40 % of the group, an overlay half of it.
        const int group = available_.height();
        input.height = !wrappedRows() ? 0 : group <= 0 ? 0 : int(group * (overlayActive() ? 0.5 : 0.4));
        input.trailingExtent = trailing_ && !trailing_->isHidden() ? trailing_->sizeHint().width() : 0;
        int end = end_ && !end_->isHidden() ? end_->sizeHint().width() : 0;
        if (countButton_ && !countButton_->isHidden()) end += countButton_->width();
        input.endExtent = end;
        input.stickyOffset = stickyOffset_;
        input.regularOffset = regularOffset_;
        input.rowsOffset = rowsOffset_;
        input.collapsedStickyRow = collapsedStickyRow_; input.collapsedRegularRow = collapsedRegularRow_;
    }
    input.current = current_;
    input.removed = dragged_;
    input.insertion = dropIndex_; input.insertionSticky = dropSticky_; input.block = dropBlock_;
    // Vertical headers are rows; their tabs sit indented under them.
    input.folderIndent = metrics_.folderIndent;
    for (const auto& folder : folders_)
        input.folders.append({folder.first, folder.count, folderHeaderExtent(folder), folder.collapsed});
    input.removedFolder = draggedFolder_;
    input.insertionFolder = dropFolder_;
    return input;
}

QPoint TabBar::tabOrigin() const
{
    if (vertical()) return {};
    // Below the content an overlay grows upward, its shadow on top.
    if (placement_ == TabBarPlacement::Bottom) return {0, rowShadowExtent()};
    return {0, metrics_.outerMargin};
}

QSize TabBar::tabArea(QSize size) const
{
    // An overlay keeps room for its shadow below the rows.
    const int shadow = !vertical() ? rowShadowExtent() : 0;
    return {size.width(), std::max(0, size.height() - (vertical() ? 0 : metrics_.outerMargin) - shadow)};
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
    return overlayActive() ? qRound(metrics_.shadowBlur * rowsReveal_.value()) : 0;
}

const TabBar::RowMeasure& TabBar::measureRows(QSize available) const
{
    if (rowMeasure_ && rowMeasure_->available == available) return *rowMeasure_;
    auto input = layoutInput(QSize(available.width(), 0), true);
    input.height = std::max(0, int(available.height() * 0.4));
    input.block.clear(); input.insertion = -1; input.removed = -1; input.removedFolder = -1;
    const int closed = std::max(metrics_.rowHeight, layoutTabs(input).size.height()) + metrics_.outerMargin;
    int push = closed, overlay = closed;
    if (collapsible()) {
        input = layoutInput(QSize(available.width(), 0), false);
        input.height = std::max(0, int(available.height() * 0.4));
        input.block.clear(); input.insertion = -1; input.removed = -1; input.removedFolder = -1;
        push = std::max(metrics_.rowHeight, layoutTabs(input).size.height()) + metrics_.outerMargin;
        input = layoutInput(QSize(available.width(), 0), false);
        input.height = std::max(0, int(available.height() * 0.5));
        overlay = std::min(available.height(), layoutTabs(input).size.height() + metrics_.outerMargin + metrics_.shadowBlur);
    }
    rowMeasure_ = RowMeasure{available, closed, push, overlay};
    return *rowMeasure_;
}

QSize TabBar::reservedSize(QSize available) const
{
    if (vertical()) {
        const int width = iconMode_ ? metrics_.verticalIconWidth : std::clamp(available.width(), minimumColumnWidth(), maximumVerticalTabWidth);
        // `available` is the column's width and the group's height.
        return {width, available.height()};
    }
    const auto sizes = measureRows(available);
    const qreal progress = collapsible() && appearance_.rowExpansion == TabRowExpansion::Push && !temporaryRowsOverlay_ ? rowsReveal_.value() : 0;
    return {available.width(), qRound(sizes.closed + (sizes.push - sizes.closed) * progress)};
}

QSize TabBar::displaySize(QSize available) const
{
    if (vertical()) return reservedSize(available);
    auto base = reservedSize(available);
    if (overlayActive()) {
        const auto sizes = measureRows(available);
        const qreal progress = rowsReveal_.value();
        const int full = sizes.overlay;
        base.setHeight(qRound(base.height() + (full - base.height()) * progress));
    } else if (dropIndex_ >= 0) {
        const auto preview = layoutTabs(layoutInput(QSize(available.width(), 0), collapsedNow()));
        base.setHeight(std::max(base.height(), preview.size.height() + metrics_.outerMargin));
    }
    return base;
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
            if (const auto slide = slides_.constFind(id); slide != slides_.cend() && slide->resizeWidth) resizing.insert(id);
        }
    for (auto& tab : tabs_) if (tab.titleWidth < 0) {
        tab.titleLayout.prepare(tab.title, font(), layoutDirection(), true, this);
        tab.titleWidth = tab.titleLayout.naturalWidth();
    }
    const auto area = tabArea(size());
    // The count shows only while rows are collapsed away; measure without
    // it first, then make room for it.
    bool countShown = false;
    if (collapsible()) {
        auto probe = layoutInput(area, true); probe.block.clear(); probe.insertion = -1;
        if (countButton_) countButton_->hide();
        probe.endExtent = end_ && !end_->isHidden() ? end_->sizeHint().width() : 0;
        countShown = layoutTabs(probe).hidden > 0;
    }
    if (countButton_) countButton_->setVisible(countShown && !dragExpanded_);
    // Resolve the count width before exposing layout/hit geometry. A digit
    // change can change wrapping again; shrink once, then only grow within
    // this pass so proportional figures cannot make the calculation cycle.
    bool canShrinkCount = true;
    for (;;) {
        arrangedInput_ = layoutInput(area, collapsedNow());
        layout_ = layoutTabs(arrangedInput_);
        if (!countButton_ || !countButton_->isVisible()) break;
        const bool collapsed = collapsedNow();
        countButton_->setText(collapsed ? QStringLiteral("+%1").arg(layout_.hidden) : QStringLiteral("\u2212"));
        const int needed = std::max(metrics_.countMinimumWidth, fontMetrics().horizontalAdvance(countButton_->text()) + uiPixels(14));
        const int countWidth = countButton_->width();
        if (needed == countWidth || (needed < countWidth && !canShrinkCount)) break;
        canShrinkCount = false;
        static_cast<CompactToolButton*>(countButton_)->setTargetSize(QSize(needed, metrics_.rowHeight));
    }
    if (countButton_ && countButton_->isVisible()) {
        countButton_->setToolTip(collapsedNow()
            ? QCoreApplication::translate("WindowSystem", "%n more tab(s) in other rows; click to expand", nullptr, layout_.hidden)
            : QCoreApplication::translate("WindowSystem", "Collapse other rows"));
        countButton_->setAccessibleName(countButton_->toolTip());
    }
    // Clamped offsets are the offsets.
    for (const auto& lane : std::as_const(layout_.laneList)) {
        if (!vertical() && lane.vertical) { rowsOffset_ = lane.offset; continue; }
        (lane.sticky ? stickyOffset_ : regularOffset_) = lane.offset;
    }
    if (collapsedNow()) { collapsedStickyRow_ = layout_.stickyRow; collapsedRegularRow_ = layout_.regularRow; }
    relayoutTrailing();
    carrierBounds_.reset();
    updateLiftedLayer();
    if (sliding) {
        // Restart from wherever each tab is drawn now.
        slides_.clear(); settling_.clear(); planarReflow_ = false;
        slideCurve_ = motionCurve(Motion::Relocate); slideDuration_ = motionDuration(Motion::Relocate);
        for (int index = 0; index < tabs_.size(); ++index) {
            const auto id = tabs_.at(index).data.toUuid();
            const auto from = before.value(id);
            const auto to = layout_.tabs.at(index).translated(tabOrigin());
            if (!from.isNull() && !layout_.tabs.at(index).isNull() && from != to)
                slides_.insert(id, {from, shapeOf(index) == TabShape::Tile
                    && (previousTileColumns != layout_.tileColumns || resizing.contains(id))});
        }
        if (!slides_.isEmpty()) { slideClock_.start(); slideTimer_.start(); }
    }
    syncAttachment();
    surfaceFrameChanged();
    emit viewChanged();
    refreshScrollInput();
}

void TabBar::relayoutTrailing()
{
    // A vertical strip has no trailing controls; its group shows a footer.
    if (vertical()) { if (countButton_) countButton_->hide(); return; }
    if (!layout_.trailing.isNull() && trailing_ && !trailing_->isHidden()) {
        const auto area = layout_.trailing.translated(tabOrigin());
        trailing_->setGeometry(QRect(area.topLeft(), QSize(trailing_->sizeHint().width(), metrics_.rowHeight)));
        trailing_->raise();
    }
    if (!layout_.end.isNull()) {
        // From the row's end leftward: the end widget, then the row count.
        auto area = layout_.end.translated(tabOrigin());
        if (end_ && !end_->isHidden()) {
            const int width = end_->sizeHint().width();
            end_->setGeometry(area.right() - width + 1, area.top(), width, metrics_.rowHeight);
            end_->raise();
            area.setRight(area.right() - width);
        }
        if (countButton_ && countButton_->isVisible()) {
            countButton_->move(area.right() - countButton_->width() + 1, area.top());
            countButton_->raise();
        }
    }
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
    if (vertical() && shape == TabShape::Row) {
        // The icon follows an outer rail, not the width of the revealed
        // title. Mirror the rail for a right-hand column.
        TabParts parts; parts.tab = rect;
        const bool right = placement_ == TabBarPlacement::Right;
        const auto direction = right ? Qt::RightToLeft : Qt::LeftToRight;
        const int center = right ? width() - metrics_.verticalIconWidth / 2 : metrics_.verticalIconWidth / 2;
        if (label.icon) parts.icon = QRect(center - label.icon / 2, rect.center().y() - label.icon / 2, label.icon, label.icon);
        if (iconOnly) {
            if (!label.icon) parts.title = QRect(center - metrics_.iconSize / 2, rect.top(), metrics_.iconSize, rect.height());
            return parts;
        }
        if (action) parts.action = tabBarCloseRect(rect, direction);
        const int gap = uiPixels(tabPartGap);
        int begin = right ? rect.left() + uiPixels(tabBarTrailing) : center + metrics_.iconSize / 2 + gap;
        int end = right ? center - metrics_.iconSize / 2 - gap : rect.right() + 1 - uiPixels(tabBarTrailing);
        if (action && sticky) {
            if (right) begin = parts.action.right() + 1 + uiPixels(tabActionGap);
            else end = parts.action.left() - uiPixels(tabActionGap);
        }
        // Numbering follows the icon; reserving it before the icon would
        // move that icon as soon as the title is revealed.
        const int readableTitle = std::min(label.title, metrics_.rowHeight - 10);
        if (label.number && end - begin >= label.number + gap + readableTitle) {
            parts.number = QRect(right ? end - label.number : begin, rect.top(), label.number, rect.height());
            if (right) end -= label.number + gap; else begin += label.number + gap;
        }
        parts.title = QRect(begin, rect.top(), std::max(0, end - begin), rect.height());
        return parts;
    }
    return painter_.parts(rect, shape, label, iconOnly, action, sticky, layoutDirection());
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
    const auto slide = slides_.constFind(tabs_.at(index).data.toUuid());
    if (slide == slides_.cend() || !slideClock_.isValid()) return target;
    const qreal progress = std::clamp(qreal(slideClock_.elapsed()) / slideDuration_, 0.0, 1.0);
    auto result = lerp(slide->from, target, planarReflow_ ? presenceProgress_.value() : slideCurve_.valueForProgress(progress));
    // A stretched row has one width driver: the column itself. Preserve the
    // separate rearrangement of multiple compact sticky tiles on an explicit
    // mode switch, including their positions and size transition.
    const bool rearrangingTiles = columnModeTransition_ && stickyCount_ > 1 && index < stickyCount_
        && appearance_.stickySizing == StickyTabSizing::Compact;
    if (vertical() && columnManaged_ && !rearrangingTiles && (columnModeTransition_ || !slide->resizeWidth)
        && !settling_.contains(tabs_.at(index).data.toUuid())) result.setWidth(target.width());
    return result;
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
        .expanded = expanded_
    };
    const auto decision = wheelController_.route(layout_, local - tabOrigin(), delta, policy, rowWheelClock_.elapsed());
    using Action = TabWheelDecision::Action;
    if (decision.action == Action::Ignore) return false;
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
    const QPointer<TabBar> self(this);
    const bool moved = scrollLaneBy(lane, decision.delta, smooth);
    if (!self) return true;
    wheelController_.observeRowScroll(layout_, lane, expanded_, placement_ == TabBarPlacement::Bottom, rowWheelClock_.elapsed());
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

void TabBar::updateRowPresentation(VisualState previous, bool moving, bool revealActive)
{
    const QPointer<TabBar> self(this);
    const bool open = expanded_ || dragExpanded_;
    if (open || moving && rowsReveal_.value() > 0) {
        if (temporaryRowsOverlay_ || appearance_.rowExpansion == TabRowExpansion::Overlay) qApp->installEventFilter(this);
    } else qApp->removeEventFilter(this);
    if (moving) rowsReveal_.animateTo(open ? 1.0 : 0.0, Motion::Reveal, true);
    else rowsReveal_.jump(open ? 1 : 0);
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
    expandedByWheel_ = expanded && byWheel;
    collapseTimer_.stop();
    if (expanded_ == expanded) {
        if (!animate) rowsReveal_.jump(expanded || dragExpanded_ ? 1 : 0);
        return;
    }
    const bool moving = animate && isVisible() && animationsEnabled();
    const bool activeWasVisible = fullyVisible(current_);
    auto before = moving ? captureVisualState() : VisualState{};
    expanded_ = expanded;
    wheelController_.reset();
    updateRowPresentation(std::move(before), moving, activeWasVisible);
}

void TabBar::setDragExpanded(bool expanded)
{
    if (!collapsible()) expanded = false;
    if (dragExpanded_ == expanded) return;
    const bool moving = isVisible() && animationsEnabled();
    const bool activeWasVisible = fullyVisible(current_);
    auto before = moving ? captureVisualState() : VisualState{};
    dragExpanded_ = expanded;
    if (expanded && !expanded_) temporaryRowsOverlay_ = true;
    if (!expanded && (!moving || expanded_)) temporaryRowsOverlay_ = false;
    updateRowPresentation(std::move(before), moving, activeWasVisible);
}

void TabBar::setDraggedTab(TabId tab)
{
    const int index = tab.isNull() ? -1 : indexOf(tab);
    if (index == dragged_) return;
    initialStickyBoundary_ = index >= 0 ? stickyBoundary() : std::nullopt;
    dragged_ = index;
    if (index < 0) { liftedPointer_.reset(); liftElevation_.animateTo(0, Motion::SurfaceLower, animationsEnabled()); }
    relayout(true);
}

void TabBar::setDraggedFolder(QUuid folder)
{
    int index = -1;
    for (int at = 0; at < folders_.size(); ++at) if (folders_.at(at).id == folder) index = at;
    if (index == draggedFolder_) return;
    draggedFolder_ = index;
    if (index < 0) liftedPointer_.reset();
    relayout(true);
}
void TabBar::setLifted(QPoint local, QPoint grab)
{
    setLiftedWithAnchor(local, {grab, {}});
}

void TabBar::setLiftedWithAnchor(QPoint local, const DragAnchor& grab)
{
    if (local.isNull()) {
        if (!liftedPointer_) return;
        liftedPointer_.reset(); liftElevation_.animateTo(0, Motion::SurfaceLower, animationsEnabled());
        syncAttachment(); surfaceFrameChanged(); return;
    }
    if (!liftedPointer_) liftElevation_.animateTo(1, Motion::SurfaceLift, animationsEnabled());
    liftedPointer_ = local; liftedGrab_ = grab;
    syncAttachment(); surfaceFrameChanged();
}

void TabBar::updateLiftedLayer()
{
    // The logical strip still constrains the label. Its elevation must not
    // inherit a narrow column's resting mask or stop at the strip's edge.
    auto* owner = window();
    const bool shown = !settling_.isEmpty() || (liftedPointer_ && (dragged_ >= 0 || draggedFolder_ >= 0)) || !incomingVisual_.empty()
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
        for (auto it = settling_.cbegin(); it != settling_.cend(); ++it)
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
    if (incomingVisual_.empty() && visual.empty()) return;
    const bool entering = incomingVisual_.empty() && !visual.empty();
    const bool leaving = !incomingVisual_.empty() && visual.empty();
    incomingVisual_ = std::move(visual); incomingPointer_ = local;
    incomingReorderOffset_.reset();
    if (!incomingVisual_.empty() && !layout_.gap.isEmpty()) {
        QRect gap;
        for (const auto& cell : layout_.gap) gap |= cell;
        incomingReorderOffset_ = incomingRectAt(local).center() - (gap.center() + tabOrigin());
    }
    if (leaving) incomingPainter_.clear();
    if (entering || leaving) liftElevation_.animateTo(entering ? 1 : 0,
        entering ? Motion::SurfaceLift : Motion::SurfaceLower, animationsEnabled());
    syncAttachment(); surfaceFrameChanged();
}

TabDragArea TabBar::dragArea() const
{
    return {size(), tabOrigin(), metrics_.rowHeight, metrics_.verticalInset};
}

QRect TabBar::incomingRect() const
{
    return incomingRectAt(incomingPointer_);
}

QRect TabBar::incomingRectAt(QPoint local) const
{
    if (incomingVisual_.empty() || incomingVisual_.size.isEmpty()) return {};
    const bool tile = vertical() && dropSticky_ && compactStickyTiles();
    auto grab = incomingVisual_.anchor;
    if (incomingVisual_.sourceAnchor) {
        const auto reference = incomingVisual_.active.isEmpty()
            ? QRect(QPoint(), incomingVisual_.size) : incomingVisual_.active;
        grab = reference.topLeft() + incomingVisual_.sourceAnchor->forSize(reference.size(), tile);
    }
    const auto constraint = tile ? TabDragConstraint::CompactTile
        : vertical() ? TabDragConstraint::VerticalRow : TabDragConstraint::HorizontalRow;
    return tabDragRect(layout_, dragArea(), local, incomingVisual_.size, grab, constraint);
}

QRect TabBar::liftedRect() const
{
    return liftedPointer_ ? liftedRectAt(*liftedPointer_) : QRect{};
}

QRect TabBar::liftedRectAt(QPoint local) const
{
    if (liftedPointer_ && draggedFolder_ >= 0 && draggedFolder_ < folders_.size()) {
        // A carried folder: its header and, open, its tabs, as one block.
        const auto& folder = folders_.at(draggedFolder_);
        const int tabs = folder.collapsed ? 0 : folder.count;
        if (vertical()) {
            const int rows = 1 + tabs;
            const int height = rows * metrics_.rowHeight + (rows - 1) * metrics_.rowGap;
            const int width = std::max(0, this->width() - 2 * metrics_.verticalInset);
            const auto grab = liftedGrab_.forSize({width, metrics_.rowHeight});
            return tabDragRect(layout_, dragArea(), local, {width, height}, grab,
                TabDragConstraint::VerticalRow);
        }
        int extent = folderHeaderExtent(folder);
        for (int index = folder.first; index < folder.first + tabs && index < tabs_.size(); ++index) extent += tabExtent(index, false);
        const auto grab = liftedGrab_.forSize({folderHeaderExtent(folder), metrics_.rowHeight});
        return tabDragRect(layout_, dragArea(), local, {extent, metrics_.rowHeight}, grab,
            TabDragConstraint::HorizontalRow);
    }
    if (!liftedPointer_ || dragged_ < 0 || dragged_ >= tabs_.size()) return {};
    const bool sticky = dropIndex_ >= 0 ? dropSticky_ : dragged_ < stickyCount_;
    if (vertical()) {
        const int rowWidth = std::max(0, width() - 2 * metrics_.verticalInset);
        if (sticky && compactStickyTiles()) {
            // The gap is laid out with the target section's tile dimensions.
            // Its position is not the pointer's; only its size is reused.
            const auto size = !layout_.gap.isEmpty() ? layout_.gap.first().size() : layout_.tabs.value(dragged_).size();
            return tabDragRect(layout_, dragArea(), local, size, QRect(QPoint(), size).center(),
                TabDragConstraint::CompactTile);
        }
        const auto grab = liftedGrab_.forSize({rowWidth, metrics_.rowHeight});
        return tabDragRect(layout_, dragArea(), local, {rowWidth, metrics_.rowHeight}, grab,
            TabDragConstraint::VerticalRow);
    }
    const int extent = sticky && appearance_.stickySizing == StickyTabSizing::Compact ? uiPixels(compactStickyTabWidth) : tabExtent(dragged_, sticky);
    // Local and incoming labels use the same scrolled row grid, including
    // a partially visible final row and a bottom strip's shadow origin.
    const auto grab = liftedGrab_.forSize({extent, metrics_.rowHeight},
        sticky && appearance_.stickySizing == StickyTabSizing::Compact);
    return tabDragRect(layout_, dragArea(), local, {extent, metrics_.rowHeight}, grab,
        TabDragConstraint::HorizontalRow);
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
    const auto held = !incomingVisual_.empty() ? incomingRectAt(local) : liftedPointer_ ? liftedRectAt(local) : QRect{};
    if (!held.isEmpty() && draggedFolder_ >= 0) {
        // A local folder keeps its existing header-based insertion probe;
        // only individual tabs adopt the edge-over-center reorder rule.
        if (vertical()) local.setY(held.top() + metrics_.rowHeight / 2);
        else local.setX(held.left() + metrics_.rowHeight / 2);
    }
    if (dropIndex_ >= 0 && !held.isEmpty() && !layout_.gap.isEmpty())
        return tabReorderAt(input, layout_, held.translated(-tabOrigin()), local - tabOrigin(), stickyReach, section,
            !incomingVisual_.empty() ? incomingReorderOffset_ : std::nullopt);
    // A bottom strip grows upward for a temporary gap, while the stable
    // no-gap layout used to pick the slot stays next to the content.
    if (placement_ == TabBarPlacement::Bottom && !overlayActive())
        local.ry() -= std::max(0, height() - reservedSize(available_).height());
    return tabInsertionAt(input, local - tabOrigin(), stickyReach, section);
}

void TabBar::setDropPreview(int index, bool sticky, QVector<int> block, int folder)
{
    if (index < 0) { block.clear(); sticky = false; index = -1; folder = -1; incomingReorderOffset_.reset(); }
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
    TabPaintState state;
    state.shape = shapeOf(index);
    state.selected = index == current_ && incomingVisual_.empty();
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
    state.preset = tabPreset(index);
    state.number = appearance_.showNumbers ? QString::number(index + 1) : QString{};
    state.icon = appearance_.showIcons ? tabs_.at(index).icon : QIcon{};
    state.title = tabs_.at(index).title;
    state.titleLayout = &tabs_[index].titleLayout;
    // A sticky tab always shows its action; the others on hover.
    state.actionShown = hasLabelAction(index) && (state.sticky || (!carriedDisplay_ && (index == pointer_.pressedAction()
        || (index == fadingAction_ && actionOpacity_.value() > 0))));
    state.actionHovered = !carriedDisplay_ && index == hover_.hit().tab && hover_.hit().action;
    state.actionPressed = !carriedDisplay_ && index == pointer_.pressedAction();
    state.actionReserved = state.sticky;
    state.actionOpacity = state.sticky || index == pointer_.pressedAction() ? 1.0 : actionOpacity_.value();
    state.actionIcon = iconFor(!state.sticky ? QStringLiteral("close") : vertical() ? QStringLiteral("sticky-vertical") : QStringLiteral("sticky"), theme_);
    return state;
}

void TabBar::paintTabAt(QPainter& painter, int index, bool settling)
{
    if (!settling && settling_.contains(tabData(index).toUuid())) return;
    const auto rect = animatedRect(index);
    if (rect.isEmpty() || painter.hasClipping() && !painter.clipBoundingRect().intersects(QRectF(rect).adjusted(-metrics_.controlRadius, 0, metrics_.controlRadius, 0))) return;
    auto state = paintState(index);
    const int lane = layout_.lanes.value(index, -1);
    const QRect visible = settling ? rect : lane < 0 ? QRect{}
        : rect.intersected(layout_.laneList.at(lane).viewport.translated(tabOrigin())).intersected(this->rect());
    const auto geometry = anchorGeometry(rect, visible, state.shape == TabShape::Tile, false);
    state.surface = state.selected && !visible.isEmpty() ? &geometry : nullptr;
    state.elevationManaged = !surfaceBody_.isEmpty();
    auto label = TabPainter::captureLabel(partsOf(index, rect), state, painter.font(), painter.layoutDirection());
    label.presence = presenceFor(index, rect);
    painter_.paintLabel(painter, label, state.titleLayout);
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
    state.addIcon = iconFor(QStringLiteral("add"), theme_);
    return state;
}
void TabBar::paintFolders(QPainter& painter, int lane)
{
    const auto origin = tabOrigin();
    for (int folder = 0; folder < folders_.size(); ++folder) {
        if (folder == draggedFolder_ || layout_.headerLanes.value(folder, -1) != lane) continue;
        painter_.paintFolderHeader(painter, layout_.headers.at(folder).translated(origin), vertical(), folderPaintState(folder));
    }
}
void TabBar::paintFolderLines(QPainter& painter, int lane)
{
    const auto origin = tabOrigin();
    const bool bottom = placement_ == TabBarPlacement::Bottom;
    for (int folder = 0; folder < folders_.size(); ++folder) {
        if (folder == draggedFolder_) continue;
        const auto state = folderPaintState(folder);
        const auto& record = folders_.at(folder);
        const bool headerHere = layout_.headerLanes.value(folder, -1) == lane;
        // The folder's shown tabs in this lane, row by row.
        QVector<QRect> rows;
        if (headerHere) rows.append(layout_.headers.at(folder));
        for (int index = record.first; index < record.first + record.count && index < tabs_.size(); ++index) {
            if (index == dragged_ || layout_.lanes.value(index, -1) != lane) continue;
            const auto rect = layout_.tabs.at(index);
            auto row = std::find_if(rows.begin(), rows.end(), [&](const QRect& r) { return vertical() || r.top() == rect.top(); });
            if (row == rows.end()) rows.append(rect); else *row |= rect;
        }
        // A collapsed folder showing only its header has no line.
        if (record.collapsed && rows.size() <= 1 && (rows.isEmpty() || (headerHere && rows.first() == layout_.headers.at(folder)))) continue;
        for (auto row : rows) {
            row.translate(origin);
            if (vertical()) {
                // A side line in the indent, along the folder's rows.
                auto span = row;
                if (headerHere) span.setTop(layout_.headers.at(folder).translated(origin).bottom() + 1 + metrics_.rowGap);
                if (span.height() <= 0 || record.collapsed) continue;
                const int x = layout_.headers.value(folder).isNull() ? span.left() : layout_.headers.at(folder).translated(origin).left() + metrics_.folderIndent / 2 - metrics_.folderLine / 2;
                painter_.paintFolderLine(painter, QRect(x, span.top(), metrics_.folderLine, span.height()), state.color);
            } else {
                // A line along the content edge, starting under the chip.
                const int start = headerHere && row.left() == layout_.headers.at(folder).translated(origin).left()
                    ? painter_.folderChipRect(layout_.headers.at(folder).translated(origin)).left() : row.left();
                const int y = bottom ? row.top() : row.bottom() + 1 - metrics_.folderLine;
                QRect line(start, y, row.right() + 1 - start, metrics_.folderLine);
                // The current tab opens into the content; its outline meets the line.
                const auto current = folderOf(current_) == folder && layout_.lanes.value(current_, -1) == lane
                    ? animatedRect(current_) : QRect{};
                if (!current.isEmpty() && current.intersects(line.adjusted(0, -1, 0, 1))) {
                    painter_.paintFolderLine(painter, QRect(line.left(), y, current.left() - line.left(), metrics_.folderLine), state.color);
                    painter_.paintFolderLine(painter, QRect(current.right() + 1, y, line.right() - current.right(), metrics_.folderLine), state.color);
                } else painter_.paintFolderLine(painter, line, state.color);
            }
        }
    }
}
void TabBar::paintFolderOutline(QPainter& painter, int lane)
{
    const int folder = folderOf(current_);
    if (folder < 0 || folder == draggedFolder_ || current_ == dragged_ || layout_.lanes.value(current_, -1) != lane) return;
    const auto state = folderPaintState(folder);
    painter_.paintFolderOutline(painter, animatedRect(current_), shapeOf(current_), state.color);
}
void TabBar::paintLaneDecorations(QPainter& painter)
{
    const auto origin = tabOrigin();
    for (int laneIndex = 0; laneIndex < layout_.laneList.size(); ++laneIndex) {
        const auto& lane = layout_.laneList.at(laneIndex);
        auto viewport = lane.viewport.translated(origin);
        if (lane.vertical) viewport.adjust(1, 0, -1, 0);
        const bool showBar = lane.maximumOffset() > 0 && laneIndex == scrollingLane_ && scrollbarOpacity_.value() > 0;
        if (showBar) {
            painter.save(); painter.setOpacity(painter.opacity() * scrollbarOpacity_.value());
            painter_.paintScrollbar(painter, scrollThumb(laneIndex), laneIndex == scrollDragLane_);
            painter.restore();
        }
    }
    if (!layout_.separator.isNull()) painter_.paintSeparator(painter, layout_.separator.translated(origin),
        window()->isActiveWindow(), vertical());
}

void TabBar::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setFont(font());
    // An overlay's shadow band stays unfilled, so the content shows through.
    auto background = rect();
    if (overlayActive() && !vertical()) {
        if (placement_ == TabBarPlacement::Bottom) background.setTop(rowShadowExtent());
        else background.setBottom(height() - rowShadowExtent() - 1);
    }
    if (overlayActive() && !vertical()) painter.fillRect(background, painter_.palette().strip);
    const auto origin = tabOrigin();
    // Short lines between resting tabs of one row.
    for (int index = 0; index + 1 < tabs_.size(); ++index) {
        const int next = index + 1;
        if (vertical() || layout_.lanes.at(index) < 0 || layout_.lanes.at(index) != layout_.lanes.at(next)) continue;
        if (index == current_ || next == current_ || index == hover_.hit().tab || next == hover_.hit().tab || tabPreset(index) || tabPreset(next)) continue;
        if (slides_.contains(tabs_.at(index).data.toUuid()) || slides_.contains(tabs_.at(next).data.toUuid())) continue;
        const auto a = layout_.visibleRect(index), b = layout_.visibleRect(next);
        if (a.isEmpty() || b.isEmpty() || a.top() != b.top() || a.right() + 1 != b.left()) continue;
        const int height = metrics_.rowHeight / 2;
        painter_.paintSeparator(painter, QRect(b.left() + origin.x(), a.top() + origin.y() + (a.height() - height) / 2, metrics_.separator, height));
    }
    for (int laneIndex = 0; laneIndex < layout_.laneList.size(); ++laneIndex) {
        const auto& lane = layout_.laneList.at(laneIndex);
        QVector<EdgeFade> fades;
        const auto appendFades = [&](const TabLayoutLane& segment) {
            const auto viewport = segment.viewport.translated(origin);
            const int fade = segment.fadeExtent(metrics_.edgeFade);
            if (segment.offset > 0) fades.append({segment.vertical
                ? QRect(viewport.left(), viewport.top(), viewport.width(), fade)
                : QRect(viewport.left() + segment.frozen, viewport.top(), fade, viewport.height()),
                segment.vertical ? Qt::TopEdge : Qt::LeftEdge});
            if (segment.offset < segment.maximumOffset()) fades.append({segment.vertical
                ? QRect(viewport.left(), viewport.bottom() - fade + 1, viewport.width(), fade)
                : QRect(viewport.right() - fade + 1, viewport.top(), fade, viewport.height()),
                segment.vertical ? Qt::BottomEdge : Qt::RightEdge});
        };
        appendFades(lane);
        if (layout_.rowScrollLane >= 0 && layout_.rowScrollLane != laneIndex)
            appendFades(layout_.laneList.at(layout_.rowScrollLane));
        if (!fades.isEmpty() && layout_.lanes.value(current_, -1) == laneIndex
            && incomingVisual_.empty() && !liftedPointer_ && liftElevation_.value() <= 0) {
            const auto area = animatedRect(current_);
            const auto visible = area.intersected(lane.viewport.translated(origin)).intersected(rect());
            const auto geometry = anchorGeometry(area, visible, shapeOf(current_) == TabShape::Tile, false);
            if (geometry.joined) {
                const auto joint = vertical() ? (placement_ == TabBarPlacement::Left ? Qt::RightEdge : Qt::LeftEdge)
                    : placement_ == TabBarPlacement::Top ? Qt::BottomEdge : Qt::TopEdge;
                auto surface = QRectF(area);
                // Complete the fade before the separately painted connection
                // leaves this widget. The content itself remains opaque.
                if (joint == Qt::RightEdge) surface.setRight(width());
                else if (joint == Qt::LeftEdge) surface.setLeft(0);
                else if (joint == Qt::BottomEdge) surface.setBottom(height());
                else surface.setTop(0);
                for (auto& fade : fades) if (fade.area.intersects(visible)) fade.convergeTo(joint, surface);
            }
        }
        const auto drawLane = [&](QPainter& painter) {
            painter.save();
            painter.setClipRect(lane.viewport.translated(origin), Qt::IntersectClip);
            // Tabs scrolling under a frozen run first, then the run; the current
            // tab of each part over its neighbors.
            const auto paintRange = [&](int first, int last, bool clipFrozen) {
                painter.save();
                if (clipFrozen && lane.frozen)
                    painter.setClipRect(lane.viewport.translated(origin).adjusted(lane.frozen, 0, 0, 0), Qt::IntersectClip);
                for (int index = first; index < last; ++index)
                    if (index != current_ && layout_.lanes.at(index) == laneIndex) paintTabAt(painter, index);
                if (current_ >= first && current_ < last && layout_.lanes.value(current_) == laneIndex) paintTabAt(painter, current_);
                painter.restore();
            };
            // Folders live among the regular tabs, under a frozen run.
            painter.save();
            if (lane.frozen) painter.setClipRect(lane.viewport.translated(origin).adjusted(lane.frozen, 0, 0, 0), Qt::IntersectClip);
            paintFolders(painter, laneIndex);
            painter.restore();
            if (lane.frozen) { paintRange(stickyCount_, count(), true); paintRange(0, stickyCount_, false); }
            else paintRange(0, count(), false);
            painter.save();
            if (lane.frozen) painter.setClipRect(lane.viewport.translated(origin).adjusted(lane.frozen, 0, 0, 0), Qt::IntersectClip);
            paintFolderLines(painter, laneIndex);
            paintFolderOutline(painter, laneIndex);
            painter.restore();
            painter.restore();
        };
        paintEdgeFades(painter, lane.viewport.translated(origin), fades, drawLane);
    }
    paintLaneDecorations(painter);
    const auto globalOrigin = mapToGlobal(QPoint());
    for (const auto& label : std::as_const(departing_))
        paintRetiredLabel(painter, painter_, label, globalOrigin, 1 - presenceProgress_.value());
    if (overlayActive() && !vertical()) {
        // The overlay reads as the strip grown over the content.
        const bool above = placement_ == TabBarPlacement::Bottom;
        const int shadow = rowShadowExtent();
        const QRect band(0, above ? 0 : height() - shadow, width(), shadow);
        surfaceRenderer_.paintOverlayBand(painter, band, above, painter_.palette().elevation, rowsReveal_.value());
    }
}

void TabBar::paintLiftedTabs(QPainter& painter)
{
    painter.setFont(font());
    const auto bodyProfile = interpolateElevation(elevationProfile(ElevationLevel::BodyRest),
        elevationProfile(ElevationLevel::BodyRaised), surfaceFocus_);
    auto style = painter_.palette().elevation;
    style.focused = groupFocused_ && window()->isActiveWindow();
    for (const auto& anchor : surfaceAnchors()) if (anchor.lifted || anchor.foreground)
        LabelPresenceEffect::paint(painter, anchor.presence, [&] { surfaceRenderer_.paintAnchor(painter, anchor.geometry,
            anchor.foreground ? elevationProfile(ElevationLevel::Flat) : bodyProfile,
            anchor.elevation, style, !anchor.foreground); });
    if (const auto incoming = incomingRect(); !incoming.isEmpty()) {
        const auto anchors = surfaceAnchors();
        const auto geometry = anchors.isEmpty() ? SurfaceGeometry{} : translatedSurfaceGeometry(anchors.first().geometry, -incoming.topLeft());
        painter.save(); painter.translate(incoming.topLeft());
        incomingPainter_.paint(painter, incomingVisual_, &geometry);
        painter.restore();
    }
    if (const auto lifted = liftedRect(); !lifted.isEmpty() && draggedFolder_ >= 0) {
        // The carried folder rides above everything: header, tabs, its line.
        const auto& folder = folders_.at(draggedFolder_);
        const auto state = folderPaintState(draggedFolder_);

        const int pitch = metrics_.rowHeight + metrics_.rowGap;
        QRect header = vertical() ? QRect(lifted.left(), lifted.top(), lifted.width(), metrics_.rowHeight)
                                  : QRect(lifted.left(), lifted.top(), folderHeaderExtent(folder), metrics_.rowHeight);
        painter_.paintFolderHeader(painter, header, vertical(), state);
        int x = header.right() + 1, y = header.top() + pitch;
        const int tabs = folder.collapsed ? 0 : folder.count;
        for (int index = folder.first; index < folder.first + tabs && index < tabs_.size(); ++index) {
            const QRect rect = vertical() ? QRect(lifted.left() + metrics_.folderIndent, y, lifted.width() - metrics_.folderIndent, metrics_.rowHeight)
                                          : QRect(x, lifted.top(), tabExtent(index, false), metrics_.rowHeight);
            TabPaintState tab;
            tab.shape = shapeOf(index); tab.selected = index == current_; tab.groupFocused = true;
            tab.outerRight = placement_ == TabBarPlacement::Right; tab.preset = tabPreset(index);
            tab.icon = appearance_.showIcons ? tabs_.at(index).icon : QIcon{};
            tab.title = tabs_.at(index).title; tab.titleLayout = &tabs_[index].titleLayout;
            const auto geometry = anchorGeometry(rect, rect.intersected(this->rect()), false, true);
            tab.elevationManaged = true; tab.surface = tab.selected ? &geometry : nullptr;
            const auto label = TabPainter::captureLabel(labelGeometry(rect, tab.shape, labelParts(index, false), false, false, false),
                tab, painter.font(), painter.layoutDirection());
            painter_.paintLabel(painter, label, tab.titleLayout);
            x = rect.right() + 1; y += pitch;
        }
        if (tabs && !vertical()) {
            const int line = placement_ == TabBarPlacement::Bottom ? lifted.top() : lifted.bottom() + 1 - metrics_.folderLine;
            const int start = painter_.folderChipRect(header).left();
            painter_.paintFolderLine(painter, QRect(start, line, lifted.right() + 1 - start, metrics_.folderLine), state.color);
        } else if (tabs) {
            const int x0 = lifted.left() + metrics_.folderIndent / 2 - metrics_.folderLine / 2;
            painter_.paintFolderLine(painter, QRect(x0, header.bottom() + 1 + metrics_.rowGap, metrics_.folderLine, lifted.bottom() - header.bottom() - metrics_.rowGap), state.color);
        }
    } else if (const auto movingLabel = liftedRect(); !movingLabel.isEmpty()) {
        // The dragged tab rides above everything, with its shadow.
        TabPaintState state;
        state.shape = shapeOf(dragged_);
        state.lifted = true; state.selected = true; state.groupFocused = true;
        state.shadowOpacity = liftProgress();
        state.sticky = dropIndex_ >= 0 ? dropSticky_ : dragged_ < stickyCount_;
        if (vertical()) state.shape = state.sticky && compactStickyTiles() ? TabShape::Tile : TabShape::Row;
        state.outerRight = placement_ == TabBarPlacement::Right;
        state.preset = tabPreset(dragged_);
        state.number = appearance_.showNumbers ? QString::number(dragged_ + 1) : QString{};
        state.icon = appearance_.showIcons ? tabs_.at(dragged_).icon : QIcon{};
        state.title = tabs_.at(dragged_).title;
        state.titleLayout = &tabs_[dragged_].titleLayout;
        state.iconOnly = state.shape == TabShape::Tile || (vertical() && iconsOnly() && columnReveal_ <= 0)
            || (!vertical() && state.sticky && appearance_.stickySizing == StickyTabSizing::Compact);
        state.actionShown = state.sticky && !state.iconOnly; state.actionReserved = true;
        state.actionIcon = iconFor(vertical() ? QStringLiteral("sticky-vertical") : QStringLiteral("sticky"), theme_);
        const auto geometry = anchorGeometry(movingLabel, movingLabel.intersected(rect()), state.shape == TabShape::Tile, true);
        state.surface = &geometry; state.elevationManaged = true; state.bodyElevation = &bodyProfile;
        const auto label = labelParts(dragged_, state.sticky);
        const auto presentation = TabPainter::captureLabel(
            labelGeometry(movingLabel, state.shape, label, state.iconOnly, state.actionShown, state.sticky),
            state, painter.font(), painter.layoutDirection());
        painter_.paintLabel(painter, presentation, state.titleLayout);
    }
    for (auto it = settling_.cbegin(); it != settling_.cend(); ++it)
        if (it.value() >= 0 && it.value() < count() && tabData(it.value()).toUuid() == it.key())
            paintTabAt(painter, it.value(), true);
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
        if (self && appearance_.rowExpansion == TabRowExpansion::Overlay && expanded_) setExpanded(false);
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
    if (iconMode_ && rect().contains(point)) setColumnHovered(true);
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
    if (expandedByWheel_) collapseTimer_.start();
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
    const auto decision = tabKeyboardDecision(event->key(), current_, count(), vertical(), expanded_);
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
    if (expanded_ && appearance_.rowExpansion == TabRowExpansion::Overlay) {
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
        QTimer::singleShot(0, this, [this] { refreshScrollInput(); if (iconMode_) reconcileColumnHover(); });
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
