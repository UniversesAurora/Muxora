// SPDX-License-Identifier: MIT
#pragma once

#include "tab_strip_controller.hpp"
#include "style/frame_clock.hpp"
#include "style/tab_carrier_painter.hpp"
#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <functional>

class QToolButton;
class QMenu;

namespace window_system {

// Widgets view of the shared strip controller. Input and native controls are
// adapted here; the model, arrangement and immutable frames belong to it.
class TabBar final : public QWidget {
    Q_OBJECT
public:
    explicit TabBar(const Theme& theme, QWidget* parent);
    ~TabBar() override;

    // Tabs. A tab's data is its TabId; hosts read it with toUuid().
    int addTab(const QIcon& icon, const QString& title);
    void removeTab(int index);
    [[nodiscard]] int count() const { return controller_.count(); }
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
    [[nodiscard]] int currentIndex() const { return controller_.currentIndex(); }
    void setCurrentIndex(int index);
    [[nodiscard]] QSize iconSize() const { return controller_.iconSize(); }
    [[nodiscard]] int indexOf(TabId tab) const;

    void setTheme(const Theme& theme);
    void setAppearance(const TabAppearance& appearance, TabBarPlacement placement);
    [[nodiscard]] TabBarPlacement placement() const { return controller_.placement(); }
    [[nodiscard]] bool vertical() const { return controller_.vertical(); }
    [[nodiscard]] const TabAppearance& appearance() const { return controller_.appearance(); }
    [[nodiscard]] const TabMetrics& metrics() const { return controller_.metrics(); }
    void setStickyCount(int count);
    [[nodiscard]] int stickyCount() const { return controller_.stickyCount(); }
    // Tab folders, in strip order.
    void setFolders(QVector<TabStripFolder> folders);
    [[nodiscard]] const QVector<TabStripFolder>& folders() const { return controller_.folders(); }
    // The folder holding a tab, or -1.
    [[nodiscard]] int folderOf(int index) const;
    [[nodiscard]] QRect folderHeaderRect(int folder) const;
    [[nodiscard]] int folderAt(QPoint local) const;
    // The folder's color preset: its own, or one picked from its identity.
    [[nodiscard]] const TabColorPreset* folderPreset(int folder) const;
    void setGroupFocused(bool focused);
    void setSurfaceContext(QRectF bodyInBar, qreal focusProgress, qreal joinProgress = 1);
    using SurfaceState = TabSurfaceSnapshot;
    [[nodiscard]] SurfaceState captureSurfaceState() const;
    void inheritSurface(const SurfaceState& previous, bool animate);
    [[nodiscard]] QVector<TabSurfaceAnchor> surfaceAnchors(QPointF offset = {}) const;
    [[nodiscard]] LabelPresencePose labelPresence(int index) const;
    [[nodiscard]] TabSurfaceExtension surfaceExtension(QPointF offset = {}) const;
    std::function<void()> surfaceChanged;
    void setCarriedDisplay(bool carried);
    void setCarriedTileColumns(int columns);
    [[nodiscard]] int carriedTileColumns() const { return controller_.carriedTileColumns(); }
    // Vertical: only icons in a narrow column; hovering peeks at full width.
    void setIconMode(bool iconMode);
    [[nodiscard]] bool iconMode() const { return controller_.iconMode(); }
    [[nodiscard]] bool peeking() const { return controller_.peeking(); }
    using VerticalPresentation = TabStripController::VerticalPresentation;
    [[nodiscard]] VerticalPresentation verticalPresentation() const { return controller_.verticalPresentation(); }
    void setCarriedPresentation(std::optional<VerticalPresentation> presentation);
    [[nodiscard]] bool iconsOnly() const { return controller_.iconsOnly(); }
    [[nodiscard]] bool compactStickyTiles() const { return controller_.compactStickyTiles(); }
    void setColumnReveal(qreal reveal, bool modeTransition = false);
    void setColumnManaged(bool managed) { controller_.setColumnManaged(managed); }
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
    [[nodiscard]] QSize availableSize() const { return controller_.availableSize(); }
    [[nodiscard]] const TabLayout& tabLayout() const { return controller_.tabLayout(); }
    [[nodiscard]] std::shared_ptr<const TabStripFrame> captureStripFrame(const QFont& font, Qt::LayoutDirection direction);
    [[nodiscard]] std::shared_ptr<const TabForegroundFrame> captureForegroundFrame(const QFont& font, Qt::LayoutDirection direction);
    // State publication is independent of QWidget painting and opt-in. The
    // notification only marks data dirty; consumers capture once when needed.
    void setPresentationPublishing(bool enabled);
    [[nodiscard]] std::shared_ptr<const TabPresentationFrame> presentationFrame();
    // Where tab coordinates of the layout start inside the widget.
    [[nodiscard]] QPoint tabOrigin() const;
    // Displayed and visible: what a click on it hits.
    [[nodiscard]] QRect tabRect(int index) const;
    // The painted contour, clipped to the displayed part of the tab.
    [[nodiscard]] QRegion tabRegion(int index) const;
    [[nodiscard]] QRect displayTabRect(int index) const { return controller_.displayTabRect(index); }
    [[nodiscard]] int tabAt(QPoint local) const;
    // The title slot, for an inline editor (8.5) and tests.
    [[nodiscard]] QRect titleRect(int index) const;
    [[nodiscard]] QRect actionRect(int index) const;
    // The lane of the regular tabs: its offset and viewport, for the rules
    // that keep shown tabs in place when the strip changes size.
    [[nodiscard]] int scrollOffset() const;
    void setScrollOffset(int offset);
    [[nodiscard]] int stickyScrollOffset() const { return controller_.stickyScrollOffset(); }
    [[nodiscard]] int rowScrollOffset() const { return controller_.rowScrollOffset(); }
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
    [[nodiscard]] bool expanded() const { return controller_.expanded(); }
    void setExpanded(bool expanded, bool byWheel = false, bool animate = true);
    [[nodiscard]] bool wrappedRows() const;
    [[nodiscard]] bool rowTransitionRunning() const { return controller_.rowTransitionRunning(); }
    [[nodiscard]] QRect scrollViewportAt(QPoint local) const;
    [[nodiscard]] bool scrollsHorizontallyAt(QPoint local) const;
    [[nodiscard]] int hiddenTabs() const { return controller_.hiddenTabs(); }
    // Dragging over a collapsed strip shows all rows as an overlay.
    void setDragExpanded(bool expanded);
    [[nodiscard]] bool overlayActive() const;

