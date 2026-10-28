// SPDX-License-Identifier: MIT
#pragma once
#include "tab_pointer_controller.hpp"
#include "tab_hover_controller.hpp"
#include "tab_column_peek.hpp"
#include "tab_presence_state.hpp"
#include "tab_rows_state.hpp"
#include "tab_position_motion.hpp"
#include "tab_lift_state.hpp"
#include "tab_label_store.hpp"
#include "tab_strip_arrangement.hpp"
#include "tab_incoming_state.hpp"
#include "tab_drag_visual.hpp"
#include "tab_foreground_frame.hpp"

#include "style/smooth_scroll.hpp"

#include "tab_layout.hpp"
#include "tab_drag_geometry.hpp"
#include "tab_wheel_controller.hpp"
#include "style/tab_painter.hpp"
#include "style/tab_carrier_painter.hpp"
#include "style/tab_title_layout.hpp"
#include "style/tab_label_content.hpp"
#include "style/tab_strip_frame.hpp"
#include "style/motion.hpp"
#include "style/elevation.hpp"
#include <window_system/layout_model.hpp>
#include <window_system/tab_appearance.hpp>
#include <window_system/theme.hpp>

#include <QElapsedTimer>
#include <QHash>
#include <QIcon>
#include <QPointer>
#include <QRegion>
#include <QSet>
#include <QTimer>
#include <QVariant>
#include <QWidget>
#include <functional>
#include <array>
#include <optional>
#include <memory>

class QToolButton;
class QMenu;

namespace window_system {

// A tab being carried onto a strip, measured by the strip it would join; or
// the header of a carried tab folder (`title` its name).
struct CarriedLabel {
    QString title;
    bool icon = false;
    bool folderHeader = false;
    bool collapsed = false;
    int count = 0;
};

using LabelPositions = QHash<QUuid, QRect>;

struct TabSurfaceAnchor {
    SurfaceGeometry geometry;
    ElevationProfile elevation;
    bool lifted = false;
    bool foreground = false;
    LabelPresencePose presence;
};

// A tab folder as a strip shows it: a run of `count` tabs from `first`
// (tab strip design 12).
struct TabStripFolder {
    QUuid id;
    QString name;
    QString colorId;
    bool collapsed = false;
    int first = 0;
    int count = 0;
    bool operator==(const TabStripFolder&) const = default;
};

// One tab strip: its tabs, their arrangement and input. It reads the pure
// layout (tab_layout.hpp) and paints through TabPainter; it never computes a
// position of its own (docs/design/tab-strip-design.md, ADR 0011).
class TabBar final : public QWidget {
    Q_OBJECT
public:
    explicit TabBar(const Theme& theme, QWidget* parent);
    ~TabBar() override;

    // Tabs. A tab's data is its TabId; hosts read it with toUuid().
    int addTab(const QIcon& icon, const QString& title);
    void removeTab(int index);
    [[nodiscard]] int count() const { return int(tabs_.size()); }
    [[nodiscard]] QVariant tabData(int index) const;
    void setTabData(int index, const QVariant& data);
    [[nodiscard]] QString tabText(int index) const;
    void setTabText(int index, const QString& text);
    [[nodiscard]] QIcon tabIcon(int index) const;
    void setTabIcon(int index, const QIcon& icon);
    [[nodiscard]] QString tabToolTip(int index) const;
    void setTabToolTip(int index, const QString& tip);
    void setTabMetadata(int index, const TabRecord& tab);
    [[nodiscard]] const TabColorPreset* tabPreset(int index) const;
    [[nodiscard]] std::shared_ptr<const TabLabelContent> labelContent(int index) const;
    [[nodiscard]] int currentIndex() const { return current_; }
    void setCurrentIndex(int index);
    [[nodiscard]] QSize iconSize() const { return {metrics_.iconSize, metrics_.iconSize}; }
    [[nodiscard]] int indexOf(TabId tab) const;

