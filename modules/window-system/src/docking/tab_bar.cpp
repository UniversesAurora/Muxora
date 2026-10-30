// SPDX-License-Identifier: MIT
#include <window_system/interface_appearance.hpp>
#include "tab_bar.hpp"
#include "style/smooth_scroll.hpp"
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

int TabBar::addTab(const QIcon& icon, const QString& title)
{
    prepareController();
    auto result = controller_.addTab(icon, title);
    synchronizeController();
    return result;
}

void TabBar::removeTab(int index)
{
    prepareController();
    controller_.removeTab(index);
    synchronizeController();
}

QVariant TabBar::tabData(int index) const
{
    return controller_.tabData(index);
}

void TabBar::setTabData(int index, const QVariant& value)
{
    prepareController();
    controller_.setTabData(index, value);
    synchronizeController();
}

QString TabBar::tabText(int index) const
{
    return controller_.tabText(index);
}

void TabBar::setTabText(int index, const QString& text)
{
    prepareController();
    controller_.setTabText(index, text);
    synchronizeController();
}

QIcon TabBar::tabIcon(int index) const
{
    return controller_.tabIcon(index);
}

void TabBar::setTabIcon(int index, const QIcon& icon)
{
    prepareController();
    controller_.setTabIcon(index, icon);
    synchronizeController();
}

QString TabBar::tabToolTip(int index) const
{
    return controller_.tabToolTip(index);
}

void TabBar::setTabToolTip(int index, const QString& tip)
{
    prepareController();
    controller_.setTabToolTip(index, tip);
    synchronizeController();
}

void TabBar::setTabMetadata(int index, const TabRecord& tab)
{
    prepareController();
    controller_.setTabMetadata(index, tab);
    synchronizeController();
}

std::shared_ptr<const TabLabelContent> TabBar::labelContent(int index) const
{
    return controller_.labelContent(index);
}

const TabColorPreset* TabBar::tabPreset(int index) const
{
    return controller_.tabPreset(index);
}

int TabBar::indexOf(TabId tab) const
{
    return controller_.indexOf(tab);
}

void TabBar::setCurrentIndex(int index)
{
    prepareController();
    controller_.setCurrentIndex(index);
    synchronizeController();
}

TabBar::SurfaceState TabBar::captureSurfaceState() const
{
    return controller_.captureSurfaceState();
}

void TabBar::inheritSurface(const SurfaceState& previous, bool animate)
{
    prepareController();
    controller_.inheritSurface(previous, animate);
    synchronizeController();
}

void TabBar::setSurfaceContext(QRectF body, qreal focus, qreal join)
{
    prepareController();
    controller_.setSurfaceContext(body, focus, join);
    synchronizeController();
}

LabelPresencePose TabBar::labelPresence(int index) const
{
    return controller_.labelPresence(index);
}

QVector<TabSurfaceAnchor> TabBar::surfaceAnchors(QPointF offset) const
{
    return controller_.surfaceAnchors(offset);
}

TabSurfaceExtension TabBar::surfaceExtension(QPointF offset) const
{
    return controller_.surfaceExtension(offset);
}

void TabBar::setTheme(const Theme& theme)
{
    prepareController();
    controller_.setTheme(theme);
    synchronizeController();
}

void TabBar::setAppearance(const TabAppearance& appearance, TabBarPlacement placement)
{
    prepareController();
    controller_.setAppearance(appearance, placement);
    synchronizeController();
}

void TabBar::setStickyCount(int count)
{
    prepareController();
    controller_.setStickyCount(count);
    synchronizeController();
}

void TabBar::setFolders(QVector<TabStripFolder> folders)
{
    prepareController();
    controller_.setFolders(std::move(folders));
    synchronizeController();
}

int TabBar::folderOf(int index) const
{
    return controller_.folderOf(index);
}

QRect TabBar::folderHeaderRect(int folder) const
{
    return controller_.folderHeaderRect(folder);
}

int TabBar::folderAt(QPoint local) const
{
    return controller_.folderAt(local);
}

const TabColorPreset* TabBar::folderPreset(int folder) const
{
    return controller_.folderPreset(folder);
}

int TabBar::folderHeaderExtent(const TabStripFolder& folder) const
{
    return controller_.folderHeaderExtent(folder);
}

void TabBar::setGroupFocused(bool focused)
{
    prepareController();
    controller_.setGroupFocused(focused);
    synchronizeController();
}