    // Drag (tab strip design 6).
    void setDraggedTab(TabId tab);
    [[nodiscard]] int draggedIndex() const { return controller_.draggedIndex(); }
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
    [[nodiscard]] int dropIndex() const { return controller_.dropIndex(); }
    [[nodiscard]] bool dropSticky() const { return controller_.dropSticky(); }
    [[nodiscard]] int dropFolder() const { return controller_.dropFolder(); }
    // A whole folder being carried: its header and tabs leave the strip.
    void setDraggedFolder(QUuid folder);
    [[nodiscard]] int draggedFolder() const { return controller_.draggedFolder(); }
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

    // A group view changes the strip through its controller in one batch:
    // the environment is refreshed first, notifications are applied after.
    template <class Apply> void edit(Apply&& apply)
    {
        prepareController();
        apply();
        synchronizeController();
    }
    [[nodiscard]] TabStripController& controller() { return controller_; }
    [[nodiscard]] const TabStripController& controller() const { return controller_; }

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
    void presentationInvalidated();

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
    [[nodiscard]] TabStripEnvironment controllerEnvironment() const;
    void prepareController(bool measureEnvironment = false);
    void synchronizeController();
    void scheduleController();
    void applyControlState();
    void invalidatePresentation();
    void requestRepaint();
    void updateLiftedLayer();
    void paintLiftedTabs(QPainter& painter);
    void trackColumnPopup();
    void reconcileColumnHover();
    void applyPointerDecision(const TabPointerDecision& decision);
    static bool animationsEnabled();

    TabStripController controller_;
    QElapsedTimer controllerClock_;
    FrameClock frameDriver_;
    QTimer wakeTimer_;
    bool synchronizing_ = false;
    bool ready_ = false;
    QPointer<QMenu> columnPopup_;
    QMetaObject::Connection popupHidden_;
    QMetaObject::Connection popupDestroyed_;
    QPointer<QWidget> trailing_;
    QPointer<QWidget> end_;
    QToolButton* countButton_ = nullptr;
    QPointer<QWidget> scrollInput_;
    QPointer<QWidget> liftedLayer_;
    QRegion liftedPaintRegion_;
    mutable ElevationRenderer surfaceRenderer_;
    TabCarrierPainter incomingPainter_;
    TabDeparturePainter departurePainter_;
    quint64 presentationGeneration_ = 0;
    bool presentationPublishing_ = false;
    bool presentationPending_ = false;
};

} // namespace window_system