    void setTheme(const Theme& theme);
    void setAppearance(const TabAppearance& appearance, TabBarPlacement placement);
    [[nodiscard]] TabBarPlacement placement() const { return placement_; }
    [[nodiscard]] bool vertical() const { return placement_ == TabBarPlacement::Left || placement_ == TabBarPlacement::Right; }
    [[nodiscard]] const TabAppearance& appearance() const { return appearance_; }
    [[nodiscard]] const TabMetrics& metrics() const { return metrics_; }
    void setStickyCount(int count);
    [[nodiscard]] int stickyCount() const { return stickyCount_; }
    // Tab folders, in strip order.
    void setFolders(QVector<TabStripFolder> folders);
    [[nodiscard]] const QVector<TabStripFolder>& folders() const { return folders_; }
    // The folder holding a tab, or -1.
    [[nodiscard]] int folderOf(int index) const;
    [[nodiscard]] QRect folderHeaderRect(int folder) const;
    [[nodiscard]] int folderAt(QPoint local) const;
    // The folder's color preset: its own, or one picked from its identity.
    [[nodiscard]] const TabColorPreset* folderPreset(int folder) const;
    void setGroupFocused(bool focused);
    void setSurfaceContext(QRectF bodyInBar, qreal focusProgress, qreal joinProgress = 1);
    struct SurfaceState { QHash<QUuid, qreal> selection; QUuid current; qreal lift; bool incoming; };
    [[nodiscard]] SurfaceState captureSurfaceState() const;
    void inheritSurface(const SurfaceState& previous, bool animate);
    [[nodiscard]] QVector<TabSurfaceAnchor> surfaceAnchors(QPointF offset = {}) const;
    [[nodiscard]] LabelPresencePose labelPresence(int index) const;
    void paintSurfaceExtension(QPainter& painter, QPointF offset = {}) const;
    std::function<void()> surfaceChanged;
    void setCarriedDisplay(bool carried);
    void setCarriedTileColumns(int columns);
    [[nodiscard]] int carriedTileColumns() const { return carriedTileColumns_; }
    // Vertical: only icons in a narrow column; hovering peeks at full width.
    void setIconMode(bool iconMode);
    [[nodiscard]] bool iconMode() const { return columnPeek_.iconsOnly(); }
    [[nodiscard]] bool peeking() const { return columnPeek_.expanded(); }
    enum class VerticalPresentation { Icons, Rows, Tiles };
    [[nodiscard]] VerticalPresentation verticalPresentation() const
    {
        if (carriedPresentation_) return *carriedPresentation_;
        if (iconMode()) return peeking() ? VerticalPresentation::Rows : VerticalPresentation::Icons;
        return appearance_.stickySizing == StickyTabSizing::Compact ? VerticalPresentation::Tiles : VerticalPresentation::Rows;
    }
    void setCarriedPresentation(std::optional<VerticalPresentation> presentation);
    [[nodiscard]] bool iconsOnly() const { return vertical() && verticalPresentation() == VerticalPresentation::Icons; }
    [[nodiscard]] bool compactStickyTiles() const { return vertical() && verticalPresentation() == VerticalPresentation::Tiles; }
    void setColumnReveal(qreal reveal, bool modeTransition = false);
    void setColumnManaged(bool managed) { columnManaged_ = managed; }
    void setColumnHovered(bool hovered);
    void restoreColumnPeek(bool peeked);
    void showColumnPopup(std::function<void()> open);
    void refreshScrollInput();
    void refreshPresentationLayers();
    void inheritHover(const TabBar& previous);
    [[nodiscard]] TabVisualState captureVisualState();
    void transitionFrom(TabVisualState state, Motion motion = Motion::Relocate);
    [[nodiscard]] TabVisualState takeSeparateDepartures(TabVisualState& previous) const;
    [[nodiscard]] LabelPositions captureDropPositions(std::optional<LabelPositions> carrierPositions = {}) const;
    void settleFrom(const LabelPositions& globalPositions);
    // Reused by any operation that makes existing labels appear or disappear.
    void animatePresence(const QSet<QUuid>& tabs, bool visible, Motion motion = Motion::Presence);
    // Horizontal: the controls after the last tab (new tab, list, sidebar
    // actions). The bar places it; hidden widgets take no space.
    void setTrailingWidget(QWidget* widget);
    // Horizontal: controls aligned to the end of the first row (the list and
    // sidebar actions); the row count joins them.
    void setEndWidget(QWidget* widget);
    void refreshTrailing();