void TabBar::setIconMode(bool iconMode)
{
    prepareController();
    controller_.setIconMode(iconMode);
    synchronizeController();
}

void TabBar::setColumnReveal(qreal reveal, bool modeTransition)
{
    prepareController();
    controller_.setColumnReveal(reveal, modeTransition);
    synchronizeController();
}

void TabBar::setColumnHovered(bool hovered)
{
    prepareController();
    controller_.setColumnHovered(hovered);
    synchronizeController();
}

void TabBar::restoreColumnPeek(bool peeked)
{
    prepareController();
    controller_.restoreColumnPeek(peeked);
    synchronizeController();
}

TabVisualState TabBar::captureVisualState()
{
    prepareController();
    auto result = controller_.captureVisualState();
    synchronizeController();
    return result;
}

void TabBar::animatePresence(const QSet<QUuid>& ids, bool visible, Motion motion)
{
    prepareController();
    controller_.animatePresence(ids, visible, motion);
    synchronizeController();
}

TabVisualState TabBar::takeSeparateDepartures(TabVisualState& previous) const
{
    return controller_.takeSeparateDepartures(previous);
}

void TabBar::transitionFrom(TabVisualState previous, Motion motion)
{
    prepareController();
    controller_.transitionFrom(std::move(previous), motion);
    synchronizeController();
}

LabelPositions TabBar::captureDropPositions(std::optional<LabelPositions> carrierPositions) const
{
    return controller_.captureDropPositions(carrierPositions);
}

void TabBar::settleFrom(const LabelPositions& globalPositions)
{
    prepareController();
    controller_.settleFrom(globalPositions);
    synchronizeController();
}

void TabBar::refreshTrailing()
{
    prepareController(true);
    controller_.refreshTrailing();
    synchronizeController();
}

int TabBar::tabExtent(int index, bool sticky) const
{
    return controller_.tabExtent(index, sticky);
}

int TabBar::carriedExtent(const CarriedLabel& label, bool sticky) const
{
    return controller_.carriedExtent(label, sticky);
}

int TabBar::naturalTabWidth(int index) const
{
    return controller_.naturalTabWidth(index);
}

bool TabBar::wrappedRows() const
{
    return controller_.wrappedRows();
}

bool TabBar::collapsible() const
{
    return controller_.collapsible();
}

bool TabBar::overlayActive() const
{
    return controller_.overlayActive();
}

QPoint TabBar::tabOrigin() const
{
    return controller_.tabOrigin();
}

int TabBar::minimumColumnWidth() const
{
    return controller_.minimumColumnWidth();
}

QSize TabBar::reservedSize(QSize available) const
{
    return controller_.reservedSize(available);
}

QSize TabBar::displaySize(QSize available) const
{
    return controller_.displaySize(available);
}

void TabBar::setAvailableSize(QSize available)
{
    prepareController();
    controller_.setAvailableSize(available);
    synchronizeController();
}

QRect TabBar::tabRect(int index) const
{
    return controller_.tabRect(index);
}

void TabBar::setCarriedDisplay(bool carried)
{
    prepareController();
    controller_.setCarriedDisplay(carried);
    synchronizeController();
}

void TabBar::setCarriedPresentation(std::optional<VerticalPresentation> presentation)
{
    prepareController();
    controller_.setCarriedPresentation(presentation);
    synchronizeController();
}

void TabBar::setCarriedTileColumns(int columns)
{
    prepareController();
    controller_.setCarriedTileColumns(columns);
    synchronizeController();
}

QRegion TabBar::tabRegion(int index) const
{
    return controller_.tabRegion(index);
}

int TabBar::tabAt(QPoint local) const
{
    return controller_.tabAt(local);
}

QRect TabBar::titleRect(int index) const
{
    return controller_.titleRect(index);
}

QRect TabBar::actionRect(int index) const
{
    return controller_.actionRect(index);
}

int TabBar::scrollOffset() const
{
    return controller_.scrollOffset();
}

void TabBar::restoreScrollOffsets(int regular, int sticky, int rows)
{
    prepareController();
    controller_.restoreScrollOffsets(regular, sticky, rows);
    synchronizeController();
}

void TabBar::setScrollOffset(int offset)
{
    prepareController();
    controller_.setScrollOffset(offset);
    synchronizeController();
}

int TabBar::maximumScrollOffset() const
{
    return controller_.maximumScrollOffset();
}

QRect TabBar::scrollViewportAt(QPoint local) const
{
    return controller_.scrollViewportAt(local);
}