    // Geometry. The group asks how much it must keep for the strip; an
    // expanded overlay or a peeking icon column is larger than that.
    // `available` is the group's size for a horizontal strip and (column
    // width, group height) for a vertical one.
    [[nodiscard]] QSize reservedSize(QSize available) const;
    [[nodiscard]] int minimumColumnWidth() const;
    // The size to show: larger than reserved while an overlay is open.
    [[nodiscard]] QSize displaySize(QSize available) const;
    void setAvailableSize(QSize available);
    [[nodiscard]] QSize availableSize() const { return available_; }
    [[nodiscard]] const TabLayout& tabLayout() const { return layout_; }
    [[nodiscard]] std::shared_ptr<const TabStripFrame> captureStripFrame(const QFont& font, Qt::LayoutDirection direction);
    [[nodiscard]] std::shared_ptr<const TabForegroundFrame> captureForegroundFrame(const QFont& font, Qt::LayoutDirection direction);
    // Where tab coordinates of the layout start inside the widget.
    [[nodiscard]] QPoint tabOrigin() const;
    // Displayed and visible: what a click on it hits.
    [[nodiscard]] QRect tabRect(int index) const;
    // The painted contour, clipped to the displayed part of the tab.
    [[nodiscard]] QRegion tabRegion(int index) const;
    [[nodiscard]] QRect displayTabRect(int index) const { return tabRect(index); }
    [[nodiscard]] int tabAt(QPoint local) const;
    // The title slot, for an inline editor (8.5) and tests.
    [[nodiscard]] QRect titleRect(int index) const;
    [[nodiscard]] QRect actionRect(int index) const;
    // The lane of the regular tabs: its offset and viewport, for the rules
    // that keep shown tabs in place when the strip changes size.
    [[nodiscard]] int scrollOffset() const;
    void setScrollOffset(int offset);
    [[nodiscard]] int stickyScrollOffset() const { return stickyOffset_; }
    [[nodiscard]] int rowScrollOffset() const { return rowsOffset_; }
    void restoreScrollOffsets(int regular, int sticky, int rows);
    [[nodiscard]] int maximumScrollOffset() const;
    [[nodiscard]] QRect scrollViewport() const;
    // The start of the main lane covered by frozen sticky tabs.
    [[nodiscard]] int frozenExtent() const;
    // A tab's rectangle in its lane's content, independent of scrolling.
    [[nodiscard]] QRect contentTabRect(int index) const;
    [[nodiscard]] bool scrollsHorizontally() const;
    // Scrolls the lane under `local`; returns whether anything moved.
    bool scrollAt(QPoint local, QPoint delta, bool smooth = true);
    void ensureVisible(int index);
    [[nodiscard]] bool fullyVisible(int index) const;
    [[nodiscard]] bool isOverflowing() const;

    // Collapsed rows (tab strip design 3.4).
    [[nodiscard]] bool collapsible() const;
    [[nodiscard]] bool expanded() const { return rowsState_.expanded(); }
    void setExpanded(bool expanded, bool byWheel = false, bool animate = true);
    [[nodiscard]] bool wrappedRows() const;
    [[nodiscard]] bool rowTransitionRunning() const { return rowsState_.needsFrame(); }
    [[nodiscard]] QRect scrollViewportAt(QPoint local) const;
    [[nodiscard]] bool scrollsHorizontallyAt(QPoint local) const;
    [[nodiscard]] int hiddenTabs() const { return layout_.hidden; }
    // Dragging over a collapsed strip shows all rows as an overlay.
    void setDragExpanded(bool expanded);
    [[nodiscard]] bool overlayActive() const;

    // Drag (tab strip design 6).
    void setDraggedTab(TabId tab);
    [[nodiscard]] int draggedIndex() const { return dragged_; }
    // The dragged tab drawn lifted under the pointer, `grab` being where in
    // the tab it was taken (tab strip design 6.1). A null point ends it.
    void setLifted(QPoint local, QPoint grab = {});
    void setLiftedWithAnchor(QPoint local, const DragAnchor& grab);
    // Where the lifted tab is drawn, in this widget.
    [[nodiscard]] QRect liftedRect() const;
    void setIncomingVisual(TabDragVisual visual, QPoint local = {});
    [[nodiscard]] QRect incomingRect() const;
    [[nodiscard]] QRect carrierBounds() const;
    [[nodiscard]] QPoint carrierAnchor() const;
    [[nodiscard]] std::shared_ptr<const TabCarrierFrame> carrierFrame();
    [[nodiscard]] TabInsertion insertionAt(QPoint local, int stickyReach = 0,
                                           TabLayoutInput::Carried carried = TabLayoutInput::Carried::Tab) const;
    // `folder` is the folder the block joins, -1 for none.
    void setDropPreview(int index, bool sticky, QVector<int> block, int folder = -1);
    [[nodiscard]] int dropIndex() const { return dropIndex_; }
    [[nodiscard]] bool dropSticky() const { return dropSticky_; }
    [[nodiscard]] int dropFolder() const { return dropFolder_; }
    // A whole folder being carried: its header and tabs leave the strip.
    void setDraggedFolder(QUuid folder);
    [[nodiscard]] int draggedFolder() const { return draggedFolder_; }
    // A folder header's width in this strip (tab strip design 12).
    [[nodiscard]] int folderHeaderExtent(const TabStripFolder& folder) const;
    [[nodiscard]] QRect dropPreviewRect() const;
    // A tab's width in the sticky or regular section of this strip.
    [[nodiscard]] int tabExtent(int index, bool sticky) const;
    // Its width ignoring sticky sizing.
    [[nodiscard]] int naturalTabWidth(int index) const;
    // The width a carried tab will have in this strip's sticky or regular
    // section (tab strip design 6.3).
    [[nodiscard]] int carriedExtent(const CarriedLabel& label, bool sticky) const;

    std::function<void(int, DragStart)> drag;
    std::function<void(int, QPoint)> menu;
    std::function<void(int)> close;
    std::function<void(int)> unstick;
    // Folder headers: a click toggles the folder, the context menu, a drag
    // carries the folder, and a vertical header's add button.
    std::function<void(int)> toggleFolder;
    std::function<void(int, QPoint)> folderMenu;
    std::function<void(int, DragStart)> folderDrag;
    std::function<void(int)> newTabInFolder;
    // The space the group keeps for the strip changed.
    std::function<void()> reservedSizeChanged;