bool TabBar::scrollsHorizontallyAt(QPoint local) const
{
    return controller_.scrollsHorizontallyAt(local);
}

QRect TabBar::scrollViewport() const
{
    return controller_.scrollViewport();
}

int TabBar::frozenExtent() const
{
    return controller_.frozenExtent();
}

QRect TabBar::contentTabRect(int index) const
{
    return controller_.contentTabRect(index);
}

bool TabBar::scrollsHorizontally() const
{
    return controller_.scrollsHorizontally();
}

bool TabBar::isOverflowing() const
{
    return controller_.isOverflowing();
}

void TabBar::ensureVisible(int index)
{
    prepareController();
    controller_.ensureVisible(index);
    synchronizeController();
}

bool TabBar::fullyVisible(int index) const
{
    return controller_.fullyVisible(index);
}

void TabBar::setExpanded(bool expanded, bool byWheel, bool animate)
{
    prepareController();
    controller_.setExpanded(expanded, byWheel, animate);
    synchronizeController();
}

void TabBar::setDragExpanded(bool expanded)
{
    prepareController();
    controller_.setDragExpanded(expanded);
    synchronizeController();
}

void TabBar::setDraggedTab(TabId tab)
{
    prepareController();
    controller_.setDraggedTab(tab);
    synchronizeController();
}

void TabBar::setDraggedFolder(QUuid folder)
{
    prepareController();
    controller_.setDraggedFolder(folder);
    synchronizeController();
}

void TabBar::setLifted(QPoint local, QPoint grab)
{
    prepareController();
    controller_.setLifted(local, grab);
    synchronizeController();
}

void TabBar::setLiftedWithAnchor(QPoint local, const DragAnchor& grab)
{
    prepareController();
    controller_.setLiftedWithAnchor(local, grab);
    synchronizeController();
}

QRect TabBar::carrierBounds() const
{
    return controller_.carrierBounds();
}

std::shared_ptr<const TabCarrierFrame> TabBar::carrierFrame()
{
    prepareController();
    auto result = controller_.carrierFrame();
    synchronizeController();
    return result;
}

QPoint TabBar::carrierAnchor() const
{
    return controller_.carrierAnchor();
}

void TabBar::setIncomingVisual(TabDragVisual visual, QPoint local)
{
    prepareController();
    controller_.setIncomingVisual(std::move(visual), local);
    synchronizeController();
}

QRect TabBar::incomingRect() const
{
    return controller_.incomingRect();
}

QRect TabBar::liftedRect() const
{
    return controller_.liftedRect();
}

TabInsertion TabBar::insertionAt(QPoint local, int stickyReach, TabLayoutInput::Carried carried) const
{
    return controller_.insertionAt(local, stickyReach, carried);
}

void TabBar::setDropPreview(int index, bool sticky, QVector<int> block, int folder)
{
    prepareController();
    controller_.setDropPreview(index, sticky, std::move(block), folder);
    synchronizeController();
}

QRect TabBar::dropPreviewRect() const
{
    return controller_.dropPreviewRect();
}

void TabBar::paintEvent(QPaintEvent*)
{
    QPainter painter(this); painter.setFont(font());
    const auto frame = controller_.captureStripFrame(painter.font(), painter.layoutDirection());
    if (!frame->background.isEmpty()) painter.fillRect(frame->background, frame->style->palette.strip);
    for (const auto& area : frame->separators) controller_.painter().paintSeparator(painter, area);
    const auto paintLabel = [&](QPainter& target, const TabStripLabelSample& sample) {
        if (target.hasClipping() && !target.clipBoundingRect().intersects(
            QRectF(sample.label.parts.tab).adjusted(-metrics().controlRadius, 0, metrics().controlRadius, 0))) return;
        controller_.painter().paintLabel(target, sample.label, &controller_.titleLayout(sample.sourceIndex));
    };
    for (const auto& lane : frame->lanes) {
        const auto drawLane = [&](QPainter& target) {
            target.save(); target.setClipRect(lane.viewport, Qt::IntersectClip);
            target.save(); target.setClipRect(lane.regularClip, Qt::IntersectClip);
            for (const auto& folder : lane.folders) controller_.painter().paintFolderHeader(target, folder.area, frame->vertical, folder.state);
            for (const auto& label : lane.regular) paintLabel(target, label);
            target.restore();
            for (const auto& label : lane.frozen) paintLabel(target, label);
            target.save(); target.setClipRect(lane.regularClip, Qt::IntersectClip);
            for (const auto& line : lane.folderLines) controller_.painter().paintFolderLine(target, line.area, line.color);
            if (lane.folderOutline) {
                const auto& outline = *lane.folderOutline;
                controller_.painter().paintFolderOutline(target, outline.area, outline.shape, outline.color);
            }
            target.restore(); target.restore();
        };
        paintEdgeFades(painter, lane.viewport, lane.fades, drawLane);
    }
    for (const auto& scrollbar : frame->scrollbars) {
        painter.save(); painter.setOpacity(painter.opacity() * scrollbar.opacity);
        controller_.painter().paintScrollbar(painter, scrollbar.thumb, scrollbar.emphasized); painter.restore();
    }
    if (!frame->sectionSeparator.isEmpty())
        controller_.painter().paintSeparator(painter, frame->sectionSeparator, frame->windowActive, frame->vertical);
    departurePainter_.paint(painter, controller_.painter(), frame->departures);
    if (!frame->overlayShadow.isEmpty()) surfaceRenderer_.paintOverlayBand(painter, frame->overlayShadow,
        frame->shadowAbove, frame->style->palette.elevation, frame->shadowOpacity);
}