    // Tests switch animations off; Windows' animation setting is honored.
    static void setAnimationsEnabled(bool enabled);
    [[nodiscard]] static bool animationsAllowed() { return animationsEnabled(); }

signals:
    void currentChanged(int index);
    // Offsets, expansion or layout changed: hover geometry is stale.
    void viewChanged();
    // Accepted scroll input, before changing offsets or expanding rows.
    void scrollInputStarted();
    // State notifications still run while a carried HWND stops painting.
    void carrierInvalidated();

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void invalidateCarrier(bool content);
    [[nodiscard]] LabelPresencePose presenceFor(int index, QRect area) const;
    [[nodiscard]] QRect incomingRectAt(QPoint local) const;
    [[nodiscard]] QRect liftedRectAt(QPoint local) const;
    [[nodiscard]] TabStripRows rowPolicy() const;
    [[nodiscard]] TabStripGeometry stripGeometry() const;
    [[nodiscard]] TabStripControls stripControls() const;
    [[nodiscard]] TabStripArrangementInput arrangementInput() const;
    [[nodiscard]] TabDragArea dragArea() const;
    [[nodiscard]] TabDragConstraint incomingConstraint() const;
    [[nodiscard]] std::optional<TabLiftTarget> liftTarget() const;
    [[nodiscard]] bool collapsedNow() const;
    void relayout(bool animate, bool measureChanged = true);
    void relayoutTrailing();
    void applyControlRects(const TabStripControlRects& controls);
    [[nodiscard]] TabLabelParts labelParts(int index, bool sticky) const;
    [[nodiscard]] bool isCompactSticky(int index) const;
    [[nodiscard]] bool rowShape() const;
    [[nodiscard]] TabShape shapeOf(int index) const;
    [[nodiscard]] TabParts partsOf(int index, const QRect& rect) const;
    [[nodiscard]] TabParts labelGeometry(const QRect& rect, TabShape shape, TabLabelParts label,
                                         bool iconOnly, bool action, bool sticky) const;
    [[nodiscard]] QRect animatedRect(int index) const;
    [[nodiscard]] std::optional<TabStripLabelSample> labelSample(int index, QRect area, const QFont& font,
        Qt::LayoutDirection direction, bool settling = false);
    [[nodiscard]] TabPaintState paintState(int index);
    void paintLiftedTabs(QPainter& painter);
    void updateLiftedLayer();
    void captureLaneFrame(TabStripLaneFrame& frame, int lane, const QFont& font, Qt::LayoutDirection direction);
    void captureFolderFrame(TabStripLaneFrame& frame, int lane);
    void captureCarriedFolder(TabForegroundFrame& frame, QRect area);
    [[nodiscard]] TabStripLabelSample captureLiftedLabel(QRect area, const QFont& font, Qt::LayoutDirection direction);
    [[nodiscard]] TabFolderPaintState folderPaintState(int folder) const;
    [[nodiscard]] int laneAt(QPoint local) const;
    [[nodiscard]] std::optional<int> stickyBoundary() const;
    [[nodiscard]] QRect scrollThumb(int lane) const;
    [[nodiscard]] QRect scrollTrack(int lane) const;
    void setLaneOffset(int lane, int offset);
    bool scrollLaneBy(int lane, int delta, bool smooth);
    void revealScrollbar(int lane);
    [[nodiscard]] QRect scrollbarHitRect() const;
    void trackColumnPopup();
    void reconcileColumnHover();
    void applyColumnPeekAction(const TabColumnPeekAction& action);
    [[nodiscard]] int laneOffsetInput(int lane) const;
    void startCollapseTimer();
    void updateRowPresentation(TabVisualState previous, bool moving, bool revealActive);
    [[nodiscard]] const TabStripRowMeasure& measureRows(QSize available) const;
    [[nodiscard]] int rowShadowExtent() const;
    mutable std::optional<TabStripRowMeasure> rowMeasure_;
    [[nodiscard]] bool hasLabelAction(int index) const;
    void updateHover(QPoint local);
    [[nodiscard]] TabPointerHit pointerHit(QPoint local, bool releasing) const;
    void applyPointerDecision(const TabPointerDecision& decision);
    [[nodiscard]] qreal liftProgress() const;
    [[nodiscard]] SurfaceGeometry anchorGeometry(QRectF anchor, QRectF visible, bool compact, bool lifted, bool transitioning = true) const;
    [[nodiscard]] QRect activeDragRect() const;
    [[nodiscard]] std::pair<qreal, qreal> attachmentValues() const;
    void syncAttachment();
    void surfaceFrameChanged();
    [[nodiscard]] qreal selectionWeight(int index) const;
    void updateTitleWidths();
    [[nodiscard]] std::shared_ptr<const TabStripLabelStyle> labelStyle() const;
    static bool animationsEnabled();

    Theme theme_;
    TabMetrics metrics_;
    TabPainter painter_;
    mutable std::shared_ptr<const TabStripLabelStyle> labelStyle_;
    TabAppearance appearance_;
    TabBarPlacement placement_ = TabBarPlacement::Top;
    TabLabelStore tabs_;
    int current_ = -1;
    int stickyCount_ = 0;
    QVector<TabStripFolder> folders_;
    bool groupFocused_ = false;
    QRectF surfaceBody_;
    qreal surfaceFocus_ = 0;
    qreal surfaceJoin_ = 1;
    ProgressAnimation attachment_{1};
    std::pair<qreal, qreal> attachmentFrom_{1, 0};
    std::pair<qreal, qreal> attachmentTo_{1, 0};
    QUuid attachmentOwner_;
    bool attachmentInitialized_ = false;
    ProgressAnimation selection_{1};
    qreal selectionStart_ = 1;
    ProgressAnimation liftElevation_{0};
    mutable ElevationRenderer surfaceRenderer_;
    bool carriedDisplay_ = false;
    int carriedTileColumns_ = 0;
    std::optional<VerticalPresentation> carriedPresentation_;
    TabColumnPeek columnPeek_;
    qreal columnReveal_ = 1.0;
    bool columnModeTransition_ = false;
    bool columnManaged_ = false;
    QPointer<QMenu> columnPopup_;
    QMetaObject::Connection popupHidden_;
    QMetaObject::Connection popupDestroyed_;
    ProgressAnimation presenceProgress_{1};
    TabPresenceState presence_;
    TabVisualState departing_;
    TabDeparturePainter departurePainter_;
    QSize available_;
    TabLayout layout_;
    // Input for that exact layout; rebuilt with it and adjusted on scrolling.
    // Pointer queries do not remeasure titles or allocate extent arrays.
    TabLayoutInput arrangedInput_;
    // Offsets of the sticky and regular lanes (horizontal single rows or
    // vertical sections) and of wrapped rows.
    int stickyOffset_ = 0;
    int regularOffset_ = 0;
    int rowsOffset_ = 0;
    SmoothScrollAnimation scrollMotion_;
    int smoothLane_ = -1;
    bool applyingScrollMotion_ = false;
    int collapsedStickyRow_ = 0;
    int collapsedRegularRow_ = 0;
    TabRowsState rowsState_;
    QTimer collapseTimer_;
    QTimer peekTimer_;
    TabWheelController wheelController_;
    QElapsedTimer rowWheelClock_;
    QVariantAnimation rowsDriver_;
    ProgressAnimation scrollbarOpacity_{0};
    QTimer scrollbarHideTimer_;
    int scrollingLane_ = -1;
    QPointer<QWidget> scrollInput_;
    // Drag.
    int dragged_ = -1;
    int dropIndex_ = -1;
    TabLiftState lifted_;
    std::optional<int> initialStickyBoundary_;
    QPointer<QWidget> liftedLayer_;
    QRegion liftedPaintRegion_;
    TabIncomingState incoming_;
    TabCarrierPainter incomingPainter_;
    mutable std::optional<QRect> carrierBounds_;
    std::shared_ptr<const TabCarrierLabels> carrierContent_;
    std::shared_ptr<const TabCarrierFrame> carrierFrame_;
    std::array<std::shared_ptr<TabCarrierFrame>, 2> carrierBuffers_;
    std::array<std::shared_ptr<TabStripFrame>, 2> stripBuffers_;
    std::array<std::shared_ptr<TabForegroundFrame>, 2> foregroundBuffers_;
    bool carrierContentDirty_ = true;
    // Two bounded shape snapshots: attachment probing and displayed shape.
    // Full input keys cover resize, animation, scrolling, and theme metrics.
    mutable std::array<std::optional<std::pair<SurfaceGeometryInput, SurfaceGeometry>>, 2> anchorGeometryCache_;
    mutable QVector<int> carrierTabs_;
    bool dropSticky_ = false;
    QVector<int> dropBlock_;
    int dropFolder_ = -1;
    int draggedFolder_ = -1;
    // Input.
    TabPointerController pointer_;
    TabHoverController hover_;
    int scrollDragLane_ = -1;
    int scrollDragOrigin_ = 0;
    QPoint scrollDragStart_;
    QPointer<QWidget> trailing_;
    QPointer<QWidget> end_;
    QToolButton* countButton_ = nullptr;
    TabPositionMotion positions_;
    ProgressAnimation actionOpacity_{0};
    int fadingAction_ = -1;
    // Since the dragged tab was lifted: its shadow fades in (tab strip design 7).
    QTimer slideTimer_;
    bool planarReflow_ = false;
    [[nodiscard]] bool hasSingleTileRow() const;
    [[nodiscard]] bool usesPlanarReflow(const TabVisualState& previous) const;
};

} // namespace window_system