void TabBar::paintLiftedTabs(QPainter& painter)
{
    painter.setFont(font());
    const auto frame = controller_.captureForegroundFrame(painter.font(), painter.layoutDirection());
    for (const auto& shadow : frame->shadows) LabelPresenceEffect::paint(painter, shadow.presence, [&] {
        surfaceRenderer_.paintAnchor(painter, shadow.geometry, shadow.body, shadow.anchor, frame->elevation, shadow.incremental);
    });
    if (frame->incoming) {
        const auto& incoming = *frame->incoming;
        painter.save(); painter.translate(incoming.origin);
        incomingPainter_.paint(painter, incoming.visual, &incoming.geometry); painter.restore();
    }
    if (frame->folder) controller_.painter().paintFolderHeader(painter, frame->folder->area, frame->vertical, frame->folder->state);
    for (const auto& sample : frame->carried) controller_.painter().paintLabel(painter, sample.label, &controller_.titleLayout(sample.sourceIndex));
    if (frame->folderLine) controller_.painter().paintFolderLine(painter, frame->folderLine->area, frame->folderLine->color);
    for (const auto& sample : frame->settling) {
        if (painter.hasClipping() && !painter.clipBoundingRect().intersects(QRectF(sample.label.parts.tab)
            .adjusted(-metrics().controlRadius, 0, metrics().controlRadius, 0))) continue;
        controller_.painter().paintLabel(painter, sample.label, &controller_.titleLayout(sample.sourceIndex));
    }
}

void TabBar::trackColumnPopup()
{
    if (columnPopup_ && columnPopup_->isVisible()) return;
    disconnect(popupHidden_); disconnect(popupDestroyed_);
    auto* popup = qobject_cast<QMenu*>(QApplication::activePopupWidget());
    if (popup && popup->isVisible()) {
        controller_.setPopupOpen(true); columnPopup_ = popup;
        const auto closed = [this] { QTimer::singleShot(0, this, [this] { trackColumnPopup(); }); };
        popupHidden_ = connect(popup, &QMenu::aboutToHide, this, closed);
        popupDestroyed_ = connect(popup, &QObject::destroyed, this, closed);
        return;
    }
    columnPopup_.clear(); controller_.setPopupOpen(false); reconcileColumnHover();
}

void TabBar::reconcileColumnHover()
{
    auto* owner = controller_.columnManaged() ? parentWidget() : this;
    setColumnHovered(isVisible() && owner && owner->isVisible() && owner->rect().contains(owner->mapFromGlobal(QCursor::pos())));
}

void TabBar::refreshScrollInput()
{
    const auto hit = controller_.scrollbarHitRect();
    if (hit.isEmpty()) { if (scrollInput_ && !controller_.scrollbarDragging()) scrollInput_->hide(); return; }
    if (!scrollInput_) {
        scrollInput_ = new QWidget(window());
        scrollInput_->setObjectName("TabScrollbarInput"); scrollInput_->setMouseTracking(true);
        scrollInput_->setCursor(Qt::ArrowCursor); scrollInput_->setFocusPolicy(Qt::NoFocus);
        scrollInput_->installEventFilter(this);
    } else if (scrollInput_->parentWidget() != window()) scrollInput_->setParent(window());
    scrollInput_->setGeometry(QRect(mapTo(window(), hit.topLeft()), hit.size()));
    scrollInput_->show(); scrollInput_->raise();
}

void TabBar::applyPointerDecision(const TabPointerDecision& decision)
{
    using Action = TabPointerDecision::Action;
    switch (decision.action) {
    case Action::None: return;
    case Action::ActivateTab: return;
    case Action::DragTab: {
        const auto callback = drag;
        if (callback) callback(decision.index, decision.start);
        return;
    }
    case Action::DragFolder: {
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

void TabBar::setAnimationsEnabled(bool enabled) { animationsSwitch = enabled; }

bool TabBar::animationsEnabled()
{
    return animationsSwitch && motionAnimationsEnabled();
}

TabBar::TabBar(const Theme& theme, QWidget* parent)
    : QWidget(parent), controller_(theme)
{
    static const bool factory = [] { QAccessible::installFactory(accessibleFactory); return true; }();
    Q_UNUSED(factory);
    setObjectName("TabBar");
    controllerClock_.start();
    setMouseTracking(true); setFocusPolicy(Qt::TabFocus); setAttribute(Qt::WA_Hover);
    countButton_ = new CompactToolButton(this, QSize(metrics().countMinimumWidth, metrics().rowHeight));
    countButton_->setObjectName("TabRowCountButton"); countButton_->setToolButtonStyle(Qt::ToolButtonTextOnly); countButton_->hide();
    connect(countButton_, &QToolButton::clicked, this, [this] { setExpanded(!expanded()); });
    frameDriver_.advance = [this](qint64 timestampNs) {
        prepareController(); controller_.advance(timestampNs); synchronizeController();
    };
    wakeTimer_.setSingleShot(true);
    connect(&wakeTimer_, &QTimer::timeout, this, [this] {
        prepareController(); controller_.advance(controllerClock_.nsecsElapsed()); synchronizeController();
    });
    ready_ = true;
    prepareController(true); synchronizeController();
}

TabBar::~TabBar()
{
    qApp->removeEventFilter(this); delete scrollInput_.data(); delete liftedLayer_.data();
}

TabStripEnvironment TabBar::controllerEnvironment() const
{
    TabStripEnvironment environment;
    environment.size = size(); environment.font = QFont(font(), this); environment.direction = layoutDirection();
    environment.logicalDpiX = logicalDpiX(); environment.logicalDpiY = logicalDpiY();
    environment.globalOrigin = mapToGlobal(QPoint());
    environment.windowArea = QRect(mapFrom(window(), QPoint()), window()->size());
    environment.visible = isVisible(); environment.windowActive = window()->isActiveWindow();
    environment.animations = animationsEnabled(); environment.smoothScrolling = isSmoothScrollingEnabled(this);
    environment.controls = {
        trailing_ && !trailing_->isHidden() ? trailing_->sizeHint().width() : 0,
        end_ && !end_->isHidden() ? end_->sizeHint().width() : 0,
        trailing_ && !trailing_->isHidden(), end_ && !end_->isHidden(),
        countButton_ ? countButton_->size() : QSize{}, metrics().countMinimumWidth, uiPixels(14),
        countButton_ != nullptr, countButton_ && !countButton_->isHidden(), isVisible()
    };
    return environment;
}

void TabBar::prepareController(bool measureEnvironment)
{
    controller_.setTimestampNs(frameDriver_.now(controllerClock_.nsecsElapsed()));
    if (measureEnvironment) { controller_.setEnvironment(controllerEnvironment()); return; }
    // Font resolution and control size hints belong to their change events,
    // never to pointer moves or animation frames. Coordinates still follow
    // the owning window when an ancestor moves without moving this widget.
    auto environment = controller_.environment();
    environment.size = size(); environment.globalOrigin = mapToGlobal(QPoint());
    environment.windowArea = QRect(mapFrom(window(), QPoint()), window()->size());
    environment.visible = isVisible(); environment.windowActive = window()->isActiveWindow();
    environment.animations = animationsEnabled(); environment.smoothScrolling = isSmoothScrollingEnabled(this);
    controller_.setEnvironment(std::move(environment));
}

void TabBar::scheduleController()
{
    if (controller_.needsFrame()) {
        if (frameDriver_.state() != QAbstractAnimation::Running)
            frameDriver_.restart(controllerClock_.nsecsElapsed(), -1);
    } else frameDriver_.stop();
    if (const auto deadline = controller_.nextWakeupNs()) {
        const auto remaining = std::max<qint64>(0, *deadline - controllerClock_.nsecsElapsed());
        wakeTimer_.start(int((remaining + 999'999) / 1'000'000));
    } else wakeTimer_.stop();
}

void TabBar::applyControlState()
{
    const auto controls = controller_.controlState();
    countButton_->setVisible(controls.countShown);
    if (controls.countMeasured) {
        countButton_->setText(controls.countText);
        if (countButton_->size() != controls.countSize)
            static_cast<CompactToolButton*>(countButton_)->setTargetSize(controls.countSize);
        countButton_->setToolTip(!expanded()
            ? QCoreApplication::translate("WindowSystem", "%n more tab(s) in other rows; click to expand", nullptr, hiddenTabs())
            : QCoreApplication::translate("WindowSystem", "Collapse other rows"));
        countButton_->setAccessibleName(countButton_->toolTip());
    }
    if (trailing_ && !controls.rects.trailing.isNull()) { trailing_->setGeometry(controls.rects.trailing); trailing_->raise(); }
    if (end_ && !controls.rects.end.isNull()) { end_->setGeometry(controls.rects.end); end_->raise(); }
    if (!controls.rects.count.isNull()) { countButton_->move(controls.rects.count.topLeft()); countButton_->raise(); }
}

void TabBar::synchronizeController()
{
    if (synchronizing_) return;
    synchronizing_ = true;
    const QPointer<TabBar> self(this);
    for (;;) {
        const auto changes = controller_.takeChanges();
        scheduleController();
        if (controller_.watchesOutsideInput()) qApp->installEventFilter(this);
        else qApp->removeEventFilter(this);
        if (!changes.repaint && !changes.presentation && !changes.carrier && !changes.surface && !changes.view
            && !changes.reservedSize && !changes.controls && !changes.reconcileHover && !changes.trackPopup
            && !changes.incomingLeft && !changes.current) break;
        if (changes.controls) applyControlState();
        if (!self) return;
        if (changes.incomingLeft) incomingPainter_.clear();
        if (changes.presentation) invalidatePresentation();
        if (changes.repaint) QWidget::update();
        if (changes.repaint || changes.surface || changes.view) { updateLiftedLayer(); refreshScrollInput(); }
        if (!self) return;
        if (changes.reconcileHover) QTimer::singleShot(0, this, [this] { reconcileColumnHover(); });
        if (changes.trackPopup) QTimer::singleShot(0, this, [this] { trackColumnPopup(); });
        if (changes.carrier) emit carrierInvalidated();
        if (!self) return;
        if (changes.surface) if (const auto callback = surfaceChanged) callback();
        if (!self) return;
        if (changes.view) emit viewChanged();
        if (!self) return;
        if (changes.reservedSize) if (const auto callback = reservedSizeChanged) callback();
        if (!self) return;
        if (changes.current) emit currentChanged(*changes.current);
        if (!self) return;
    }
    synchronizing_ = false;
}

void TabBar::setTrailingWidget(QWidget* widget)
{
    if (trailing_ && trailing_ != widget) trailing_->removeEventFilter(this);
    trailing_ = widget;
    if (widget) { widget->setParent(this); widget->installEventFilter(this); widget->show(); }
    refreshTrailing();
}

void TabBar::setEndWidget(QWidget* widget)
{
    if (end_ && end_ != widget) end_->removeEventFilter(this);
    end_ = widget;
    if (widget) { widget->setParent(this); widget->installEventFilter(this); widget->show(); }
    refreshTrailing();
}

void TabBar::showColumnPopup(std::function<void()> open)
{
    const QPointer<TabBar> self(this);
    prepareController(); controller_.setPopupOpen(true); synchronizeController();
    if (!self) return;
    open();
    if (self) QTimer::singleShot(0, this, [this] { trackColumnPopup(); });
}

void TabBar::inheritHover(const TabBar& previous)
{
    prepareController(); controller_.inheritHover(previous.controller_, mapFromGlobal(QCursor::pos())); synchronizeController();
}

void TabBar::refreshPresentationLayers()
{
    const QPointer<TabBar> self(this);
    prepareController(); controller_.refreshPresentationLayers(); synchronizeController();
    if (self) { updateLiftedLayer(); refreshScrollInput(); }
}

bool TabBar::scrollAt(QPoint local, QPoint delta, bool smooth)
{
    prepareController();
    const auto decision = controller_.routeScroll(local, delta);
    if (decision.action == TabWheelDecision::Action::Ignore) { synchronizeController(); return false; }
    const QPointer<TabBar> self(this);
    emit scrollInputStarted();
    if (!self) return true;
    const bool moved = controller_.applyScroll(decision, smooth);
    synchronizeController(); return moved;
}

void TabBar::updateLiftedLayer()
{
    auto* owner = window();
    auto current = controller_.foregroundRegion().translated(mapTo(owner, QPoint()));
    current &= owner->rect();
    if (!current.isEmpty() && !liftedLayer_) {
        liftedLayer_ = new QWidget(owner); liftedLayer_->setObjectName("LiftedTabLayer");
        liftedLayer_->setAttribute(Qt::WA_TransparentForMouseEvents);
        liftedLayer_->setAttribute(Qt::WA_NoSystemBackground); liftedLayer_->installEventFilter(this);
    }
    if (!liftedLayer_) return;
    if (liftedLayer_->parentWidget() != owner) { liftedLayer_->setParent(owner); liftedPaintRegion_ = {}; }
    const auto dirty = liftedPaintRegion_ | current;
    if (current.isEmpty()) liftedLayer_->hide();
    else {
        liftedLayer_->setGeometry(owner->rect());
        if (current != liftedPaintRegion_) liftedLayer_->setMask(current);
        liftedLayer_->show(); liftedLayer_->raise(); liftedLayer_->update(dirty);
    }
    if (!dirty.isEmpty()) owner->update(dirty);
    liftedPaintRegion_ = std::move(current);
}

std::shared_ptr<const TabStripFrame> TabBar::captureStripFrame(const QFont& font, Qt::LayoutDirection direction)
{
    prepareController();
    auto frame = controller_.captureStripFrame(font, direction);
    synchronizeController(); return frame;
}

std::shared_ptr<const TabForegroundFrame> TabBar::captureForegroundFrame(const QFont& font, Qt::LayoutDirection direction)
{
    prepareController();
    auto frame = controller_.captureForegroundFrame(font, direction);
    synchronizeController(); return frame;
}

void TabBar::mousePressEvent(QMouseEvent* event)
{
    const auto local = event->position().toPoint();
    const auto global = event->globalPosition().toPoint();
    event->accept(); prepareController();
    const QPointer<TabBar> self(this);
    if (event->button() == Qt::LeftButton && controller_.scrollbarHit(local)) emit scrollInputStarted();
    if (!self) return;
    const auto decision = controller_.pointerPress(event->button(), local, global);
    synchronizeController();
    if (self) applyPointerDecision(decision);
}

void TabBar::mouseMoveEvent(QMouseEvent* event)
{
    prepareController();
    const auto decision = controller_.pointerMove(event->buttons(), event->position().toPoint(),
        event->globalPosition().toPoint(), QApplication::startDragDistance());
    const QPointer<TabBar> self(this);
    synchronizeController(); if (self) applyPointerDecision(decision);
}

void TabBar::mouseReleaseEvent(QMouseEvent* event)
{
    event->accept(); prepareController();
    const auto decision = controller_.pointerRelease(event->button(), event->position().toPoint());
    const QPointer<TabBar> self(this);
    synchronizeController(); if (self) applyPointerDecision(decision);
}

void TabBar::mouseDoubleClickEvent(QMouseEvent* event) { mousePressEvent(event); }

void TabBar::wheelEvent(QWheelEvent* event)
{
    const auto local = event->position().toPoint();
    const auto delta = event->pixelDelta().isNull() ? event->angleDelta() / 3 : event->pixelDelta();
    const bool smooth = event->pixelDelta().isNull();
    event->accept(); scrollAt(local, delta, smooth);
}

void TabBar::enterEvent(QEnterEvent* event)
{
    prepareController(); controller_.enter(event->position().toPoint());
    QWidget::enterEvent(event); synchronizeController();
}

void TabBar::leaveEvent(QEvent* event)
{
    prepareController(); controller_.leave(); QWidget::leaveEvent(event); synchronizeController();
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
    prepareController();
    if (!controller_.keyPress(event->key())) {
        const QPointer<TabBar> self(this);
        synchronizeController(); if (self) QWidget::keyPressEvent(event); return;
    }
    event->accept(); synchronizeController();
}

void TabBar::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (ready_) { prepareController(); synchronizeController(); }
}

void TabBar::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (!ready_) return;
    if (event->type() == QEvent::ActivationChange || event->type() == QEvent::FontChange
        || event->type() == QEvent::StyleChange || event->type() == QEvent::LayoutDirectionChange) {
        prepareController(true);
        if (event->type() == QEvent::StyleChange) controller_.refreshStyle();
        synchronizeController();
    }
}

bool TabBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == scrollInput_) {
        if (event->type() == QEvent::Enter) {
            prepareController(); controller_.hoverScrollbar(); controller_.setColumnHovered(true); synchronizeController();
            return false;
        } else if (event->type() == QEvent::Leave) {
            prepareController(); controller_.hoverScrollbar();
            const bool dragging = controller_.scrollbarDragging();
            const QPointer<TabBar> self(this);
            synchronizeController(); if (self && !dragging) reconcileColumnHover();
            return false;
        } else if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease
                   || event->type() == QEvent::MouseButtonDblClick || event->type() == QEvent::MouseMove) {
            auto* source = static_cast<QMouseEvent*>(event);
            QMouseEvent forwarded(source->type(), QPointF(mapFromGlobal(source->globalPosition().toPoint())), source->globalPosition(),
                source->button(), source->buttons(), source->modifiers(), source->pointingDevice());
            forwarded.setTimestamp(source->timestamp()); QCoreApplication::sendEvent(this, &forwarded); return true;
        } else if (event->type() == QEvent::Wheel) {
            auto* source = static_cast<QWheelEvent*>(event);
            scrollAt(mapFromGlobal(source->globalPosition().toPoint()),
                source->pixelDelta().isNull() ? source->angleDelta() / 3 : source->pixelDelta(), source->pixelDelta().isNull());
            return true;
        } else if (event->type() == QEvent::ContextMenu) {
            auto* source = static_cast<QContextMenuEvent*>(event);
            QContextMenuEvent forwarded(source->reason(), mapFromGlobal(source->globalPos()), source->globalPos(), source->modifiers());
            QCoreApplication::sendEvent(this, &forwarded); return true;
        }
    }
    if (watched == liftedLayer_ && event->type() == QEvent::Paint) {
        QPainter painter(liftedLayer_); painter.translate(mapTo(liftedLayer_->parentWidget(), QPoint()) - liftedLayer_->pos());
        paintLiftedTabs(painter); return true;
    }
    if ((watched == trailing_ || watched == end_) && event->type() == QEvent::Enter) {
        prepareController(); controller_.controlsEntered(); synchronizeController(); return false;
    }
    if ((watched == trailing_ || watched == end_) && (event->type() == QEvent::LayoutRequest
        || event->type() == QEvent::ShowToParent || event->type() == QEvent::HideToParent)) {
        QTimer::singleShot(0, this, [this] { refreshTrailing(); }); return false;
    }
    if (controller_.dismissesOutsideInput()) {
        if (event->type() == QEvent::MouseButtonPress) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            if (!QRect(mapToGlobal(QPoint()), size()).contains(mouse->globalPosition().toPoint())) {
                setExpanded(false); return false;
            }
        } else if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            setExpanded(false); return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

bool TabBar::event(QEvent* event)
{
    if (!ready_) return QWidget::event(event);
    if (event->type() == QEvent::Show || event->type() == QEvent::Hide || event->type() == QEvent::Move
        || event->type() == QEvent::ParentChange || event->type() == QEvent::ScreenChangeInternal
        || event->type() == QEvent::DevicePixelRatioChange) {
        const QPointer<TabBar> self(this);
        prepareController(event->type() == QEvent::ScreenChangeInternal || event->type() == QEvent::DevicePixelRatioChange);
        controller_.refreshPresentationLayers(); synchronizeController();
        if (!self) return true;
    }
    if (event->type() == QEvent::Hide && scrollInput_) scrollInput_->hide();
    if (event->type() == QEvent::Show || event->type() == QEvent::Move)
        QTimer::singleShot(0, this, [this] { refreshScrollInput(); if (iconMode()) reconcileColumnHover(); });
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        const int index = tabAt(help->pos());
        QString text; QRect area;
        if (index >= 0) {
            area = tabRect(index);
            const auto action = actionRect(index);
            if (!action.isEmpty() && action.contains(help->pos())) {
                area = action;
                text = index < stickyCount() ? QCoreApplication::translate("WindowSystem", "Unstick tab")
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
            const auto& record = folders().at(folder);
            if (vertical() && controller_.painter().folderAddRect(area).contains(help->pos()))
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
